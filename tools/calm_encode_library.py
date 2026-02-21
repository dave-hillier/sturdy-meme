#!/usr/bin/env python3
"""Pre-encode motion clips into a CALM latent library JSON file.

Takes motion clip files (.npy arrays of stacked observations) and an encoder
network (.bin in MLP1 format or a PyTorch checkpoint), encodes each clip into
a 64D latent vector, and writes a JSON library file for the C++ engine.

The latent library maps clip names to L2-normalized latent vectors with
semantic tags, matching the CALMLatentSpace::EncodedBehavior format.

Output JSON format:
{
  "latent_dim": 64,
  "behaviors": [
    {
      "clip": "walk_forward.npy",
      "tags": ["walk", "locomotion"],
      "latent": [0.12, -0.03, ...]
    },
    ...
  ]
}

Usage:
    # Encode clips using a PyTorch encoder checkpoint
    python tools/calm_encode_library.py \\
        --encoder-checkpoint models/calm_encoder.pth \\
        --clips data/motion_clips/ \\
        --tags-file data/clip_tags.json \\
        --output data/calm/latent_library.json

    # Encode clips using an already-exported encoder .bin
    python tools/calm_encode_library.py \\
        --encoder-bin data/calm/models/encoder.bin \\
        --clips data/motion_clips/ \\
        --output data/calm/latent_library.json

    # Create a dummy library for testing
    python tools/calm_encode_library.py \\
        --dummy --output data/calm/latent_library.json
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:
    np = None

try:
    import torch
except ImportError:
    torch = None

# MLP1 format constants (must match C++ ModelLoader)
MAGIC = 0x4D4C5031
VERSION = 1
ACTIVATION_NONE = 0
ACTIVATION_RELU = 1
ACTIVATION_TANH = 2


def load_mlp_bin(path: Path) -> list[dict]:
    """Load an MLP from the engine's MLP1 binary format.

    Returns list of dicts with 'weight', 'bias', 'activation' (numpy arrays).
    """
    if np is None:
        print("Error: numpy is required. Install with: pip install numpy", file=sys.stderr)
        sys.exit(1)

    with open(path, "rb") as f:
        magic, version, num_layers = struct.unpack("<III", f.read(12))
        if magic != MAGIC:
            raise ValueError(f"Invalid magic: 0x{magic:08X} (expected 0x{MAGIC:08X})")
        if version != VERSION:
            raise ValueError(f"Unsupported version: {version}")

        layers = []
        for _ in range(num_layers):
            in_f, out_f, act = struct.unpack("<III", f.read(12))
            weight_data = np.frombuffer(f.read(out_f * in_f * 4), dtype=np.float32)
            bias_data = np.frombuffer(f.read(out_f * 4), dtype=np.float32)
            layers.append({
                "weight": weight_data.reshape(out_f, in_f),
                "bias": bias_data.copy(),
                "activation": act,
            })
    return layers


def forward_mlp_numpy(layers: list[dict], x: np.ndarray) -> np.ndarray:
    """Run MLP forward pass using numpy (no PyTorch dependency needed)."""
    for layer in layers:
        x = layer["weight"] @ x + layer["bias"]
        if layer["activation"] == ACTIVATION_RELU:
            x = np.maximum(x, 0)
        elif layer["activation"] == ACTIVATION_TANH:
            x = np.tanh(x)
    return x


def l2_normalize(v: np.ndarray) -> np.ndarray:
    """L2-normalize a vector."""
    norm = np.linalg.norm(v)
    if norm > 1e-8:
        return v / norm
    return v


def load_tags_file(path: Path) -> dict[str, list[str]]:
    """Load a clip-to-tags mapping JSON file.

    Expected format:
    {
        "walk_forward.npy": ["walk", "locomotion"],
        "run_fast.npy": ["run", "locomotion"],
        "crouch_idle.npy": ["crouch", "idle"],
        ...
    }

    Or a list format:
    [
        {"clip": "walk_forward.npy", "tags": ["walk", "locomotion"]},
        ...
    ]
    """
    with open(path) as f:
        data = json.load(f)

    if isinstance(data, dict):
        return data
    if isinstance(data, list):
        return {entry["clip"]: entry["tags"] for entry in data}
    raise ValueError(f"Unexpected tags file format in {path}")


def infer_tags_from_filename(clip_name: str) -> list[str]:
    """Infer semantic tags from a clip filename.

    Examples:
        walk_forward.npy → ["walk"]
        run_fast.npy → ["run"]
        crouch_idle.npy → ["crouch", "idle"]
        kick_right.npy → ["kick", "strike"]
    """
    stem = Path(clip_name).stem.lower()
    tags = []

    tag_keywords = {
        "walk": ["walk"],
        "run": ["run"],
        "sprint": ["run", "sprint"],
        "jog": ["run", "jog"],
        "idle": ["idle"],
        "stand": ["idle"],
        "crouch": ["crouch"],
        "sneak": ["crouch", "sneak"],
        "kick": ["kick", "strike"],
        "punch": ["punch", "strike"],
        "strike": ["strike"],
        "attack": ["strike"],
        "jump": ["jump"],
        "roll": ["roll"],
        "dodge": ["dodge"],
        "turn": ["turn"],
        "strafe": ["strafe"],
    }

    for keyword, keyword_tags in tag_keywords.items():
        if keyword in stem:
            tags.extend(keyword_tags)

    # Deduplicate while preserving order
    seen = set()
    unique_tags = []
    for t in tags:
        if t not in seen:
            seen.add(t)
            unique_tags.append(t)

    return unique_tags if unique_tags else ["unknown"]


def encode_clips_with_bin_encoder(encoder_path: Path,
                                   clips_dir: Path,
                                   tags_map: dict[str, list[str]]) -> list[dict]:
    """Encode motion clips using an MLP1 .bin encoder (numpy-only)."""
    if np is None:
        print("Error: numpy is required", file=sys.stderr)
        sys.exit(1)

    encoder_layers = load_mlp_bin(encoder_path)
    print(f"Loaded encoder from {encoder_path}")
    in_dim = encoder_layers[0]["weight"].shape[1]
    out_dim = encoder_layers[-1]["weight"].shape[0]
    print(f"  Input dim: {in_dim}, Output dim: {out_dim}")

    clip_files = sorted(clips_dir.glob("*.npy"))
    if not clip_files:
        print(f"Warning: no .npy files found in {clips_dir}", file=sys.stderr)
        return []

    behaviors = []
    for clip_file in clip_files:
        obs = np.load(clip_file).astype(np.float32)
        # If obs is 2D (frames x features), flatten or use last window
        if obs.ndim == 2:
            # Use the full flattened observation if it matches encoder input
            flat = obs.flatten()
            if flat.shape[0] >= in_dim:
                flat = flat[:in_dim]
            else:
                # Zero-pad
                padded = np.zeros(in_dim, dtype=np.float32)
                padded[: flat.shape[0]] = flat
                flat = padded
        elif obs.ndim == 1:
            flat = obs
            if flat.shape[0] < in_dim:
                padded = np.zeros(in_dim, dtype=np.float32)
                padded[: flat.shape[0]] = flat
                flat = padded
            elif flat.shape[0] > in_dim:
                flat = flat[:in_dim]
        else:
            print(f"  Skipping {clip_file.name}: unexpected shape {obs.shape}")
            continue

        latent = forward_mlp_numpy(encoder_layers, flat)
        latent = l2_normalize(latent)

        clip_name = clip_file.name
        tags = tags_map.get(clip_name, infer_tags_from_filename(clip_name))

        behaviors.append({
            "clip": clip_name,
            "tags": tags,
            "latent": latent.tolist(),
        })
        print(f"  Encoded {clip_name} → tags={tags}")

    return behaviors


def encode_clips_with_torch(checkpoint_path: Path,
                             clips_dir: Path,
                             tags_map: dict[str, list[str]]) -> list[dict]:
    """Encode motion clips using a PyTorch encoder checkpoint."""
    if torch is None:
        print("Error: PyTorch is required for --encoder-checkpoint", file=sys.stderr)
        sys.exit(1)
    if np is None:
        print("Error: numpy is required", file=sys.stderr)
        sys.exit(1)

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    if isinstance(checkpoint, dict):
        if "model" in checkpoint:
            state_dict = checkpoint["model"]
        elif "state_dict" in checkpoint:
            state_dict = checkpoint["state_dict"]
        else:
            state_dict = checkpoint
    else:
        print("Error: unexpected checkpoint format", file=sys.stderr)
        sys.exit(1)

    # Find encoder keys
    encoder_prefixes = [
        "a2c_network.encoder.",
        "a2c_network._enc_mlp.",
        "model.a2c_network.encoder.",
    ]
    encoder_prefix = None
    for prefix in encoder_prefixes:
        if any(k.startswith(prefix) for k in state_dict):
            encoder_prefix = prefix
            break

    if encoder_prefix is None:
        print("Error: encoder not found in checkpoint", file=sys.stderr)
        return []

    # Build encoder from state dict
    weight_keys = sorted(
        k for k in state_dict
        if k.startswith(encoder_prefix) and k.endswith(".weight")
    )

    # Build a simple sequential model
    encoder_layers = []
    for wk in weight_keys:
        layer_prefix = wk[: -len(".weight")]
        bk = layer_prefix + ".bias"
        encoder_layers.append({
            "weight": state_dict[wk].numpy(),
            "bias": state_dict[bk].numpy() if bk in state_dict else np.zeros(state_dict[wk].shape[0]),
            "activation": ACTIVATION_RELU,  # All hidden layers use ReLU
        })
    # Last layer has no activation (L2 normalized externally)
    if encoder_layers:
        encoder_layers[-1]["activation"] = ACTIVATION_NONE

    in_dim = encoder_layers[0]["weight"].shape[1]
    out_dim = encoder_layers[-1]["weight"].shape[0]
    print(f"Loaded encoder from checkpoint (prefix: {encoder_prefix})")
    print(f"  Input dim: {in_dim}, Output dim: {out_dim}")

    clip_files = sorted(clips_dir.glob("*.npy"))
    if not clip_files:
        print(f"Warning: no .npy files found in {clips_dir}")
        return []

    behaviors = []
    for clip_file in clip_files:
        obs = np.load(clip_file).astype(np.float32)
        if obs.ndim == 2:
            flat = obs.flatten()[:in_dim]
        elif obs.ndim == 1:
            flat = obs[:in_dim]
        else:
            continue

        if flat.shape[0] < in_dim:
            padded = np.zeros(in_dim, dtype=np.float32)
            padded[: flat.shape[0]] = flat
            flat = padded

        latent = forward_mlp_numpy(encoder_layers, flat)
        latent = l2_normalize(latent)

        clip_name = clip_file.name
        tags = tags_map.get(clip_name, infer_tags_from_filename(clip_name))

        behaviors.append({
            "clip": clip_name,
            "tags": tags,
            "latent": latent.tolist(),
        })
        print(f"  Encoded {clip_name} → tags={tags}")

    return behaviors


def create_dummy_library(output_path: Path, latent_dim: int = 64) -> None:
    """Create a dummy latent library with hand-crafted latent vectors for testing.

    Generates orthogonal-ish latent vectors for common behavior types.
    """
    import random
    random.seed(42)

    behaviors_spec = [
        ("walk_forward", ["walk", "locomotion"]),
        ("walk_backward", ["walk", "locomotion"]),
        ("run_forward", ["run", "locomotion"]),
        ("run_fast", ["run", "sprint", "locomotion"]),
        ("jog", ["run", "jog", "locomotion"]),
        ("idle_stand", ["idle"]),
        ("idle_look", ["idle"]),
        ("crouch_walk", ["crouch", "locomotion"]),
        ("crouch_idle", ["crouch", "idle"]),
        ("kick_right", ["kick", "strike"]),
        ("punch_jab", ["punch", "strike"]),
        ("jump_forward", ["jump"]),
        ("roll_forward", ["roll", "dodge"]),
        ("turn_left", ["turn"]),
        ("strafe_right", ["strafe", "locomotion"]),
    ]

    behaviors = []
    for i, (clip_name, tags) in enumerate(behaviors_spec):
        # Generate a random unit vector, seeded so each behavior is unique
        latent = [random.gauss(0, 1) for _ in range(latent_dim)]
        norm = math.sqrt(sum(x * x for x in latent))
        latent = [x / norm for x in latent]

        behaviors.append({
            "clip": f"{clip_name}.npy",
            "tags": tags,
            "latent": [round(x, 6) for x in latent],
        })

    library = {
        "latent_dim": latent_dim,
        "behaviors": behaviors,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(library, f, indent=2)

    print(f"Created dummy latent library: {output_path}")
    print(f"  {len(behaviors)} behaviors, latent_dim={latent_dim}")
    for b in behaviors:
        print(f"    {b['clip']}: tags={b['tags']}")


def main():
    parser = argparse.ArgumentParser(
        description="Pre-encode motion clips into a CALM latent library JSON",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Encode using exported encoder .bin (no PyTorch needed):
    python tools/calm_encode_library.py \\
        --encoder-bin data/calm/models/encoder.bin \\
        --clips data/motion_clips/ \\
        --output data/calm/latent_library.json

  Encode using PyTorch checkpoint:
    python tools/calm_encode_library.py \\
        --encoder-checkpoint models/calm_checkpoint.pth \\
        --clips data/motion_clips/ \\
        --output data/calm/latent_library.json

  With explicit tag file:
    python tools/calm_encode_library.py \\
        --encoder-bin encoder.bin \\
        --clips clips/ \\
        --tags-file clip_tags.json \\
        --output latent_library.json

  Create dummy library for testing:
    python tools/calm_encode_library.py \\
        --dummy --output data/calm/latent_library.json
        """,
    )
    parser.add_argument(
        "--encoder-bin", type=Path,
        help="Path to exported encoder .bin file (MLP1 format)",
    )
    parser.add_argument(
        "--encoder-checkpoint", type=Path,
        help="Path to PyTorch checkpoint containing encoder",
    )
    parser.add_argument(
        "--clips", type=Path,
        help="Directory containing motion clip .npy files",
    )
    parser.add_argument(
        "--tags-file", type=Path,
        help="Optional JSON file mapping clip names to tag lists",
    )
    parser.add_argument(
        "--output", type=Path, default=Path("data/calm/latent_library.json"),
        help="Output JSON path (default: data/calm/latent_library.json)",
    )
    parser.add_argument(
        "--latent-dim", type=int, default=64,
        help="Latent vector dimension (default: 64)",
    )
    parser.add_argument(
        "--dummy", action="store_true",
        help="Create a dummy library with random latents for testing",
    )

    args = parser.parse_args()

    if args.dummy:
        create_dummy_library(args.output, args.latent_dim)
        return

    if args.encoder_bin is None and args.encoder_checkpoint is None:
        parser.error("either --encoder-bin or --encoder-checkpoint is required (or use --dummy)")

    if args.clips is None:
        parser.error("--clips is required when encoding clips")

    if not args.clips.is_dir():
        print(f"Error: clips directory not found: {args.clips}", file=sys.stderr)
        sys.exit(1)

    # Load tags
    tags_map: dict[str, list[str]] = {}
    if args.tags_file:
        tags_map = load_tags_file(args.tags_file)
        print(f"Loaded tags for {len(tags_map)} clips from {args.tags_file}")

    # Encode
    if args.encoder_bin:
        behaviors = encode_clips_with_bin_encoder(args.encoder_bin, args.clips, tags_map)
    else:
        behaviors = encode_clips_with_torch(args.encoder_checkpoint, args.clips, tags_map)

    if not behaviors:
        print("Warning: no behaviors encoded", file=sys.stderr)
        sys.exit(1)

    # Write library JSON
    library = {
        "latent_dim": args.latent_dim,
        "behaviors": behaviors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(library, f, indent=2)

    print(f"\nWrote latent library: {args.output}")
    print(f"  {len(behaviors)} behaviors, latent_dim={args.latent_dim}")


if __name__ == "__main__":
    main()
