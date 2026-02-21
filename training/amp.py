"""Adversarial Motion Prior (AMP) discriminator training.

Implements the AMP style reward and discriminator update with WGAN-GP
gradient penalty, following Peng et al. "AMP: Adversarial Motion Priors
for Stylized Physics-Based Character Control" (2021).

The discriminator learns to distinguish reference motion observations from
policy-generated observations. The style reward encourages the policy to
produce motions indistinguishable from the reference dataset.
"""

import sys
from pathlib import Path
from typing import Optional

import numpy as np
import torch
import torch.nn as nn
import torch.autograd as autograd

# Import MotionDataset from tools/
_tools_dir = str(Path(__file__).resolve().parent.parent / "tools")
if _tools_dir not in sys.path:
    sys.path.insert(0, _tools_dir)

from motion_dataset import MotionDataset


class AMPTrainer:
    """AMP discriminator trainer with WGAN-GP.

    Trains a discriminator to classify real (reference motion) vs fake
    (policy rollout) AMP observations, and provides a style reward signal
    for the policy based on the discriminator's score.

    The style reward formula (from AMP paper):
        r_style = clamp(1 - 0.25 * (D(obs) - 1)^2, 0, 1)

    This encourages the policy to produce observations that the discriminator
    scores close to 1 (the real target).

    Args:
        discriminator: AMPDiscriminator network.
        motion_dataset: MotionDataset providing reference motion observations.
        lr: Learning rate for the discriminator optimizer.
        grad_penalty_weight: Weight of the gradient penalty term (lambda in WGAN-GP).
        device: Torch device for computation.
    """

    def __init__(
        self,
        discriminator: nn.Module,
        motion_dataset: MotionDataset,
        lr: float = 1e-4,
        grad_penalty_weight: float = 10.0,
        device: torch.device = torch.device("cpu"),
    ):
        self.discriminator = discriminator
        self.motion_dataset = motion_dataset
        self.grad_penalty_weight = grad_penalty_weight
        self.device = device

        self.optimizer = torch.optim.Adam(discriminator.parameters(), lr=lr)

    def compute_style_reward(self, amp_obs: torch.Tensor) -> torch.Tensor:
        """Compute AMP style reward from discriminator scores.

        Evaluates the discriminator on policy-generated observations and
        converts the raw scores to a bounded reward signal.

        Formula:
            r_style = clamp(1 - 0.25 * (D(obs) - 1)^2, 0, 1)

        This gives reward 1.0 when D(obs) = 1 (perfectly real-looking),
        and decays quadratically as the score moves away from 1.

        Args:
            amp_obs: AMP observation tensor from policy rollout,
                shape (batch, amp_obs_dim).

        Returns:
            Style rewards, shape (batch,), values in [0, 1].
        """
        with torch.no_grad():
            scores = self.discriminator(amp_obs).squeeze(-1)
            reward = torch.clamp(1.0 - 0.25 * (scores - 1.0) ** 2, min=0.0, max=1.0)
        return reward

    def _compute_gradient_penalty(
        self,
        real_obs: torch.Tensor,
        fake_obs: torch.Tensor,
    ) -> torch.Tensor:
        """Compute WGAN-GP gradient penalty.

        Samples random interpolations between real and fake observations
        and penalizes the gradient norm for deviating from 1.

        Args:
            real_obs: Reference motion observations, shape (batch, obs_dim).
            fake_obs: Policy rollout observations, shape (batch, obs_dim).

        Returns:
            Scalar gradient penalty loss.
        """
        batch_size = real_obs.shape[0]

        # Random interpolation coefficient
        alpha = torch.rand(batch_size, 1, device=self.device)
        interpolated = (alpha * real_obs + (1.0 - alpha) * fake_obs).requires_grad_(
            True
        )

        # Discriminator score on interpolated samples
        scores = self.discriminator(interpolated)

        # Compute gradients w.r.t. interpolated input
        gradients = autograd.grad(
            outputs=scores,
            inputs=interpolated,
            grad_outputs=torch.ones_like(scores),
            create_graph=True,
            retain_graph=True,
        )[0]

        # Gradient penalty: (||grad|| - 1)^2
        gradients = gradients.view(batch_size, -1)
        gradient_norm = gradients.norm(2, dim=1)
        penalty = ((gradient_norm - 1.0) ** 2).mean()

        return penalty

    def update(
        self,
        policy_obs: torch.Tensor,
        reference_obs: Optional[torch.Tensor] = None,
        reference_batch_size: Optional[int] = None,
    ) -> dict[str, float]:
        """Train the discriminator on real and fake observations.

        Uses the WGAN-GP objective:
            L_D = E[D(fake)] - E[D(real)] + lambda * GP
        where GP is the gradient penalty.

        The discriminator is trained to score real observations high and
        fake observations low. The gradient penalty stabilizes training.

        Args:
            policy_obs: AMP observations from policy rollout,
                shape (batch, obs_dim). These are the "fake" samples.
            reference_obs: Reference motion observations. If None, samples
                from the motion_dataset. Shape (batch, obs_dim).
            reference_batch_size: Number of reference samples to draw if
                reference_obs is not provided. Defaults to policy_obs batch size.

        Returns:
            Dict of training statistics:
                "disc_loss": Total discriminator loss
                "disc_real_score": Mean discriminator score on real data
                "disc_fake_score": Mean discriminator score on fake data
                "gradient_penalty": Gradient penalty value
        """
        batch_size = policy_obs.shape[0]

        # Get reference observations
        if reference_obs is None:
            ref_batch_size = reference_batch_size or batch_size
            ref_np = self.motion_dataset.sample_amp_obs(ref_batch_size)
            reference_obs = torch.from_numpy(ref_np).to(self.device)

        # Ensure same batch size for gradient penalty
        min_batch = min(policy_obs.shape[0], reference_obs.shape[0])
        fake_obs = policy_obs[:min_batch]
        real_obs = reference_obs[:min_batch]

        # Forward pass
        real_scores = self.discriminator(real_obs)
        fake_scores = self.discriminator(fake_obs.detach())

        # WGAN loss: maximize real scores, minimize fake scores
        # Equivalent to minimizing: E[D(fake)] - E[D(real)]
        disc_loss = fake_scores.mean() - real_scores.mean()

        # Gradient penalty
        gp = self._compute_gradient_penalty(real_obs, fake_obs.detach())

        # Total loss
        total_loss = disc_loss + self.grad_penalty_weight * gp

        # Optimize
        self.optimizer.zero_grad()
        total_loss.backward()
        self.optimizer.step()

        return {
            "disc_loss": disc_loss.item(),
            "disc_real_score": real_scores.mean().item(),
            "disc_fake_score": fake_scores.mean().item(),
            "gradient_penalty": gp.item(),
        }
