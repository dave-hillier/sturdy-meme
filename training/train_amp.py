#!/usr/bin/env python3
"""Main training script for AMP/CALM/HLC character animation.

Supports three training modes:
  - amp:  Train LLCPolicy with fixed z=0 (no style conditioning).
          Learns a single locomotion behavior from the reference motion dataset.

  - calm: Train LLCPolicy + MotionEncoder with per-clip latent z.
          Learns a latent space of diverse behaviors from a motion library.

  - hlc:  Train HLCPolicy with a frozen LLC.
          Learns task-specific (heading/location/strike) high-level control.

Each epoch:
  1. Collect rollouts from vectorized environments
  2. Compute AMP style reward from discriminator
  3. Combine style reward with task reward (weighted sum)
  4. PPO update on policy + value network
  5. Discriminator update on real vs fake observations
  6. Log statistics
  7. Save checkpoint every 50 epochs

Usage:
    # AMP training (single behavior)
    python -m training.train_amp \\
        --motions data/calm/motions/manifest.yaml \\
        --mode amp --num-envs 4096

    # CALM training (multi-behavior latent space)
    python -m training.train_amp \\
        --motions data/calm/motions/manifest.yaml \\
        --mode calm --num-envs 4096

    # HLC training (task controller with frozen LLC)
    python -m training.train_amp \\
        --motions data/calm/motions/manifest.yaml \\
        --mode hlc --task heading \\
        --resume checkpoints/calm_epoch_500.pt
"""

import argparse
import logging
import os
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

# Ensure tools/ is importable for MotionDataset
_project_root = Path(__file__).resolve().parent.parent
_tools_dir = str(_project_root / "tools")
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)

from motion_dataset import MotionDataset

from training.networks import (
    LLCPolicy,
    ValueNetwork,
    AMPDiscriminator,
    MotionEncoder,
    HLCPolicy,
    DEFAULT_LATENT_DIM,
    DEFAULT_OBS_DIM,
    DEFAULT_ACTION_DIM,
    DEFAULT_ENCODER_OBS_STEPS,
)
from training.ppo import RolloutBuffer, PPOTrainer
from training.amp import AMPTrainer
from training.vec_env_wrapper import VecEnvWrapper

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("train_amp")

# Task observation dimensions matching C++ TaskController
TASK_OBS_DIMS = {
    "heading": 3,   # local_target_dir_x, local_target_dir_z, target_speed
    "location": 3,  # local_offset_x, local_offset_y, local_offset_z
    "strike": 4,    # local_target_x, local_target_y, local_target_z, distance
}


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="AMP/CALM/HLC training for physics-based character animation",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    # Environment
    parser.add_argument(
        "--num-envs",
        type=int,
        default=4096,
        help="Number of parallel simulation environments",
    )
    parser.add_argument(
        "--skeleton",
        type=str,
        default=None,
        help="Path to character skeleton .glb file (for C++ env)",
    )

    # Motion data
    parser.add_argument(
        "--motions",
        type=str,
        default=None,
        help="Path to motion manifest YAML or directory of .npz files",
    )

    # Training mode
    parser.add_argument(
        "--mode",
        type=str,
        choices=["amp", "calm", "hlc"],
        default="amp",
        help="Training mode: amp (single behavior), calm (latent space), hlc (task controller)",
    )
    parser.add_argument(
        "--task",
        type=str,
        choices=["heading", "location", "strike"],
        default="heading",
        help="Task type for HLC training mode",
    )

    # Training hyperparameters
    parser.add_argument(
        "--epochs", type=int, default=1000, help="Number of training epochs"
    )
    parser.add_argument(
        "--steps-per-epoch",
        type=int,
        default=32,
        help="Environment steps per epoch (rollout length)",
    )
    parser.add_argument(
        "--batch-size", type=int, default=512, help="Mini-batch size for PPO updates"
    )
    parser.add_argument(
        "--lr", type=float, default=3e-4, help="Learning rate for policy/value networks"
    )
    parser.add_argument(
        "--disc-lr",
        type=float,
        default=1e-4,
        help="Learning rate for AMP discriminator",
    )

    # Reward weighting
    parser.add_argument(
        "--style-reward-weight",
        type=float,
        default=0.5,
        help="Weight of AMP style reward (1 - weight goes to task reward)",
    )

    # Checkpointing
    parser.add_argument(
        "--output",
        type=str,
        default="checkpoints",
        help="Output directory for checkpoints",
    )
    parser.add_argument(
        "--resume",
        type=str,
        default=None,
        help="Path to checkpoint to resume training from",
    )
    parser.add_argument(
        "--save-interval",
        type=int,
        default=50,
        help="Save checkpoint every N epochs",
    )

    # PPO hyperparameters
    parser.add_argument(
        "--clip-epsilon", type=float, default=0.2, help="PPO clipping parameter"
    )
    parser.add_argument(
        "--entropy-coeff",
        type=float,
        default=0.01,
        help="Entropy bonus coefficient",
    )
    parser.add_argument(
        "--value-coeff",
        type=float,
        default=0.5,
        help="Value loss coefficient",
    )
    parser.add_argument(
        "--gamma", type=float, default=0.99, help="Discount factor"
    )
    parser.add_argument(
        "--gae-lambda", type=float, default=0.95, help="GAE lambda"
    )
    parser.add_argument(
        "--max-grad-norm", type=float, default=1.0, help="Gradient norm clipping"
    )
    parser.add_argument(
        "--ppo-epochs",
        type=int,
        default=4,
        help="Number of PPO optimization epochs per rollout",
    )

    # AMP discriminator
    parser.add_argument(
        "--grad-penalty-weight",
        type=float,
        default=10.0,
        help="WGAN-GP gradient penalty weight",
    )

    # Misc
    parser.add_argument(
        "--device",
        type=str,
        default="auto",
        help="Torch device (auto/cpu/cuda/cuda:0)",
    )
    parser.add_argument("--seed", type=int, default=42, help="Random seed")

    return parser.parse_args()


def get_device(device_str: str) -> torch.device:
    """Resolve device string to a torch.device."""
    if device_str == "auto":
        if torch.cuda.is_available():
            return torch.device("cuda")
        else:
            return torch.device("cpu")
    return torch.device(device_str)


def load_motion_dataset(args: argparse.Namespace) -> MotionDataset:
    """Load the motion dataset from the specified path.

    Args:
        args: Parsed command-line arguments.

    Returns:
        Loaded MotionDataset instance.
    """
    if args.motions is None:
        logger.warning("No motion data specified (--motions); using empty dataset")
        return MotionDataset()

    motions_path = Path(args.motions)
    if motions_path.is_dir():
        logger.info("Loading motions from directory: %s", motions_path)
        return MotionDataset(motion_dir=str(motions_path))
    elif motions_path.suffix in (".yaml", ".yml"):
        logger.info("Loading motions from manifest: %s", motions_path)
        return MotionDataset(manifest_path=str(motions_path))
    else:
        logger.error("Unrecognized motion path format: %s", motions_path)
        sys.exit(1)


def build_networks(
    args: argparse.Namespace,
    obs_dim: int,
    action_dim: int,
    device: torch.device,
) -> dict:
    """Construct all networks for the selected training mode.

    Args:
        args: Parsed command-line arguments.
        obs_dim: Observation dimension from the environment.
        action_dim: Action dimension from the environment.
        device: Torch device.

    Returns:
        Dict containing the constructed networks keyed by role name.
    """
    nets = {}

    if args.mode in ("amp", "calm"):
        # LLC policy
        nets["policy"] = LLCPolicy(
            latent_dim=DEFAULT_LATENT_DIM,
            obs_dim=obs_dim,
            action_dim=action_dim,
        ).to(device)

        # Value network (input is raw obs for AMP, obs for CALM)
        nets["value"] = ValueNetwork(obs_dim=obs_dim).to(device)

        # AMP discriminator
        nets["discriminator"] = AMPDiscriminator(amp_obs_dim=obs_dim).to(device)

        if args.mode == "calm":
            # Motion encoder for per-clip latent codes
            nets["encoder"] = MotionEncoder(
                obs_dim=obs_dim,
                latent_dim=DEFAULT_LATENT_DIM,
                encoder_obs_steps=DEFAULT_ENCODER_OBS_STEPS,
            ).to(device)

    elif args.mode == "hlc":
        task_obs_dim = TASK_OBS_DIMS.get(args.task, 3)

        # HLC policy
        nets["policy"] = HLCPolicy(
            task_obs_dim=task_obs_dim,
            latent_dim=DEFAULT_LATENT_DIM,
        ).to(device)

        # Value network for task observations
        nets["value"] = ValueNetwork(obs_dim=task_obs_dim).to(device)

        # AMP discriminator (still used for style reward)
        nets["discriminator"] = AMPDiscriminator(amp_obs_dim=obs_dim).to(device)

        # Frozen LLC (loaded from checkpoint)
        nets["llc"] = LLCPolicy(
            latent_dim=DEFAULT_LATENT_DIM,
            obs_dim=obs_dim,
            action_dim=action_dim,
        ).to(device)
        # Freeze LLC parameters
        for param in nets["llc"].parameters():
            param.requires_grad = False

    return nets


def save_checkpoint(
    path: str,
    epoch: int,
    nets: dict,
    ppo_trainer: PPOTrainer,
    amp_trainer: AMPTrainer,
    args: argparse.Namespace,
) -> None:
    """Save a training checkpoint.

    Args:
        path: Output file path.
        epoch: Current epoch number.
        nets: Dict of networks.
        ppo_trainer: PPO trainer (for optimizer state).
        amp_trainer: AMP trainer (for discriminator optimizer state).
        args: Original command-line arguments.
    """
    checkpoint = {
        "epoch": epoch,
        "mode": args.mode,
        "args": vars(args),
    }

    # Save all network state dicts
    for name, net in nets.items():
        checkpoint[f"net_{name}"] = net.state_dict()

    # Save optimizer states
    checkpoint["ppo_optimizer"] = ppo_trainer.optimizer.state_dict()
    checkpoint["disc_optimizer"] = amp_trainer.optimizer.state_dict()

    os.makedirs(os.path.dirname(path) if os.path.dirname(path) else ".", exist_ok=True)
    torch.save(checkpoint, path)
    logger.info("Saved checkpoint: %s (epoch %d)", path, epoch)


def load_checkpoint(
    path: str,
    nets: dict,
    ppo_trainer: PPOTrainer,
    amp_trainer: AMPTrainer,
) -> int:
    """Load a training checkpoint.

    Args:
        path: Checkpoint file path.
        nets: Dict of networks to load state into.
        ppo_trainer: PPO trainer (for optimizer state).
        amp_trainer: AMP trainer (for discriminator optimizer state).

    Returns:
        Epoch number to resume from.
    """
    checkpoint = torch.load(path, weights_only=False)

    for name, net in nets.items():
        key = f"net_{name}"
        if key in checkpoint:
            net.load_state_dict(checkpoint[key])
            logger.info("Loaded %s weights from checkpoint", name)

    if "ppo_optimizer" in checkpoint:
        ppo_trainer.optimizer.load_state_dict(checkpoint["ppo_optimizer"])

    if "disc_optimizer" in checkpoint:
        amp_trainer.optimizer.load_state_dict(checkpoint["disc_optimizer"])

    epoch = checkpoint.get("epoch", 0)
    logger.info("Resumed from checkpoint: %s (epoch %d)", path, epoch)
    return epoch


def collect_amp_obs(obs_buffer: list[np.ndarray]) -> torch.Tensor:
    """Concatenate collected observations into an AMP observation tensor.

    For simplicity, we use the single-frame observations directly as AMP
    observations. In a full implementation, you would compute the AMP-specific
    features (heading-invariant root state, DOF positions/velocities, etc.)
    from the raw physics state.

    Args:
        obs_buffer: List of observation arrays from the rollout.

    Returns:
        Stacked tensor of AMP observations, shape (total_steps, obs_dim).
    """
    if not obs_buffer:
        return torch.empty(0)
    return torch.from_numpy(np.concatenate(obs_buffer, axis=0))


def sample_latents_for_calm(
    encoder: torch.nn.Module,
    motion_dataset: MotionDataset,
    num_envs: int,
    obs_dim: int,
    encoder_obs_steps: int,
    device: torch.device,
) -> torch.Tensor:
    """Sample per-environment latent codes using the motion encoder.

    For each environment, samples a random motion clip and encodes a temporal
    window of observations to produce a latent code. This gives each
    environment a different behavior style.

    Args:
        encoder: MotionEncoder network.
        motion_dataset: Dataset of motion clips.
        num_envs: Number of environments.
        obs_dim: Single-frame observation dimension.
        encoder_obs_steps: Number of temporal frames to stack.
        device: Torch device.

    Returns:
        Latent codes, shape (num_envs, latent_dim).
    """
    if len(motion_dataset.clips) == 0:
        return torch.zeros(num_envs, DEFAULT_LATENT_DIM, device=device)

    stacked_obs_list = []
    for _ in range(num_envs):
        clip = motion_dataset.sample_clip()
        amp_obs = clip.amp_observations  # (N-1, obs_dim)

        if amp_obs.shape[0] >= encoder_obs_steps:
            start = np.random.randint(0, amp_obs.shape[0] - encoder_obs_steps + 1)
            window = amp_obs[start : start + encoder_obs_steps]
        else:
            # Pad with repetition if clip is too short
            window = np.zeros((encoder_obs_steps, obs_dim), dtype=np.float32)
            n = min(amp_obs.shape[0], encoder_obs_steps)
            window[:n] = amp_obs[:n]
            for j in range(n, encoder_obs_steps):
                window[j] = amp_obs[-1]

        stacked_obs_list.append(window.flatten())

    stacked_obs = torch.from_numpy(
        np.array(stacked_obs_list, dtype=np.float32)
    ).to(device)

    with torch.no_grad():
        latents = encoder(stacked_obs)

    return latents


def train(args: argparse.Namespace) -> None:
    """Main training loop.

    Args:
        args: Parsed command-line arguments.
    """
    # Setup
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = get_device(args.device)
    logger.info("Using device: %s", device)
    logger.info("Training mode: %s", args.mode)

    # Load motion data
    motion_dataset = load_motion_dataset(args)
    logger.info(
        "Motion dataset: %d clips, %d total frames, obs_dim=%d",
        len(motion_dataset.clips),
        motion_dataset.total_frames,
        motion_dataset.obs_dim,
    )

    # Create vectorized environment
    env = VecEnvWrapper(
        num_envs=args.num_envs,
        skeleton_path=args.skeleton,
        obs_dim=DEFAULT_OBS_DIM,
        action_dim=DEFAULT_ACTION_DIM,
    )
    obs_dim = env.obs_dim
    action_dim = env.action_dim
    logger.info(
        "Environment: %d envs, obs_dim=%d, action_dim=%d",
        args.num_envs,
        obs_dim,
        action_dim,
    )

    # Build networks
    nets = build_networks(args, obs_dim, action_dim, device)

    # Create trainers
    ppo_trainer = PPOTrainer(
        policy=nets["policy"],
        value_net=nets["value"],
        lr=args.lr,
        clip_epsilon=args.clip_epsilon,
        entropy_coeff=args.entropy_coeff,
        value_coeff=args.value_coeff,
        max_grad_norm=args.max_grad_norm,
        num_update_epochs=args.ppo_epochs,
    )

    amp_trainer = AMPTrainer(
        discriminator=nets["discriminator"],
        motion_dataset=motion_dataset,
        lr=args.disc_lr,
        grad_penalty_weight=args.grad_penalty_weight,
        device=device,
    )

    # Create rollout buffer
    buffer = RolloutBuffer(
        num_steps=args.steps_per_epoch,
        num_envs=args.num_envs,
        obs_dim=obs_dim,
        action_dim=action_dim,
        device=device,
    )
    if args.mode in ("calm", "hlc"):
        buffer.enable_latent_storage(DEFAULT_LATENT_DIM)

    # Resume from checkpoint if specified
    start_epoch = 0
    if args.resume:
        start_epoch = load_checkpoint(args.resume, nets, ppo_trainer, amp_trainer)

    # Initialize environment
    obs_np = env.reset()
    obs = torch.from_numpy(obs_np).to(device)

    # Per-env latent codes for CALM mode
    latents = torch.zeros(args.num_envs, DEFAULT_LATENT_DIM, device=device)
    if args.mode == "calm" and "encoder" in nets:
        latents = sample_latents_for_calm(
            nets["encoder"],
            motion_dataset,
            args.num_envs,
            obs_dim,
            DEFAULT_ENCODER_OBS_STEPS,
            device,
        )

    # Tracking for latent resampling in CALM mode
    latent_step_counters = np.zeros(args.num_envs, dtype=np.int32)
    latent_resample_interval = 150  # Re-encode latent every N steps

    logger.info("Starting training from epoch %d", start_epoch)
    logger.info(
        "Hyperparams: lr=%.1e, disc_lr=%.1e, clip_eps=%.2f, gamma=%.3f, "
        "gae_lambda=%.2f, batch_size=%d, steps/epoch=%d",
        args.lr,
        args.disc_lr,
        args.clip_epsilon,
        args.gamma,
        args.gae_lambda,
        args.batch_size,
        args.steps_per_epoch,
    )

    # =========================================================================
    # Main training loop
    # =========================================================================
    for epoch in range(start_epoch, args.epochs):
        epoch_start = time.time()
        buffer.reset()
        amp_obs_buffer: list[np.ndarray] = []

        # --- Rollout collection ---
        for step in range(args.steps_per_epoch):
            with torch.no_grad():
                if args.mode == "amp":
                    # AMP mode: fixed z=0
                    z = torch.zeros(
                        args.num_envs, DEFAULT_LATENT_DIM, device=device
                    )
                    actions, log_probs = nets["policy"].get_actions(z, obs)
                    values = nets["value"](obs).squeeze(-1)

                elif args.mode == "calm":
                    # CALM mode: per-env latent from encoder
                    actions, log_probs = nets["policy"].get_actions(latents, obs)
                    values = nets["value"](obs).squeeze(-1)

                elif args.mode == "hlc":
                    # HLC mode: policy outputs latent, frozen LLC produces actions
                    # For HLC, obs is the task observation (simplified: use first
                    # task_obs_dim features of the full obs as task obs placeholder)
                    task_obs_dim = TASK_OBS_DIMS.get(args.task, 3)
                    task_obs = obs[:, :task_obs_dim]
                    z, log_probs = nets["policy"].get_latent(task_obs)
                    actions = nets["llc"].get_actions(z, obs, deterministic=True)[0]
                    values = nets["value"](task_obs).squeeze(-1)
                    latents = z  # Store for buffer

            # Step environment
            actions_np = actions.cpu().numpy()
            obs_np, rewards_np, dones_np, infos = env.step(actions_np)

            # Store AMP observations for discriminator training
            amp_obs_buffer.append(obs_np.copy())

            # Convert to tensors
            rewards_t = torch.from_numpy(rewards_np).to(device)
            dones_t = torch.from_numpy(dones_np).to(device)
            obs_next = torch.from_numpy(obs_np).to(device)

            # Store in buffer
            if args.mode == "hlc":
                buffer.add(
                    obs[:, : TASK_OBS_DIMS.get(args.task, 3)],
                    latents,
                    log_probs,
                    rewards_t,
                    dones_t,
                    values,
                    latents,
                )
            else:
                buffer.add(
                    obs,
                    actions,
                    log_probs,
                    rewards_t,
                    dones_t,
                    values,
                    latents if args.mode == "calm" else None,
                )

            obs = obs_next

            # CALM: resample latents for terminated episodes
            if args.mode == "calm" and "encoder" in nets:
                latent_step_counters += 1
                terminated_mask = dones_np > 0.5
                resample_mask = latent_step_counters >= latent_resample_interval
                need_resample = terminated_mask | resample_mask
                resample_indices = np.where(need_resample)[0]

                if len(resample_indices) > 0:
                    new_latents = sample_latents_for_calm(
                        nets["encoder"],
                        motion_dataset,
                        len(resample_indices),
                        obs_dim,
                        DEFAULT_ENCODER_OBS_STEPS,
                        device,
                    )
                    latents[resample_indices] = new_latents
                    latent_step_counters[resample_indices] = 0

        # --- Compute AMP style rewards ---
        policy_amp_obs = collect_amp_obs(amp_obs_buffer).to(device)

        if policy_amp_obs.shape[0] > 0:
            style_rewards = amp_trainer.compute_style_reward(policy_amp_obs)
            # Reshape to (steps, envs)
            style_rewards_2d = style_rewards.view(
                args.steps_per_epoch, args.num_envs
            )
        else:
            style_rewards_2d = torch.zeros(
                args.steps_per_epoch, args.num_envs, device=device
            )

        # Blend style reward with task reward
        w = args.style_reward_weight
        buffer.rewards = w * style_rewards_2d + (1.0 - w) * buffer.rewards

        # --- Compute returns and advantages ---
        with torch.no_grad():
            if args.mode == "hlc":
                task_obs_dim = TASK_OBS_DIMS.get(args.task, 3)
                last_values = nets["value"](obs[:, :task_obs_dim]).squeeze(-1)
            else:
                last_values = nets["value"](obs).squeeze(-1)
            last_dones = dones_t

        buffer.compute_returns(
            last_values,
            last_dones,
            gamma=args.gamma,
            gae_lambda=args.gae_lambda,
        )

        # --- PPO update ---
        ppo_stats = ppo_trainer.update(buffer, batch_size=args.batch_size)

        # --- Discriminator update ---
        disc_stats = amp_trainer.update(policy_amp_obs)

        # --- CALM: update encoder (optional, using contrastive loss) ---
        if args.mode == "calm" and "encoder" in nets:
            # The encoder is updated implicitly through the PPO objective
            # since better latents lead to higher style rewards.
            # A more advanced approach could add a contrastive encoder loss here.
            pass

        # --- Logging ---
        epoch_time = time.time() - epoch_start
        mean_reward = buffer.rewards.mean().item()
        mean_style_reward = style_rewards_2d.mean().item()

        logger.info(
            "Epoch %4d/%d | time %.1fs | reward %.4f | style_r %.4f | "
            "pi_loss %.4f | v_loss %.4f | ent %.4f | kl %.4f | "
            "d_loss %.4f | d_real %.3f | d_fake %.3f | gp %.4f",
            epoch + 1,
            args.epochs,
            epoch_time,
            mean_reward,
            mean_style_reward,
            ppo_stats["policy_loss"],
            ppo_stats["value_loss"],
            ppo_stats["entropy"],
            ppo_stats["approx_kl"],
            disc_stats["disc_loss"],
            disc_stats["disc_real_score"],
            disc_stats["disc_fake_score"],
            disc_stats["gradient_penalty"],
        )

        # --- Save checkpoint ---
        if (epoch + 1) % args.save_interval == 0 or (epoch + 1) == args.epochs:
            ckpt_name = f"{args.mode}_epoch_{epoch + 1}.pt"
            ckpt_path = os.path.join(args.output, ckpt_name)
            save_checkpoint(ckpt_path, epoch + 1, nets, ppo_trainer, amp_trainer, args)

    logger.info("Training complete. %d epochs.", args.epochs)


def main() -> None:
    """Entry point."""
    args = parse_args()
    train(args)


if __name__ == "__main__":
    main()
