"""Proximal Policy Optimization (PPO) with Generalized Advantage Estimation.

Implements the PPO-Clip algorithm with a rollout buffer for on-policy data
collection from vectorized environments.

References:
  - Schulman et al., "Proximal Policy Optimization Algorithms", 2017
  - Schulman et al., "High-Dimensional Continuous Control Using GAE", 2015
"""

from typing import Optional

import torch
import torch.nn as nn
import numpy as np


class RolloutBuffer:
    """Stores rollout data from vectorized environments for PPO training.

    Collects observations, actions, log probabilities, rewards, done flags,
    and value estimates for N steps from M parallel environments. After
    collection, computes GAE-based returns and advantages for the PPO update.

    Args:
        num_steps: Number of environment steps to collect per rollout.
        num_envs: Number of parallel environments.
        obs_dim: Dimension of the observation vector.
        action_dim: Dimension of the action vector.
        device: Torch device for tensor storage.
    """

    def __init__(
        self,
        num_steps: int,
        num_envs: int,
        obs_dim: int,
        action_dim: int,
        device: torch.device = torch.device("cpu"),
    ):
        self.num_steps = num_steps
        self.num_envs = num_envs
        self.obs_dim = obs_dim
        self.action_dim = action_dim
        self.device = device

        # Pre-allocate storage tensors
        self.observations = torch.zeros(
            (num_steps, num_envs, obs_dim), dtype=torch.float32, device=device
        )
        self.actions = torch.zeros(
            (num_steps, num_envs, action_dim), dtype=torch.float32, device=device
        )
        self.log_probs = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )
        self.rewards = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )
        self.dones = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )
        self.values = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )

        # Computed after rollout
        self.returns = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )
        self.advantages = torch.zeros(
            (num_steps, num_envs), dtype=torch.float32, device=device
        )

        # Optional: latent codes for CALM/HLC training
        self.latents: Optional[torch.Tensor] = None

        self.step = 0

    def reset(self) -> None:
        """Reset the buffer for a new rollout collection."""
        self.step = 0

    def enable_latent_storage(self, latent_dim: int) -> None:
        """Enable storage for latent codes (used in CALM/HLC modes).

        Args:
            latent_dim: Dimension of the latent code vector.
        """
        self.latents = torch.zeros(
            (self.num_steps, self.num_envs, latent_dim),
            dtype=torch.float32,
            device=self.device,
        )

    def add(
        self,
        obs: torch.Tensor,
        actions: torch.Tensor,
        log_probs: torch.Tensor,
        rewards: torch.Tensor,
        dones: torch.Tensor,
        values: torch.Tensor,
        latents: Optional[torch.Tensor] = None,
    ) -> None:
        """Add one timestep of vectorized rollout data.

        Args:
            obs: Observations, shape (num_envs, obs_dim).
            actions: Actions taken, shape (num_envs, action_dim).
            log_probs: Log probabilities of actions, shape (num_envs,).
            rewards: Rewards received, shape (num_envs,).
            dones: Episode termination flags, shape (num_envs,).
            values: Value estimates, shape (num_envs,).
            latents: Optional latent codes, shape (num_envs, latent_dim).
        """
        assert self.step < self.num_steps, (
            f"Buffer is full: step {self.step} >= num_steps {self.num_steps}"
        )

        self.observations[self.step] = obs
        self.actions[self.step] = actions
        self.log_probs[self.step] = log_probs
        self.rewards[self.step] = rewards
        self.dones[self.step] = dones
        self.values[self.step] = values

        if latents is not None and self.latents is not None:
            self.latents[self.step] = latents

        self.step += 1

    def compute_returns(
        self,
        last_values: torch.Tensor,
        last_dones: torch.Tensor,
        gamma: float = 0.99,
        gae_lambda: float = 0.95,
    ) -> None:
        """Compute GAE advantages and discounted returns.

        Uses Generalized Advantage Estimation (GAE) to compute advantages
        that balance bias and variance in the return estimates.

        Args:
            last_values: Value estimate for the state after the last step,
                shape (num_envs,).
            last_dones: Done flags for the state after the last step,
                shape (num_envs,).
            gamma: Discount factor.
            gae_lambda: GAE lambda for advantage estimation.
        """
        gae = torch.zeros(self.num_envs, dtype=torch.float32, device=self.device)
        next_values = last_values
        next_dones = last_dones

        for t in reversed(range(self.num_steps)):
            # TD residual: delta = r_t + gamma * V(s_{t+1}) * (1 - done) - V(s_t)
            delta = (
                self.rewards[t]
                + gamma * next_values * (1.0 - next_dones)
                - self.values[t]
            )
            # GAE: A_t = delta_t + gamma * lambda * (1 - done) * A_{t+1}
            gae = delta + gamma * gae_lambda * (1.0 - next_dones) * gae
            self.advantages[t] = gae
            self.returns[t] = gae + self.values[t]

            next_values = self.values[t]
            next_dones = self.dones[t]

    def get_batches(
        self, batch_size: int
    ) -> list[dict[str, torch.Tensor]]:
        """Generate randomized mini-batches from the rollout data.

        Flattens the (num_steps, num_envs) data into a single dimension and
        yields randomly shuffled mini-batches for the PPO update.

        Args:
            batch_size: Number of samples per mini-batch.

        Returns:
            List of dicts, each containing:
                "obs": (batch_size, obs_dim)
                "actions": (batch_size, action_dim)
                "log_probs": (batch_size,)
                "returns": (batch_size,)
                "advantages": (batch_size,)
                "values": (batch_size,)
                "latents": (batch_size, latent_dim) [if latents enabled]
        """
        total_samples = self.num_steps * self.num_envs

        # Flatten all data from (steps, envs, ...) to (steps * envs, ...)
        flat_obs = self.observations[: self.num_steps].reshape(total_samples, -1)
        flat_actions = self.actions[: self.num_steps].reshape(total_samples, -1)
        flat_log_probs = self.log_probs[: self.num_steps].reshape(total_samples)
        flat_returns = self.returns[: self.num_steps].reshape(total_samples)
        flat_advantages = self.advantages[: self.num_steps].reshape(total_samples)
        flat_values = self.values[: self.num_steps].reshape(total_samples)

        flat_latents = None
        if self.latents is not None:
            flat_latents = self.latents[: self.num_steps].reshape(total_samples, -1)

        # Normalize advantages
        adv_mean = flat_advantages.mean()
        adv_std = flat_advantages.std() + 1e-8
        flat_advantages = (flat_advantages - adv_mean) / adv_std

        # Random permutation
        indices = torch.randperm(total_samples, device=self.device)
        batches = []

        for start in range(0, total_samples, batch_size):
            end = min(start + batch_size, total_samples)
            batch_indices = indices[start:end]

            batch = {
                "obs": flat_obs[batch_indices],
                "actions": flat_actions[batch_indices],
                "log_probs": flat_log_probs[batch_indices],
                "returns": flat_returns[batch_indices],
                "advantages": flat_advantages[batch_indices],
                "values": flat_values[batch_indices],
            }

            if flat_latents is not None:
                batch["latents"] = flat_latents[batch_indices]

            batches.append(batch)

        return batches


class PPOTrainer:
    """PPO-Clip trainer for policy and value network optimization.

    Implements the clipped surrogate objective with value function clipping
    and entropy bonus. Uses Adam optimizer with gradient norm clipping.

    Args:
        policy: The policy network (LLCPolicy or HLCPolicy).
        value_net: The value function network.
        lr: Learning rate for the Adam optimizer.
        clip_epsilon: PPO clipping parameter for the surrogate objective.
        entropy_coeff: Coefficient for the entropy bonus term.
        value_coeff: Coefficient for the value function loss.
        max_grad_norm: Maximum gradient norm for clipping.
        num_update_epochs: Number of PPO optimization epochs per rollout.
    """

    def __init__(
        self,
        policy: nn.Module,
        value_net: nn.Module,
        lr: float = 3e-4,
        clip_epsilon: float = 0.2,
        entropy_coeff: float = 0.01,
        value_coeff: float = 0.5,
        max_grad_norm: float = 1.0,
        num_update_epochs: int = 4,
    ):
        self.policy = policy
        self.value_net = value_net
        self.clip_epsilon = clip_epsilon
        self.entropy_coeff = entropy_coeff
        self.value_coeff = value_coeff
        self.max_grad_norm = max_grad_norm
        self.num_update_epochs = num_update_epochs

        # Combine parameters from both networks into a single optimizer
        self.optimizer = torch.optim.Adam(
            list(policy.parameters()) + list(value_net.parameters()),
            lr=lr,
        )

    def update(
        self,
        buffer: RolloutBuffer,
        batch_size: int = 512,
    ) -> dict[str, float]:
        """Run PPO update epochs on the collected rollout data.

        For each epoch, generates randomized mini-batches and performs one
        gradient step per batch using the PPO-Clip objective.

        Args:
            buffer: Filled RolloutBuffer with computed returns and advantages.
            batch_size: Mini-batch size for gradient updates.

        Returns:
            Dict of training statistics averaged over all updates:
                "policy_loss": Surrogate policy loss
                "value_loss": Value function MSE loss
                "entropy": Mean policy entropy
                "approx_kl": Approximate KL divergence between old and new policy
        """
        total_policy_loss = 0.0
        total_value_loss = 0.0
        total_entropy = 0.0
        total_approx_kl = 0.0
        num_updates = 0

        for _epoch in range(self.num_update_epochs):
            batches = buffer.get_batches(batch_size)

            for batch in batches:
                obs = batch["obs"]
                actions = batch["actions"]
                old_log_probs = batch["log_probs"]
                returns = batch["returns"]
                advantages = batch["advantages"]

                # Recompute log probs and entropy under the current policy
                latents = batch.get("latents")
                if latents is not None and hasattr(self.policy, "evaluate_actions"):
                    # LLCPolicy: evaluate_actions(z, obs, actions)
                    new_log_probs, entropy = self.policy.evaluate_actions(
                        latents, obs, actions
                    )
                elif hasattr(self.policy, "evaluate_latent"):
                    # HLCPolicy: evaluate_latent(task_obs, z=actions)
                    new_log_probs, entropy = self.policy.evaluate_latent(
                        obs, actions
                    )
                else:
                    # Fallback: LLCPolicy without latents (AMP mode, z=0)
                    z_zeros = torch.zeros(
                        obs.shape[0],
                        self.policy.latent_dim,
                        device=obs.device,
                    )
                    new_log_probs, entropy = self.policy.evaluate_actions(
                        z_zeros, obs, actions
                    )

                # Policy loss: PPO-Clip
                ratio = torch.exp(new_log_probs - old_log_probs)
                surr1 = ratio * advantages
                surr2 = (
                    torch.clamp(ratio, 1.0 - self.clip_epsilon, 1.0 + self.clip_epsilon)
                    * advantages
                )
                policy_loss = -torch.min(surr1, surr2).mean()

                # Value loss: clipped MSE
                new_values = self.value_net(obs).squeeze(-1)
                value_loss = nn.functional.mse_loss(new_values, returns)

                # Entropy bonus
                entropy_mean = entropy.mean()

                # Total loss
                loss = (
                    policy_loss
                    + self.value_coeff * value_loss
                    - self.entropy_coeff * entropy_mean
                )

                # Optimize
                self.optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(
                    list(self.policy.parameters()) + list(self.value_net.parameters()),
                    self.max_grad_norm,
                )
                self.optimizer.step()

                # Approximate KL divergence
                with torch.no_grad():
                    approx_kl = ((ratio - 1.0) - torch.log(ratio)).mean().item()

                total_policy_loss += policy_loss.item()
                total_value_loss += value_loss.item()
                total_entropy += entropy_mean.item()
                total_approx_kl += approx_kl
                num_updates += 1

        if num_updates == 0:
            return {
                "policy_loss": 0.0,
                "value_loss": 0.0,
                "entropy": 0.0,
                "approx_kl": 0.0,
            }

        return {
            "policy_loss": total_policy_loss / num_updates,
            "value_loss": total_value_loss / num_updates,
            "entropy": total_entropy / num_updates,
            "approx_kl": total_approx_kl / num_updates,
        }
