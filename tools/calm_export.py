#!/usr/bin/env python3
"""Export PyTorch CALM checkpoints to the engine's MLP1 binary weight format.

Extracts LLC (style MLP, main MLP, mu head), encoder, and HLC networks from
a NVlabs/CALM or rl_games checkpoint and writes each as a separate .bin file
readable by the C++ ModelLoader.

Binary format (MLP1):
    Header:  magic=0x4D4C5031, version=1, numLayers
    Per layer: inFeatures, outFeatures, activationType(0=None,1=ReLU,2=Tanh)
               float[outFeatures*inFeatures] weights (row-major)
               float[outFeatures]            bias

Usage:
    python tools/calm_export.py --checkpoint path/to/checkpoint.pth --output data/calm/models/

    Produces:
        llc_style.bin   - Style MLP (latent z → style embedding)
        llc_main.bin    - Main policy MLP (concat(styleEmbed, obs) → hidden)
        llc_mu_head.bin - Action head (hidden → joint targets)
        encoder.bin     - Motion encoder (stacked obs → latent, optional)
        hlc_heading.bin - Heading HLC (optional, if present in checkpoint)
        hlc_location.bin - Location HLC (optional)
        hlc_strike.bin  - Strike HLC (optional)
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import torch
except ImportError:
    print("Error: PyTorch is required. Install with: pip install torch", file=sys.stderr)
    sys.exit(1)

# MLP1 binary format constants
MAGIC = 0x4D4C5031
VERSION = 1

# Activation type encoding
ACTIVATION_NONE = 0
ACTIVATION_RELU = 1
ACTIVATION_TANH = 2


def write_mlp_bin(path: Path, layers: list[dict]) -> None:
    """Write an MLP to the engine's MLP1 binary format.

    Args:
        path: Output .bin file path
        layers: List of dicts with keys:
            'weight': 2D tensor [outFeatures, inFeatures]
            'bias': 1D tensor [outFeatures]
            'activation': one of ACTIVATION_NONE, ACTIVATION_RELU, ACTIVATION_TANH
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(layers)))
        for layer in layers:
            weight = layer["weight"]
            bias = layer["bias"]
            activation = layer["activation"]
            out_features, in_features = weight.shape
            f.write(struct.pack("<III", in_features, out_features, activation))
            # Weights: row-major [outFeatures x inFeatures]
            f.write(weight.cpu().detach().float().numpy().tobytes())
            # Bias: [outFeatures]
            f.write(bias.cpu().detach().float().numpy().tobytes())
    print(f"  Wrote {path} ({len(layers)} layers)")


def extract_sequential_layers(state_dict: dict, prefix: str,
                              activations: list[int]) -> list[dict]:
    """Extract layers from a PyTorch nn.Sequential-style state dict.

    Handles both numbered (0.weight, 2.weight) and named (fc1.weight) patterns.
    The activations list specifies the activation for each linear layer in order.

    Args:
        state_dict: Full checkpoint state dict
        prefix: Key prefix (e.g., 'a2c_network.actor_mlp.')
        activations: List of activation types, one per linear layer
    """
    # Collect weight/bias pairs under this prefix
    weight_keys = sorted(
        k for k in state_dict
        if k.startswith(prefix) and k.endswith(".weight")
    )
    bias_keys = sorted(
        k for k in state_dict
        if k.startswith(prefix) and k.endswith(".bias")
    )

    if not weight_keys:
        return []

    # Match weights to biases by layer name
    layers = []
    for wk in weight_keys:
        layer_prefix = wk[: -len(".weight")]
        bk = layer_prefix + ".bias"
        if bk not in state_dict:
            print(f"  Warning: no bias for {wk}, skipping")
            continue
        layers.append({
            "weight": state_dict[wk],
            "bias": state_dict[bk],
        })

    # Assign activations (extend with ACTIVATION_NONE if fewer provided)
    for i, layer in enumerate(layers):
        if i < len(activations):
            layer["activation"] = activations[i]
        else:
            layer["activation"] = ACTIVATION_NONE

    return layers


def try_extract_calm_llc(state_dict: dict) -> tuple[list, list, list]:
    """Try to extract LLC components from various checkpoint formats.

    Returns (style_layers, main_layers, mu_head_layers) or empty lists on failure.
    """
    # Pattern 1: NVlabs/CALM format (a2c_network.*)
    style_prefixes = [
        "a2c_network.style_mlp.",
        "a2c_network._style_mlp.",
        "model.a2c_network.style_mlp.",
    ]
    main_prefixes = [
        "a2c_network.actor_mlp.",
        "a2c_network._actor_mlp.",
        "model.a2c_network.actor_mlp.",
    ]
    mu_prefixes = [
        "a2c_network.mu.",
        "a2c_network._mu.",
        "model.a2c_network.mu.",
    ]

    # CALM default: style MLP uses tanh on all layers
    # Main MLP uses ReLU on hidden layers, None on output
    style_activations = [ACTIVATION_TANH, ACTIVATION_TANH]
    main_activations = [ACTIVATION_RELU, ACTIVATION_RELU, ACTIVATION_RELU]
    mu_activations = [ACTIVATION_NONE]

    style_layers = []
    main_layers = []
    mu_layers = []

    for prefix in style_prefixes:
        style_layers = extract_sequential_layers(state_dict, prefix, style_activations)
        if style_layers:
            break

    for prefix in main_prefixes:
        main_layers = extract_sequential_layers(state_dict, prefix, main_activations)
        if main_layers:
            break

    for prefix in mu_prefixes:
        mu_layers = extract_sequential_layers(state_dict, prefix, mu_activations)
        if mu_layers:
            break

    return style_layers, main_layers, mu_layers


def try_extract_encoder(state_dict: dict) -> list:
    """Try to extract the motion encoder network."""
    encoder_prefixes = [
        "a2c_network.encoder.",
        "a2c_network._enc_mlp.",
        "model.a2c_network.encoder.",
    ]
    # Encoder: ReLU hidden, None on final (output gets L2-normalized externally)
    activations = [ACTIVATION_RELU, ACTIVATION_RELU, ACTIVATION_RELU, ACTIVATION_NONE]

    for prefix in encoder_prefixes:
        layers = extract_sequential_layers(state_dict, prefix, activations)
        if layers:
            return layers
    return []


def try_extract_hlc(state_dict: dict, task: str) -> list:
    """Try to extract an HLC network for a specific task."""
    prefixes = [
        f"a2c_network.hlc_{task}.",
        f"hlc_{task}.",
        f"a2c_network.{task}_hlc.",
        f"model.a2c_network.hlc_{task}.",
    ]
    # HLC: ReLU hidden, None on final (output gets L2-normalized externally)
    activations = [ACTIVATION_RELU, ACTIVATION_RELU, ACTIVATION_NONE]

    for prefix in prefixes:
        layers = extract_sequential_layers(state_dict, prefix, activations)
        if layers:
            return layers
    return []


def print_layer_info(name: str, layers: list[dict]) -> None:
    """Print summary of extracted layers."""
    if not layers:
        print(f"  {name}: not found")
        return
    dims = []
    for layer in layers:
        w = layer["weight"]
        act_names = {0: "none", 1: "relu", 2: "tanh"}
        dims.append(f"{w.shape[1]}→{w.shape[0]}({act_names[layer['activation']]})")
    print(f"  {name}: {' → '.join(dims)}")


def load_checkpoint(path: Path) -> dict:
    """Load a PyTorch checkpoint, handling various formats."""
    checkpoint = torch.load(path, map_location="cpu", weights_only=False)

    # rl_games format: state dict is nested under 'model'
    if isinstance(checkpoint, dict):
        if "model" in checkpoint:
            return checkpoint["model"]
        if "state_dict" in checkpoint:
            return checkpoint["state_dict"]
    return checkpoint


def export_checkpoint(checkpoint_path: Path, output_dir: Path,
                      tasks: list[str] | None = None) -> bool:
    """Export a CALM checkpoint to engine binary format.

    Returns True if at least the LLC was exported successfully.
    """
    print(f"Loading checkpoint: {checkpoint_path}")
    state_dict = load_checkpoint(checkpoint_path)

    if not isinstance(state_dict, dict):
        print("Error: checkpoint is not a state dict", file=sys.stderr)
        return False

    print(f"  Found {len(state_dict)} keys")

    # Extract LLC components
    style_layers, main_layers, mu_layers = try_extract_calm_llc(state_dict)
    print("\nLLC components:")
    print_layer_info("Style MLP", style_layers)
    print_layer_info("Main MLP", main_layers)
    print_layer_info("Mu Head", mu_layers)

    if not style_layers or not main_layers or not mu_layers:
        print("\nError: could not find LLC components in checkpoint.", file=sys.stderr)
        print("Available keys (first 30):", file=sys.stderr)
        for k in sorted(state_dict.keys())[:30]:
            print(f"  {k}: {state_dict[k].shape}", file=sys.stderr)
        return False

    # Write LLC
    write_mlp_bin(output_dir / "llc_style.bin", style_layers)
    write_mlp_bin(output_dir / "llc_main.bin", main_layers)
    write_mlp_bin(output_dir / "llc_mu_head.bin", mu_layers)

    # Extract and write encoder (optional)
    encoder_layers = try_extract_encoder(state_dict)
    print("\nEncoder:")
    print_layer_info("Encoder", encoder_layers)
    if encoder_layers:
        write_mlp_bin(output_dir / "encoder.bin", encoder_layers)

    # Extract and write HLCs (optional)
    if tasks is None:
        tasks = ["heading", "location", "strike"]

    print("\nHLCs:")
    for task in tasks:
        hlc_layers = try_extract_hlc(state_dict, task)
        print_layer_info(f"HLC ({task})", hlc_layers)
        if hlc_layers:
            write_mlp_bin(output_dir / f"hlc_{task}.bin", hlc_layers)

    print("\nExport complete.")
    return True


def create_dummy_checkpoint(output_dir: Path) -> None:
    """Create dummy .bin files with random weights for testing the C++ loader.

    Matches CALM default architecture:
        Style MLP: 64 → 512(tanh) → 256(tanh)
        Main MLP: (256+102) → 1024(relu) → 1024(relu) → 512(relu)
        Mu Head: 512 → 37(none)
        Encoder: 612 → 1024(relu) → 1024(relu) → 512(relu) → 64(none)
        HLC heading: 3 → 256(relu) → 128(relu) → 64(none)
        HLC location: 3 → 256(relu) → 128(relu) → 64(none)
        HLC strike: 4 → 256(relu) → 128(relu) → 64(none)
    """
    print("Creating dummy CALM model files for testing...")
    output_dir.mkdir(parents=True, exist_ok=True)

    def make_layers(dims: list[tuple[int, int, int]]) -> list[dict]:
        layers = []
        for in_f, out_f, act in dims:
            layers.append({
                "weight": torch.randn(out_f, in_f) * 0.01,
                "bias": torch.zeros(out_f),
                "activation": act,
            })
        return layers

    # LLC
    write_mlp_bin(output_dir / "llc_style.bin", make_layers([
        (64, 512, ACTIVATION_TANH),
        (512, 256, ACTIVATION_TANH),
    ]))
    write_mlp_bin(output_dir / "llc_main.bin", make_layers([
        (358, 1024, ACTIVATION_RELU),  # 256 (style) + 102 (obs)
        (1024, 1024, ACTIVATION_RELU),
        (1024, 512, ACTIVATION_RELU),
    ]))
    write_mlp_bin(output_dir / "llc_mu_head.bin", make_layers([
        (512, 37, ACTIVATION_NONE),
    ]))

    # Encoder
    write_mlp_bin(output_dir / "encoder.bin", make_layers([
        (612, 1024, ACTIVATION_RELU),
        (1024, 1024, ACTIVATION_RELU),
        (1024, 512, ACTIVATION_RELU),
        (512, 64, ACTIVATION_NONE),
    ]))

    # HLCs
    write_mlp_bin(output_dir / "hlc_heading.bin", make_layers([
        (3, 256, ACTIVATION_RELU),
        (256, 128, ACTIVATION_RELU),
        (128, 64, ACTIVATION_NONE),
    ]))
    write_mlp_bin(output_dir / "hlc_location.bin", make_layers([
        (3, 256, ACTIVATION_RELU),
        (256, 128, ACTIVATION_RELU),
        (128, 64, ACTIVATION_NONE),
    ]))
    write_mlp_bin(output_dir / "hlc_strike.bin", make_layers([
        (4, 256, ACTIVATION_RELU),
        (256, 128, ACTIVATION_RELU),
        (128, 64, ACTIVATION_NONE),
    ]))

    print("Dummy model files created.")


def main():
    parser = argparse.ArgumentParser(
        description="Export PyTorch CALM checkpoints to engine MLP1 binary format",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Export a real checkpoint:
    python tools/calm_export.py --checkpoint models/calm_checkpoint.pth --output data/calm/models/

  Create dummy weights for testing:
    python tools/calm_export.py --dummy --output data/calm/models/

  Export with custom HLC task names:
    python tools/calm_export.py --checkpoint cp.pth --output out/ --tasks heading location strike patrol

  List keys in a checkpoint (for debugging):
    python tools/calm_export.py --checkpoint cp.pth --list-keys
        """,
    )
    parser.add_argument(
        "--checkpoint", type=Path,
        help="Path to PyTorch checkpoint (.pth or .pt)",
    )
    parser.add_argument(
        "--output", type=Path, default=Path("data/calm/models"),
        help="Output directory for .bin files (default: data/calm/models/)",
    )
    parser.add_argument(
        "--tasks", nargs="+", default=["heading", "location", "strike"],
        help="HLC task names to look for (default: heading location strike)",
    )
    parser.add_argument(
        "--dummy", action="store_true",
        help="Create dummy model files with random weights for testing",
    )
    parser.add_argument(
        "--list-keys", action="store_true",
        help="List all keys in the checkpoint and exit",
    )

    args = parser.parse_args()

    if args.dummy:
        create_dummy_checkpoint(args.output)
        return

    if args.checkpoint is None:
        parser.error("--checkpoint is required (or use --dummy)")

    if not args.checkpoint.exists():
        print(f"Error: checkpoint not found: {args.checkpoint}", file=sys.stderr)
        sys.exit(1)

    state_dict = load_checkpoint(args.checkpoint)

    if args.list_keys:
        print(f"Keys in {args.checkpoint}:")
        for k in sorted(state_dict.keys()):
            if hasattr(state_dict[k], "shape"):
                print(f"  {k}: {state_dict[k].shape}")
            else:
                print(f"  {k}: {type(state_dict[k])}")
        return

    if not export_checkpoint(args.checkpoint, args.output, args.tasks):
        sys.exit(1)


if __name__ == "__main__":
    main()
