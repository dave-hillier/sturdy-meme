"""PyTorch MLP policy matching the C++ MLPPolicy architecture.

Network: 3 hidden layers x 1024 units, ELU activation on hidden, linear output.
The weight format must exactly match the C++ binary format for export.
"""

import torch
import torch.nn as nn
import numpy as np
from typing import Optional

from .config import PolicyConfig, HumanoidConfig


class MLPPolicy(nn.Module):
    """MLP policy network matching C++ MLPPolicy architecture.

    Architecture:
        input -> Linear(input_dim, 1024) -> ELU ->
                 Linear(1024, 1024) -> ELU ->
                 Linear(1024, 1024) -> ELU ->
                 Linear(1024, output_dim)

    Output is raw (unnormalized) torques. A separate log_std parameter
    vector is used for the stochastic policy during training.
    """

    def __init__(self, input_dim: int, output_dim: int,
                 config: Optional[PolicyConfig] = None):
        super().__init__()
        if config is None:
            config = PolicyConfig()

        self.input_dim = input_dim
        self.output_dim = output_dim

        layers = []
        prev_dim = input_dim
        for _ in range(config.hidden_layers):
            layers.append(nn.Linear(prev_dim, config.hidden_dim))
            layers.append(nn.ELU())
            prev_dim = config.hidden_dim
        layers.append(nn.Linear(prev_dim, output_dim))

        self.network = nn.Sequential(*layers)

        # Stochastic policy: learnable per-action log standard deviation
        self.log_std = nn.Parameter(torch.full((output_dim,), -1.0))

        self._init_weights()

    def _init_weights(self):
        """Xavier initialization matching C++ initRandom."""
        for m in self.network:
            if isinstance(m, nn.Linear):
                nn.init.xavier_uniform_(m.weight)
                nn.init.zeros_(m.bias)

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        """Deterministic forward pass (mean action)."""
        return self.network(obs)

    def get_distribution(self, obs: torch.Tensor):
        """Return action distribution for PPO sampling."""
        mean = self.forward(obs)
        std = self.log_std.exp().expand_as(mean)
        return torch.distributions.Normal(mean, std)

    def evaluate_actions(self, obs: torch.Tensor, actions: torch.Tensor):
        """Compute log_prob and entropy for given state-action pairs."""
        dist = self.get_distribution(obs)
        log_prob = dist.log_prob(actions).sum(dim=-1)
        entropy = dist.entropy().sum(dim=-1)
        return log_prob, entropy

    def get_layer_params(self):
        """Yield (weight, bias, input_dim, output_dim) for each linear layer.
        This matches the C++ MLPPolicy layer ordering for export."""
        for module in self.network:
            if isinstance(module, nn.Linear):
                yield (
                    module.weight.detach().cpu().numpy(),  # [out, in] row-major
                    module.bias.detach().cpu().numpy(),     # [out]
                    module.in_features,
                    module.out_features,
                )


class ValueNetwork(nn.Module):
    """Separate value function for PPO.
    Same architecture as the policy but outputs a single scalar."""

    def __init__(self, input_dim: int, config: Optional[PolicyConfig] = None):
        super().__init__()
        if config is None:
            config = PolicyConfig()

        layers = []
        prev_dim = input_dim
        for _ in range(config.hidden_layers):
            layers.append(nn.Linear(prev_dim, config.hidden_dim))
            layers.append(nn.ELU())
            prev_dim = config.hidden_dim
        layers.append(nn.Linear(prev_dim, 1))

        self.network = nn.Sequential(*layers)
        self._init_weights()

    def _init_weights(self):
        for m in self.network:
            if isinstance(m, nn.Linear):
                nn.init.xavier_uniform_(m.weight)
                nn.init.zeros_(m.bias)

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        return self.network(obs).squeeze(-1)
