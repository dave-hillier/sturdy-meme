#!/usr/bin/env python3
"""PPO training loop for UniCon policy.

Usage:
    python -m tools.unicon_training.train [--config config.json] [--output generated/unicon]

Implements PPO with:
    - Clipped surrogate objective
    - GAE advantage estimation
    - Constrained multi-objective reward (paper Section 3.3)
    - RSIS initialization (paper Section 3.4)
    - Policy variance annealing (paper Section 3.5)
    - Motion balancer for dataset diversity
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

from .config import TrainingConfig, PPOConfig
from .policy import MLPPolicy, ValueNetwork
from .environment import UniConEnv
from .export import export_policy
from .motion_loader import load_motion_directory, generate_standing_motion


class RolloutBuffer:
    """Stores trajectory data for PPO updates."""

    def __init__(self, num_envs: int, horizon: int, obs_dim: int, act_dim: int,
                 device: str = "cpu"):
        self.num_envs = num_envs
        self.horizon = horizon
        self.device = device

        self.observations = torch.zeros(horizon, num_envs, obs_dim, device=device)
        self.actions = torch.zeros(horizon, num_envs, act_dim, device=device)
        self.log_probs = torch.zeros(horizon, num_envs, device=device)
        self.rewards = torch.zeros(horizon, num_envs, device=device)
        self.dones = torch.zeros(horizon, num_envs, device=device)
        self.values = torch.zeros(horizon, num_envs, device=device)
        self.advantages = torch.zeros(horizon, num_envs, device=device)
        self.returns = torch.zeros(horizon, num_envs, device=device)

        self.step = 0

    def add(self, obs, action, log_prob, reward, done, value):
        self.observations[self.step] = obs
        self.actions[self.step] = action
        self.log_probs[self.step] = log_prob
        self.rewards[self.step] = reward
        self.dones[self.step] = done
        self.values[self.step] = value
        self.step += 1

    def compute_gae(self, last_value: torch.Tensor, gamma: float, gae_lambda: float):
        """Compute generalized advantage estimation."""
        last_gae = 0
        for t in reversed(range(self.horizon)):
            if t == self.horizon - 1:
                next_value = last_value
            else:
                next_value = self.values[t + 1]
            next_non_terminal = 1.0 - self.dones[t]
            delta = self.rewards[t] + gamma * next_value * next_non_terminal - self.values[t]
            self.advantages[t] = last_gae = delta + gamma * gae_lambda * next_non_terminal * last_gae
        self.returns = self.advantages + self.values

    def flatten(self):
        """Flatten (horizon, num_envs, ...) -> (horizon * num_envs, ...)."""
        batch_size = self.horizon * self.num_envs
        return {
            "observations": self.observations.reshape(batch_size, -1),
            "actions": self.actions.reshape(batch_size, -1),
            "log_probs": self.log_probs.reshape(batch_size),
            "advantages": self.advantages.reshape(batch_size),
            "returns": self.returns.reshape(batch_size),
        }

    def reset(self):
        self.step = 0


class PPOTrainer:
    """PPO trainer for the UniCon policy."""

    def __init__(self, config: TrainingConfig):
        self.config = config
        self.ppo = config.ppo
        self.device = torch.device(config.device if torch.cuda.is_available() else "cpu")

        # Load motion data
        print(f"Loading motion data from {config.motion_dir}...")
        self.motion_data = load_motion_directory(
            config.motion_dir, config.humanoid.num_bodies
        )
        if not self.motion_data:
            print("No motion data found. Using standing motion for bootstrap training.")
            self.motion_data = {"standing": generate_standing_motion(config.humanoid.num_bodies)}

        print(f"Loaded {len(self.motion_data)} motion clips")

        # Create environments
        # Note: For production training with 4096 envs, use vectorized MuJoCo
        # or Isaac Gym. Here we create a smaller set for CPU training.
        actual_num_envs = min(config.ppo.num_envs, 32)  # Cap for CPU training
        print(f"Creating {actual_num_envs} environments...")
        self.envs = [
            UniConEnv(config, motion_data=self.motion_data)
            for _ in range(actual_num_envs)
        ]
        self.actual_num_envs = actual_num_envs

        # Get dimensions from first env
        obs_dim = self.envs[0].obs_dim
        act_dim = self.envs[0].act_dim

        # Create policy and value networks
        self.policy = MLPPolicy(obs_dim, act_dim, config.policy).to(self.device)
        self.value_net = ValueNetwork(obs_dim, config.policy).to(self.device)

        # Optimizer
        self.optimizer = optim.Adam(
            list(self.policy.parameters()) + list(self.value_net.parameters()),
            lr=config.ppo.learning_rate,
        )

        # Rollout buffer
        self.buffer = RolloutBuffer(
            actual_num_envs, config.ppo.samples_per_env,
            obs_dim, act_dim, self.device,
        )

        # Metrics
        self.iteration = 0
        self.total_timesteps = 0
        self.best_reward = -float("inf")

        # Output directory
        self.output_dir = Path(config.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        print(f"Policy: input_dim={obs_dim}, output_dim={act_dim}")
        print(f"Device: {self.device}")

    def train(self):
        """Run the full training loop."""
        print(f"\nStarting training for {self.ppo.num_iterations} iterations...")

        # Initialize environments
        obs_list = [env.reset(seed=self.config.seed + i) for i, env in enumerate(self.envs)]
        obs = torch.tensor(np.stack(obs_list), dtype=torch.float32, device=self.device)

        for iteration in range(self.ppo.num_iterations):
            self.iteration = iteration
            iter_start = time.time()

            # Anneal policy variance
            self._anneal_log_std()

            # Collect rollouts
            self.buffer.reset()
            episode_rewards = []
            episode_lengths = []

            with torch.no_grad():
                for step in range(self.ppo.samples_per_env):
                    # Get action from policy
                    dist = self.policy.get_distribution(obs)
                    action = dist.sample()
                    log_prob = dist.log_prob(action).sum(dim=-1)
                    value = self.value_net(obs)

                    # Step environments
                    action_np = action.cpu().numpy()
                    new_obs_list = []
                    rewards = []
                    dones = []

                    for i, env in enumerate(self.envs):
                        ob, reward, done, info = env.step(action_np[i])
                        rewards.append(reward)
                        dones.append(float(done))

                        if done:
                            episode_rewards.append(
                                info.get("episode_reward", reward)
                            )
                            episode_lengths.append(info.get("episode_length", 0))
                            ob = env.reset()

                        new_obs_list.append(ob)

                    new_obs = torch.tensor(
                        np.stack(new_obs_list), dtype=torch.float32, device=self.device
                    )
                    reward_t = torch.tensor(rewards, dtype=torch.float32, device=self.device)
                    done_t = torch.tensor(dones, dtype=torch.float32, device=self.device)

                    self.buffer.add(obs, action, log_prob, reward_t, done_t, value)
                    obs = new_obs

                # Compute GAE with last value
                last_value = self.value_net(obs)
                self.buffer.compute_gae(last_value, self.ppo.gamma, self.ppo.gae_lambda)

            # PPO update
            metrics = self._ppo_update()

            self.total_timesteps += self.actual_num_envs * self.ppo.samples_per_env
            iter_time = time.time() - iter_start

            # Logging
            if iteration % self.ppo.log_interval == 0:
                mean_reward = np.mean(episode_rewards) if episode_rewards else 0.0
                mean_length = np.mean(episode_lengths) if episode_lengths else 0.0
                print(
                    f"Iter {iteration:6d} | "
                    f"reward={mean_reward:.4f} | "
                    f"ep_len={mean_length:.0f} | "
                    f"policy_loss={metrics['policy_loss']:.4f} | "
                    f"value_loss={metrics['value_loss']:.4f} | "
                    f"entropy={metrics['entropy']:.4f} | "
                    f"kl={metrics['approx_kl']:.6f} | "
                    f"timesteps={self.total_timesteps} | "
                    f"time={iter_time:.2f}s"
                )

            # Checkpoint
            if iteration % self.ppo.checkpoint_interval == 0 and iteration > 0:
                self._save_checkpoint(iteration)

                mean_reward = np.mean(episode_rewards) if episode_rewards else 0.0
                if mean_reward > self.best_reward:
                    self.best_reward = mean_reward
                    self._export_best()

        # Final export
        self._save_checkpoint(self.ppo.num_iterations)
        self._export_best()
        print(f"\nTraining complete. Best reward: {self.best_reward:.4f}")

    def _ppo_update(self) -> dict:
        """Perform PPO update epochs on collected rollouts."""
        data = self.buffer.flatten()

        # Normalize advantages
        adv = data["advantages"]
        adv = (adv - adv.mean()) / (adv.std() + 1e-8)

        total_policy_loss = 0.0
        total_value_loss = 0.0
        total_entropy = 0.0
        total_approx_kl = 0.0
        num_updates = 0

        batch_size = adv.shape[0]
        indices = torch.randperm(batch_size, device=self.device)

        for epoch in range(self.ppo.num_epochs):
            for start in range(0, batch_size, self.ppo.minibatch_size):
                end = start + self.ppo.minibatch_size
                mb_indices = indices[start:end]

                mb_obs = data["observations"][mb_indices]
                mb_actions = data["actions"][mb_indices]
                mb_old_log_probs = data["log_probs"][mb_indices]
                mb_advantages = adv[mb_indices]
                mb_returns = data["returns"][mb_indices]

                # Evaluate current policy
                new_log_probs, entropy = self.policy.evaluate_actions(mb_obs, mb_actions)
                new_values = self.value_net(mb_obs)

                # Policy loss (clipped surrogate)
                ratio = (new_log_probs - mb_old_log_probs).exp()
                surr1 = ratio * mb_advantages
                surr2 = torch.clamp(ratio, 1 - self.ppo.clip_epsilon,
                                    1 + self.ppo.clip_epsilon) * mb_advantages
                policy_loss = -torch.min(surr1, surr2).mean()

                # Value loss
                value_loss = nn.functional.mse_loss(new_values, mb_returns)

                # Entropy bonus
                entropy_loss = -entropy.mean()

                # Total loss
                loss = (policy_loss
                        + self.ppo.value_loss_coeff * value_loss
                        + self.ppo.entropy_coeff * entropy_loss)

                self.optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(
                    list(self.policy.parameters()) + list(self.value_net.parameters()),
                    self.ppo.max_grad_norm,
                )
                self.optimizer.step()

                # Tracking
                with torch.no_grad():
                    approx_kl = (mb_old_log_probs - new_log_probs).mean()
                    total_policy_loss += policy_loss.item()
                    total_value_loss += value_loss.item()
                    total_entropy += entropy.mean().item()
                    total_approx_kl += approx_kl.item()
                    num_updates += 1

            # Reshuffle indices for next epoch
            indices = torch.randperm(batch_size, device=self.device)

        n = max(num_updates, 1)
        return {
            "policy_loss": total_policy_loss / n,
            "value_loss": total_value_loss / n,
            "entropy": total_entropy / n,
            "approx_kl": total_approx_kl / n,
        }

    def _anneal_log_std(self):
        """Linearly anneal policy log_std over training."""
        progress = min(self.iteration / max(self.config.log_std_anneal_iterations, 1), 1.0)
        target = (self.config.initial_log_std
                  + progress * (self.config.final_log_std - self.config.initial_log_std))
        self.policy.log_std.data.fill_(target)

    def _save_checkpoint(self, iteration: int):
        """Save training checkpoint."""
        path = self.output_dir / f"checkpoint_{iteration:06d}.pt"
        torch.save({
            "iteration": iteration,
            "policy_state_dict": self.policy.state_dict(),
            "value_state_dict": self.value_net.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
            "total_timesteps": self.total_timesteps,
            "best_reward": self.best_reward,
        }, path)
        print(f"  Checkpoint saved: {path}")

    def _export_best(self):
        """Export the best policy weights in C++ binary format."""
        weights_path = self.output_dir / "policy_weights.bin"
        export_policy(self.policy, str(weights_path))
        print(f"  Exported C++ weights: {weights_path}")


def main():
    parser = argparse.ArgumentParser(description="Train UniCon policy with PPO")
    parser.add_argument("--config", type=str, default=None,
                        help="Path to JSON config file (overrides defaults)")
    parser.add_argument("--output", type=str, default="generated/unicon",
                        help="Output directory for weights and checkpoints")
    parser.add_argument("--motions", type=str, default="assets/motions",
                        help="Directory containing motion capture files")
    parser.add_argument("--iterations", type=int, default=None,
                        help="Override number of training iterations")
    parser.add_argument("--num-envs", type=int, default=None,
                        help="Override number of parallel environments")
    parser.add_argument("--device", type=str, default=None,
                        help="Device: 'cuda' or 'cpu'")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed")
    args = parser.parse_args()

    # Build config
    config = TrainingConfig()
    config.output_dir = args.output
    config.motion_dir = args.motions
    config.seed = args.seed

    if args.config:
        with open(args.config) as f:
            overrides = json.load(f)
        # Apply overrides to config (simple flat merge)
        for key, value in overrides.items():
            if hasattr(config, key):
                setattr(config, key, value)

    if args.iterations is not None:
        config.ppo.num_iterations = args.iterations
    if args.num_envs is not None:
        config.ppo.num_envs = args.num_envs
    if args.device is not None:
        config.device = args.device

    # Set seeds
    np.random.seed(config.seed)
    torch.manual_seed(config.seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed(config.seed)

    trainer = PPOTrainer(config)
    trainer.train()


if __name__ == "__main__":
    main()
