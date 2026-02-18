#!/usr/bin/env python3
"""Export trained PyTorch policy weights to the C++ MLPPolicy binary format.

Binary format (little-endian):
    Header:
        uint32  magic = 0x4D4C5001  ("MLP\x01")
        uint32  numLayers
    Per layer:
        uint32  inputDim
        uint32  outputDim
        float32[outputDim * inputDim]  weights (row-major)
        float32[outputDim]             biases

This exactly matches src/unicon/MLPPolicy.h loadWeights().

Usage:
    # From a checkpoint:
    python -m tools.unicon_training.export --checkpoint generated/unicon/checkpoint_001000.pt --output generated/unicon/policy_weights.bin

    # Generate random weights for testing (no dependencies beyond stdlib):
    python -m tools.unicon_training.export --random --output generated/unicon/policy_weights.bin
"""

import argparse
import math
import random
import struct
import sys
from pathlib import Path

MAGIC = 0x4D4C5001  # "MLP\x01"


def export_policy(policy, output_path: str):
    """Export a PyTorch MLPPolicy to C++ binary format.

    Args:
        policy: MLPPolicy instance (from policy.py)
        output_path: path to write the binary file

    Requires: numpy (via PyTorch dependency)
    """
    import numpy as np

    layers = list(policy.get_layer_params())

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        # Header
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<I", len(layers)))

        for weights, biases, in_dim, out_dim in layers:
            # Layer header
            f.write(struct.pack("<I", in_dim))
            f.write(struct.pack("<I", out_dim))

            # Weights: PyTorch Linear stores [out_features, in_features] which is
            # already row-major with rows = output neurons — matches C++ layout
            w = weights.astype(np.float32)
            assert w.shape == (out_dim, in_dim), f"Expected ({out_dim}, {in_dim}), got {w.shape}"
            f.write(w.tobytes())

            # Biases
            b = biases.astype(np.float32)
            assert b.shape == (out_dim,), f"Expected ({out_dim},), got {b.shape}"
            f.write(b.tobytes())

    # Verify file size
    expected_size = 8  # header
    for _, _, in_dim, out_dim in layers:
        expected_size += 8 + out_dim * in_dim * 4 + out_dim * 4
    actual_size = Path(output_path).stat().st_size
    assert actual_size == expected_size, (
        f"Size mismatch: expected {expected_size}, got {actual_size}"
    )

    print(f"Exported {len(layers)} layers to {output_path} ({actual_size} bytes)")
    for i, (_, _, in_dim, out_dim) in enumerate(layers):
        print(f"  Layer {i}: {in_dim} -> {out_dim}")


def export_random_policy(obs_dim: int, act_dim: int, output_path: str,
                         hidden_dim: int = 1024, hidden_layers: int = 3,
                         seed: int = 42):
    """Generate and export a random policy (for testing C++ loading).

    Uses Xavier initialization matching C++ MLPPolicy::initRandom().
    Only uses stdlib — no numpy or torch required.
    """
    rng = random.Random(seed)

    def _make_layer(in_dim: int, out_dim: int) -> tuple:
        # Xavier initialization: stddev = sqrt(2 / (in + out))
        stddev = math.sqrt(2.0 / (in_dim + out_dim))
        weights = [rng.gauss(0, stddev) for _ in range(out_dim * in_dim)]
        biases = [0.0] * out_dim
        return weights, biases, in_dim, out_dim

    layers = []
    prev_dim = obs_dim
    for _ in range(hidden_layers):
        layers.append(_make_layer(prev_dim, hidden_dim))
        prev_dim = hidden_dim
    layers.append(_make_layer(prev_dim, act_dim))

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<I", len(layers)))
        for weights, biases, in_dim, out_dim in layers:
            f.write(struct.pack("<I", in_dim))
            f.write(struct.pack("<I", out_dim))
            f.write(struct.pack(f"<{len(weights)}f", *weights))
            f.write(struct.pack(f"<{len(biases)}f", *biases))

    size = Path(output_path).stat().st_size
    print(f"Exported random policy to {output_path} ({size} bytes)")
    print(f"  Architecture: {obs_dim} -> " +
          " -> ".join([f"{hidden_dim}" for _ in range(hidden_layers)]) +
          f" -> {act_dim}")


def verify_weights(path: str):
    """Read back and verify a weight file matches the expected format.
    Only uses stdlib."""
    with open(path, "rb") as f:
        magic, = struct.unpack("<I", f.read(4))
        assert magic == MAGIC, f"Bad magic: 0x{magic:08X} (expected 0x{MAGIC:08X})"

        num_layers, = struct.unpack("<I", f.read(4))
        print(f"Weight file: {path}")
        print(f"  Layers: {num_layers}")

        for i in range(num_layers):
            in_dim, = struct.unpack("<I", f.read(4))
            out_dim, = struct.unpack("<I", f.read(4))

            weight_count = out_dim * in_dim
            weight_bytes = f.read(weight_count * 4)
            bias_bytes = f.read(out_dim * 4)

            assert len(weight_bytes) == weight_count * 4, (
                f"Layer {i}: expected {weight_count * 4} weight bytes, got {len(weight_bytes)}"
            )
            assert len(bias_bytes) == out_dim * 4, (
                f"Layer {i}: expected {out_dim * 4} bias bytes, got {len(bias_bytes)}"
            )

            weights = struct.unpack(f"<{weight_count}f", weight_bytes)
            biases = struct.unpack(f"<{out_dim}f", bias_bytes)

            w_min = min(weights)
            w_max = max(weights)
            b_min = min(biases)
            b_max = max(biases)

            print(f"  Layer {i}: {in_dim} -> {out_dim}, "
                  f"weights range [{w_min:.4f}, {w_max:.4f}], "
                  f"biases range [{b_min:.4f}, {b_max:.4f}]")

        remaining = f.read()
        assert len(remaining) == 0, f"Unexpected {len(remaining)} trailing bytes"

    print("  Verification: OK")


def main():
    parser = argparse.ArgumentParser(description="Export UniCon policy weights for C++")
    parser.add_argument("--checkpoint", type=str, default=None,
                        help="Path to training checkpoint (.pt)")
    parser.add_argument("--random", action="store_true",
                        help="Generate random weights (for testing, no deps)")
    parser.add_argument("--obs-dim", type=int, default=429,
                        help="Observation dimension (default: 20 joints, tau=1 = 429)")
    parser.add_argument("--act-dim", type=int, default=60,
                        help="Action dimension (default: 20 joints * 3)")
    parser.add_argument("--hidden-dim", type=int, default=1024,
                        help="Hidden layer dimension")
    parser.add_argument("--hidden-layers", type=int, default=3,
                        help="Number of hidden layers")
    parser.add_argument("--output", type=str, default="generated/unicon/policy_weights.bin",
                        help="Output binary file path")
    parser.add_argument("--verify", action="store_true",
                        help="Verify the output file after writing")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for random weight generation")
    args = parser.parse_args()

    if args.random:
        export_random_policy(
            args.obs_dim, args.act_dim, args.output,
            args.hidden_dim, args.hidden_layers, args.seed,
        )
    elif args.checkpoint:
        import torch
        from .config import PolicyConfig
        from .policy import MLPPolicy as TorchPolicy

        checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
        config = PolicyConfig(hidden_dim=args.hidden_dim, hidden_layers=args.hidden_layers)
        policy = TorchPolicy(args.obs_dim, args.act_dim, config)
        policy.load_state_dict(checkpoint["policy_state_dict"])
        export_policy(policy, args.output)
    else:
        parser.error("Must specify either --checkpoint or --random")

    if args.verify:
        verify_weights(args.output)


if __name__ == "__main__":
    main()
