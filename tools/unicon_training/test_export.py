#!/usr/bin/env python3
"""Round-trip verification: export PyTorch weights, read back, compare.

This validates that the binary export format matches what C++ MLPPolicy expects.

Usage:
    python -m tools.unicon_training.test_export
"""

import struct
import tempfile
import numpy as np
import torch

from .config import PolicyConfig
from .policy import MLPPolicy
from .export import MAGIC, export_policy, verify_weights


def test_export_round_trip():
    """Export a policy, read back the binary, compare layer by layer."""
    obs_dim = 217  # 20 joints, tau=1: (1+1)*(11+200) + 1*7 = 429... let me compute
    # Actually: (1 + tau) * (11 + 10*J) + tau * 7
    # = (1 + 1) * (11 + 10*20) + 1 * 7 = 2 * 211 + 7 = 429
    obs_dim = 429
    act_dim = 60   # 20 joints * 3

    config = PolicyConfig(hidden_dim=64, hidden_layers=2)  # Small for testing
    policy = MLPPolicy(obs_dim, act_dim, config)

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        path = f.name

    # Export
    export_policy(policy, path)

    # Read back and compare
    with open(path, "rb") as f:
        magic, = struct.unpack("<I", f.read(4))
        assert magic == MAGIC, f"Magic mismatch: 0x{magic:08X}"

        num_layers, = struct.unpack("<I", f.read(4))
        expected_layers = list(policy.get_layer_params())
        assert num_layers == len(expected_layers), (
            f"Layer count mismatch: {num_layers} vs {len(expected_layers)}"
        )

        for i, (expected_w, expected_b, exp_in, exp_out) in enumerate(expected_layers):
            in_dim, = struct.unpack("<I", f.read(4))
            out_dim, = struct.unpack("<I", f.read(4))

            assert in_dim == exp_in, f"Layer {i}: in_dim {in_dim} vs {exp_in}"
            assert out_dim == exp_out, f"Layer {i}: out_dim {out_dim} vs {exp_out}"

            weights = np.frombuffer(f.read(out_dim * in_dim * 4), dtype=np.float32)
            weights = weights.reshape(out_dim, in_dim)
            biases = np.frombuffer(f.read(out_dim * 4), dtype=np.float32)

            np.testing.assert_array_almost_equal(
                weights, expected_w, decimal=6,
                err_msg=f"Layer {i} weights mismatch"
            )
            np.testing.assert_array_almost_equal(
                biases, expected_b, decimal=6,
                err_msg=f"Layer {i} biases mismatch"
            )

        remaining = f.read()
        assert len(remaining) == 0, f"Trailing bytes: {len(remaining)}"

    print("Round-trip test PASSED")

    # Also verify using the verify function
    verify_weights(path)

    import os
    os.unlink(path)


def test_forward_pass_matches():
    """Verify that a PyTorch forward pass produces the same output
    as would be expected from the C++ matmul + ELU implementation."""
    obs_dim = 429
    act_dim = 60

    config = PolicyConfig(hidden_dim=64, hidden_layers=2)
    policy = MLPPolicy(obs_dim, act_dim, config)
    policy.eval()

    # Fixed input
    torch.manual_seed(123)
    obs = torch.randn(1, obs_dim)

    # PyTorch forward pass
    with torch.no_grad():
        pytorch_output = policy(obs).numpy().flatten()

    # Manual forward pass using raw weights (simulating C++)
    layers = list(policy.get_layer_params())
    x = obs.numpy().flatten()

    for i, (w, b, in_dim, out_dim) in enumerate(layers):
        # matmul: out = w @ x + b
        x = w @ x + b
        # ELU on hidden layers only
        if i < len(layers) - 1:
            x = np.where(x > 0, x, np.exp(x) - 1.0)

    np.testing.assert_array_almost_equal(
        x, pytorch_output, decimal=4,
        err_msg="Forward pass mismatch between PyTorch and manual (C++ equivalent)"
    )
    print("Forward pass equivalence test PASSED")
    print(f"  Output sample: [{', '.join(f'{v:.4f}' for v in pytorch_output[:5])}...]")


def test_state_encoder_dims():
    """Verify observation dimension calculations match C++."""
    from .state_encoder import StateEncoder

    # Standard UniCon: 20 joints, tau=1
    enc = StateEncoder(20, 1)
    # (1 + 1) * (11 + 10*20) + 1 * 7 = 2 * 211 + 7 = 429
    assert enc.observation_dim == 429, f"Expected 429, got {enc.observation_dim}"

    # 20 joints, tau=3
    enc3 = StateEncoder(20, 3)
    # (1 + 3) * 211 + 3 * 7 = 844 + 21 = 865
    assert enc3.observation_dim == 865, f"Expected 865, got {enc3.observation_dim}"

    print("State encoder dimension test PASSED")
    print(f"  20 joints, tau=1: obs_dim={enc.observation_dim}")
    print(f"  20 joints, tau=3: obs_dim={enc3.observation_dim}")


if __name__ == "__main__":
    test_state_encoder_dims()
    test_export_round_trip()
    test_forward_pass_matches()
    print("\nAll tests PASSED")
