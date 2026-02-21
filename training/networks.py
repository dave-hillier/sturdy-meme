"""Neural network architectures for CALM/AMP training.

All architectures match the C++ inference engine (src/ml/) so that trained
weights can be exported to .bin files and loaded at runtime for GPU inference.

C++ architecture reference (src/ml/calm/LowLevelController.h):
  LLC forward pass:
    1. styleEmbed = tanh(styleMLP(z))
    2. combined   = concat(styleEmbed, obs)
    3. hidden     = relu(mainMLP(combined))
    4. actions    = muHead(hidden)           (linear, no activation)

Default dimensions (from CharacterConfig + compute shader specialization constants):
  - latent_dim       = 64   (LatentSpace::DEFAULT_LATENT_DIM)
  - obs_dim          = 102  (calm_inference.comp OBS_DIM specialization constant)
  - action_dim       = 37   (calm_inference.comp ACTION_DIM specialization constant)
  - style_embed_dim  = 64
  - encoder_obs_steps = 10  (CharacterConfig::numEncoderObsSteps)
"""

import torch
import torch.nn as nn
import torch.nn.functional as F


# ---------------------------------------------------------------------------
# Default dimensions matching C++ inference engine
# ---------------------------------------------------------------------------
DEFAULT_LATENT_DIM = 64
DEFAULT_OBS_DIM = 102
DEFAULT_ACTION_DIM = 37
DEFAULT_STYLE_EMBED_DIM = 64
DEFAULT_HIDDEN_DIM = 256
DEFAULT_ENCODER_OBS_STEPS = 10


# ---------------------------------------------------------------------------
# StyleMLP
# ---------------------------------------------------------------------------
class StyleMLP(nn.Module):
    """Style embedding network: maps latent code z to a style embedding.

    Architecture:
        latent_dim (64) -> 256 -> 128 -> style_embed_dim (64)
        Activation: Tanh on all layers (matches C++ activation=2 for style MLP)

    Matches C++ MLPNetwork used in StyleConditionedNetwork::styleMLP_.
    The C++ compute shader (calm_inference.comp) applies Tanh activation
    to all style MLP layers.
    """

    def __init__(
        self,
        latent_dim: int = DEFAULT_LATENT_DIM,
        style_embed_dim: int = DEFAULT_STYLE_EMBED_DIM,
    ):
        super().__init__()
        self.latent_dim = latent_dim
        self.style_embed_dim = style_embed_dim

        self.net = nn.Sequential(
            nn.Linear(latent_dim, 256),
            nn.Tanh(),
            nn.Linear(256, 128),
            nn.Tanh(),
            nn.Linear(128, style_embed_dim),
            nn.Tanh(),
        )

    def forward(self, z: torch.Tensor) -> torch.Tensor:
        """Map latent code to style embedding.

        Args:
            z: Latent code tensor, shape (batch, latent_dim).

        Returns:
            Style embedding tensor, shape (batch, style_embed_dim).
        """
        return self.net(z)


# ---------------------------------------------------------------------------
# MainMLP
# ---------------------------------------------------------------------------
class MainMLP(nn.Module):
    """Main policy body: processes concatenated style embedding and observation.

    Architecture:
        (style_embed_dim + obs_dim) -> 1024 -> 512 -> hidden_dim (256)
        Activation: ReLU (matches C++ activation=1 for main MLP)

    Matches C++ MLPNetwork used in StyleConditionedNetwork::mainMLP_.
    """

    def __init__(
        self,
        style_embed_dim: int = DEFAULT_STYLE_EMBED_DIM,
        obs_dim: int = DEFAULT_OBS_DIM,
        hidden_dim: int = DEFAULT_HIDDEN_DIM,
    ):
        super().__init__()
        self.input_dim = style_embed_dim + obs_dim
        self.hidden_dim = hidden_dim

        self.net = nn.Sequential(
            nn.Linear(self.input_dim, 1024),
            nn.ReLU(),
            nn.Linear(1024, 512),
            nn.ReLU(),
            nn.Linear(512, hidden_dim),
            nn.ReLU(),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Forward pass through main MLP body.

        Args:
            x: Concatenated [style_embed, obs], shape (batch, style_embed_dim + obs_dim).

        Returns:
            Hidden representation, shape (batch, hidden_dim).
        """
        return self.net(x)


# ---------------------------------------------------------------------------
# MuHead
# ---------------------------------------------------------------------------
class MuHead(nn.Module):
    """Action mean head: linear projection from hidden to action space.

    Architecture:
        hidden_dim (256) -> action_dim (37)
        Activation: None (linear output)

    Matches C++ MLPNetwork used in LowLevelController::muHead_.
    The C++ compute shader applies activation=0 (None) for the final layer.
    """

    def __init__(
        self,
        hidden_dim: int = DEFAULT_HIDDEN_DIM,
        action_dim: int = DEFAULT_ACTION_DIM,
    ):
        super().__init__()
        self.linear = nn.Linear(hidden_dim, action_dim)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Project hidden representation to action means.

        Args:
            x: Hidden state, shape (batch, hidden_dim).

        Returns:
            Action means, shape (batch, action_dim).
        """
        return self.linear(x)


# ---------------------------------------------------------------------------
# LLCPolicy (Low-Level Controller)
# ---------------------------------------------------------------------------
class LLCPolicy(nn.Module):
    """CALM Low-Level Controller policy.

    Combines StyleMLP + MainMLP + MuHead into the full LLC forward pass.
    Also maintains a learnable log_std parameter for stochastic action sampling
    during training.

    Architecture (matches C++ LowLevelController + calm_inference.comp):
        z -> StyleMLP -> style_embed
        concat(style_embed, obs) -> MainMLP -> hidden
        hidden -> MuHead -> action_means
        action_std = exp(log_std)   (learnable, shared across batch)

    Args:
        latent_dim: Dimension of latent code z (default 64).
        obs_dim: Dimension of per-frame observation (default 102).
        action_dim: Number of action DOFs (default 37).
        style_embed_dim: Dimension of style embedding (default 64).
        hidden_dim: Output dimension of MainMLP (default 256).
        init_log_std: Initial value for log_std parameter.
    """

    def __init__(
        self,
        latent_dim: int = DEFAULT_LATENT_DIM,
        obs_dim: int = DEFAULT_OBS_DIM,
        action_dim: int = DEFAULT_ACTION_DIM,
        style_embed_dim: int = DEFAULT_STYLE_EMBED_DIM,
        hidden_dim: int = DEFAULT_HIDDEN_DIM,
        init_log_std: float = -1.0,
    ):
        super().__init__()
        self.latent_dim = latent_dim
        self.obs_dim = obs_dim
        self.action_dim = action_dim

        self.style_mlp = StyleMLP(latent_dim, style_embed_dim)
        self.main_mlp = MainMLP(style_embed_dim, obs_dim, hidden_dim)
        self.mu_head = MuHead(hidden_dim, action_dim)

        # Learnable log standard deviation (diagonal Gaussian)
        self.log_std = nn.Parameter(torch.full((action_dim,), init_log_std))

    def forward(
        self, z: torch.Tensor, obs: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Compute action distribution parameters.

        Args:
            z: Latent code, shape (batch, latent_dim).
            obs: Observation, shape (batch, obs_dim).

        Returns:
            Tuple of (action_mean, action_log_std):
                action_mean: shape (batch, action_dim)
                action_log_std: shape (action_dim,) broadcast over batch
        """
        style_embed = self.style_mlp(z)
        combined = torch.cat([style_embed, obs], dim=-1)
        hidden = self.main_mlp(combined)
        mu = self.mu_head(hidden)
        return mu, self.log_std

    def get_actions(
        self, z: torch.Tensor, obs: torch.Tensor, deterministic: bool = False
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Sample actions and compute log probabilities.

        Args:
            z: Latent code, shape (batch, latent_dim).
            obs: Observation, shape (batch, obs_dim).
            deterministic: If True, return the mean action without noise.

        Returns:
            Tuple of (actions, log_probs):
                actions: shape (batch, action_dim)
                log_probs: shape (batch,)
        """
        mu, log_std = self.forward(z, obs)
        std = torch.exp(log_std)

        if deterministic:
            actions = mu
        else:
            noise = torch.randn_like(mu)
            actions = mu + std * noise

        # Log probability of a diagonal Gaussian
        log_probs = -0.5 * (
            ((actions - mu) / (std + 1e-8)) ** 2
            + 2.0 * log_std
            + torch.log(torch.tensor(2.0 * torch.pi))
        )
        log_probs = log_probs.sum(dim=-1)

        return actions, log_probs

    def evaluate_actions(
        self, z: torch.Tensor, obs: torch.Tensor, actions: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Evaluate log probability and entropy for given actions.

        Used during PPO update to recompute log_probs under the current policy.

        Args:
            z: Latent code, shape (batch, latent_dim).
            obs: Observation, shape (batch, obs_dim).
            actions: Previously sampled actions, shape (batch, action_dim).

        Returns:
            Tuple of (log_probs, entropy):
                log_probs: shape (batch,)
                entropy: shape (batch,)
        """
        mu, log_std = self.forward(z, obs)
        std = torch.exp(log_std)

        log_probs = -0.5 * (
            ((actions - mu) / (std + 1e-8)) ** 2
            + 2.0 * log_std
            + torch.log(torch.tensor(2.0 * torch.pi))
        )
        log_probs = log_probs.sum(dim=-1)

        # Entropy of diagonal Gaussian
        entropy = 0.5 * (1.0 + torch.log(2.0 * torch.pi * std ** 2)).sum(dim=-1)

        return log_probs, entropy


# ---------------------------------------------------------------------------
# ValueNetwork
# ---------------------------------------------------------------------------
class ValueNetwork(nn.Module):
    """State value function V(obs).

    Architecture:
        obs_dim -> 1024 -> 512 -> 256 -> 1
        Activation: ReLU (hidden layers), None (output)

    Used by PPO to estimate the expected return from a state.
    """

    def __init__(self, obs_dim: int = DEFAULT_OBS_DIM):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(obs_dim, 1024),
            nn.ReLU(),
            nn.Linear(1024, 512),
            nn.ReLU(),
            nn.Linear(512, 256),
            nn.ReLU(),
            nn.Linear(256, 1),
        )

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        """Estimate state value.

        Args:
            obs: Observation tensor, shape (batch, obs_dim).

        Returns:
            Value estimates, shape (batch, 1).
        """
        return self.net(obs)


# ---------------------------------------------------------------------------
# AMPDiscriminator
# ---------------------------------------------------------------------------
class AMPDiscriminator(nn.Module):
    """Adversarial Motion Prior discriminator.

    Classifies whether an observation comes from the reference motion dataset
    (real) or from the policy rollout (fake).

    Architecture:
        amp_obs_dim -> 1024 -> 512 -> 1
        Activation: ReLU (hidden layers), None (output)

    The output is a scalar score (not passed through sigmoid) because WGAN-GP
    training uses raw discriminator scores rather than probabilities.
    """

    def __init__(self, amp_obs_dim: int = DEFAULT_OBS_DIM):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(amp_obs_dim, 1024),
            nn.ReLU(),
            nn.Linear(1024, 512),
            nn.ReLU(),
            nn.Linear(512, 1),
        )

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        """Score an AMP observation.

        Args:
            obs: AMP observation tensor, shape (batch, amp_obs_dim).

        Returns:
            Discriminator scores, shape (batch, 1).
        """
        return self.net(obs)


# ---------------------------------------------------------------------------
# MotionEncoder
# ---------------------------------------------------------------------------
class MotionEncoder(nn.Module):
    """CALM motion encoder: maps a temporal window of observations to a latent code.

    Architecture:
        (encoder_obs_steps * obs_dim) -> 1024 -> 512 -> latent_dim (64)
        Activation: ReLU (hidden layers)
        Output: L2-normalized latent vector

    Matches C++ LatentSpace encoder (src/ml/LatentSpace.h).
    CharacterConfig::numEncoderObsSteps = 10 frames of stacked observations.
    """

    def __init__(
        self,
        obs_dim: int = DEFAULT_OBS_DIM,
        latent_dim: int = DEFAULT_LATENT_DIM,
        encoder_obs_steps: int = DEFAULT_ENCODER_OBS_STEPS,
    ):
        super().__init__()
        self.obs_dim = obs_dim
        self.latent_dim = latent_dim
        self.encoder_obs_steps = encoder_obs_steps
        self.input_dim = encoder_obs_steps * obs_dim

        self.net = nn.Sequential(
            nn.Linear(self.input_dim, 1024),
            nn.ReLU(),
            nn.Linear(1024, 512),
            nn.ReLU(),
            nn.Linear(512, latent_dim),
        )

    def forward(self, stacked_obs: torch.Tensor) -> torch.Tensor:
        """Encode a temporal observation window into a latent code.

        Args:
            stacked_obs: Flattened temporal observations, shape
                (batch, encoder_obs_steps * obs_dim).

        Returns:
            L2-normalized latent code, shape (batch, latent_dim).
        """
        raw = self.net(stacked_obs)
        return F.normalize(raw, p=2, dim=-1)


# ---------------------------------------------------------------------------
# HLCPolicy (High-Level Controller)
# ---------------------------------------------------------------------------
class HLCPolicy(nn.Module):
    """High-Level Controller policy: maps task observations to latent codes.

    Produces an L2-normalized latent code that commands the frozen LLC.
    Different tasks (heading, location, strike) have different task_obs_dim:
      - heading:  3  (local_target_dir_x, local_target_dir_z, target_speed)
      - location: 3  (local_offset_x, local_offset_y, local_offset_z)
      - strike:   4  (local_target_x, local_target_y, local_target_z, distance)

    Architecture:
        task_obs_dim -> 512 -> 256 -> latent_dim (64)
        Output: L2-normalized
        Has learnable log_std for stochastic latent sampling during training.

    Matches C++ TaskController (src/ml/TaskController.h).
    """

    def __init__(
        self,
        task_obs_dim: int = 3,
        latent_dim: int = DEFAULT_LATENT_DIM,
        init_log_std: float = -2.0,
    ):
        super().__init__()
        self.task_obs_dim = task_obs_dim
        self.latent_dim = latent_dim

        self.net = nn.Sequential(
            nn.Linear(task_obs_dim, 512),
            nn.ReLU(),
            nn.Linear(512, 256),
            nn.ReLU(),
            nn.Linear(256, latent_dim),
        )

        # Learnable log standard deviation for exploration
        self.log_std = nn.Parameter(torch.full((latent_dim,), init_log_std))

    def forward(self, task_obs: torch.Tensor) -> torch.Tensor:
        """Compute L2-normalized latent mean from task observation.

        Args:
            task_obs: Task-specific observation, shape (batch, task_obs_dim).

        Returns:
            L2-normalized latent mean, shape (batch, latent_dim).
        """
        raw = self.net(task_obs)
        return F.normalize(raw, p=2, dim=-1)

    def get_latent(
        self, task_obs: torch.Tensor, deterministic: bool = False
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Sample a latent code and compute log probability.

        Args:
            task_obs: Task-specific observation, shape (batch, task_obs_dim).
            deterministic: If True, return the normalized mean without noise.

        Returns:
            Tuple of (latent, log_prob):
                latent: L2-normalized latent code, shape (batch, latent_dim)
                log_prob: shape (batch,)
        """
        mu = self.forward(task_obs)
        std = torch.exp(self.log_std)

        if deterministic:
            return mu, torch.zeros(task_obs.shape[0], device=task_obs.device)

        noise = torch.randn_like(mu)
        z_raw = mu + std * noise
        z = F.normalize(z_raw, p=2, dim=-1)

        # Log probability under the Gaussian (before normalization)
        log_prob = -0.5 * (
            ((z_raw - mu) / (std + 1e-8)) ** 2
            + 2.0 * self.log_std
            + torch.log(torch.tensor(2.0 * torch.pi))
        )
        log_prob = log_prob.sum(dim=-1)

        return z, log_prob

    def evaluate_latent(
        self, task_obs: torch.Tensor, z: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Evaluate log probability and entropy for a given latent code.

        Args:
            task_obs: Task-specific observation, shape (batch, task_obs_dim).
            z: Previously sampled latent code, shape (batch, latent_dim).

        Returns:
            Tuple of (log_prob, entropy):
                log_prob: shape (batch,)
                entropy: shape (batch,)
        """
        mu = self.forward(task_obs)
        std = torch.exp(self.log_std)

        log_prob = -0.5 * (
            ((z - mu) / (std + 1e-8)) ** 2
            + 2.0 * self.log_std
            + torch.log(torch.tensor(2.0 * torch.pi))
        )
        log_prob = log_prob.sum(dim=-1)

        entropy = 0.5 * (1.0 + torch.log(2.0 * torch.pi * std ** 2)).sum(dim=-1)

        return log_prob, entropy
