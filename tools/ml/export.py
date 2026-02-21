"""Export MLP policy weights to/from the C++ ml::ModelLoader binary format.

Binary format (little-endian):
    Header:
        uint32  magic   = 0x4D4C5031  ("MLP1")
        uint32  version = 1
        uint32  numLayers
    Per layer:
        uint32  inFeatures
        uint32  outFeatures
        uint32  activationType  (0=None, 1=ReLU, 2=Tanh, 3=ELU)
        float32[outFeatures * inFeatures]  weights (row-major)
        float32[outFeatures]               biases

This matches src/ml/ModelLoader.h loadMLP().
All functions except export_policy() use stdlib only (no numpy/torch).
"""

import math
import random
import struct
from pathlib import Path

MAGIC = 0x4D4C5031  # "MLP1"
VERSION = 1

# Activation types matching ml::Activation enum
ACT_NONE = 0
ACT_RELU = 1
ACT_TANH = 2
ACT_ELU = 3


def export_policy(policy, output_path: str, activation_type: int = ACT_ELU):
    """Export a PyTorch MLPPolicy to C++ binary format.

    Args:
        policy: any nn.Module with a get_layer_params() method yielding
                (weights_ndarray, biases_ndarray, in_dim, out_dim) per layer.
        output_path: path to write the binary file.
        activation_type: activation for hidden layers (output layer uses None).

    Requires: numpy (via PyTorch).
    """
    import numpy as np

    layers = list(policy.get_layer_params())
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(layers)))

        for i, (weights, biases, in_dim, out_dim) in enumerate(layers):
            # Hidden layers use the specified activation, output layer is linear
            act = activation_type if i < len(layers) - 1 else ACT_NONE

            f.write(struct.pack("<III", in_dim, out_dim, act))

            w = weights.astype(np.float32)
            assert w.shape == (out_dim, in_dim), f"Expected ({out_dim}, {in_dim}), got {w.shape}"
            f.write(w.tobytes())

            b = biases.astype(np.float32)
            assert b.shape == (out_dim,), f"Expected ({out_dim},), got {b.shape}"
            f.write(b.tobytes())

    expected_size = 12  # header: magic + version + numLayers
    for _, _, in_dim, out_dim in layers:
        expected_size += 12 + out_dim * in_dim * 4 + out_dim * 4  # 3 uint32 + weights + biases
    actual_size = Path(output_path).stat().st_size
    assert actual_size == expected_size, (
        f"Size mismatch: expected {expected_size}, got {actual_size}"
    )

    print(f"Exported {len(layers)} layers to {output_path} ({actual_size} bytes)")
    for i, (_, _, in_dim, out_dim) in enumerate(layers):
        print(f"  Layer {i}: {in_dim} -> {out_dim}")


def export_random_policy(input_dim: int, output_dim: int, output_path: str,
                         hidden_dim: int = 1024, hidden_layers: int = 3,
                         activation_type: int = ACT_ELU, seed: int = 42):
    """Generate and export Xavier-initialized random weights.

    Stdlib only - no numpy or torch required.
    """
    rng = random.Random(seed)

    def _make_layer(in_dim: int, out_dim: int) -> tuple:
        stddev = math.sqrt(2.0 / (in_dim + out_dim))
        weights = [rng.gauss(0, stddev) for _ in range(out_dim * in_dim)]
        biases = [0.0] * out_dim
        return weights, biases, in_dim, out_dim

    layers = []
    prev_dim = input_dim
    for _ in range(hidden_layers):
        layers.append(_make_layer(prev_dim, hidden_dim))
        prev_dim = hidden_dim
    layers.append(_make_layer(prev_dim, output_dim))

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(layers)))
        for i, (weights, biases, in_dim, out_dim) in enumerate(layers):
            act = activation_type if i < len(layers) - 1 else ACT_NONE
            f.write(struct.pack("<III", in_dim, out_dim, act))
            f.write(struct.pack(f"<{len(weights)}f", *weights))
            f.write(struct.pack(f"<{len(biases)}f", *biases))

    size = Path(output_path).stat().st_size
    print(f"Exported random policy to {output_path} ({size} bytes)")
    print(f"  Architecture: {input_dim} -> " +
          " -> ".join([f"{hidden_dim}" for _ in range(hidden_layers)]) +
          f" -> {output_dim}")


ACT_NAMES = {ACT_NONE: "None", ACT_RELU: "ReLU", ACT_TANH: "Tanh", ACT_ELU: "ELU"}


def verify_weights(path: str):
    """Read back and verify a weight file. Stdlib only."""
    with open(path, "rb") as f:
        magic, version, num_layers = struct.unpack("<III", f.read(12))
        assert magic == MAGIC, f"Bad magic: 0x{magic:08X} (expected 0x{MAGIC:08X})"
        assert version == VERSION, f"Unsupported version: {version}"

        print(f"Weight file: {path}")
        print(f"  Layers: {num_layers}")

        for i in range(num_layers):
            in_dim, out_dim, act_type = struct.unpack("<III", f.read(12))

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

            act_name = ACT_NAMES.get(act_type, f"Unknown({act_type})")
            print(f"  Layer {i}: {in_dim} -> {out_dim} [{act_name}], "
                  f"weights [{min(weights):.4f}, {max(weights):.4f}], "
                  f"biases [{min(biases):.4f}, {max(biases):.4f}]")

        remaining = f.read()
        assert len(remaining) == 0, f"Unexpected {len(remaining)} trailing bytes"

    print("  Verification: OK")
