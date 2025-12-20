#!/usr/bin/env python3
"""
Scale tree preset JSON files to adjust tree dimensions.

Usage:
    python3 tools/scale_tree_presets.py                    # Scale all presets by 0.25
    python3 tools/scale_tree_presets.py --scale-factor 0.5 # Custom scale factor
    python3 tools/scale_tree_presets.py --dry-run          # Preview changes without writing
"""

import argparse
import json
import os
from pathlib import Path


def scale_all_values(d: dict, scale_factor: float) -> dict:
    """Scale all numeric values in a dict with string keys like {"0": 1.5, "1": 2.0}."""
    return {k: round(v * scale_factor, 2) for k, v in d.items()}


def scale_level_zero_only(d: dict, scale_factor: float) -> dict:
    """Scale only the '0' key value in a dict, leaving other levels unchanged.

    In ez-tree, radius values for levels 1+ are relative multipliers applied to parent radius.
    """
    result = dict(d)
    if "0" in result:
        result["0"] = round(result["0"] * scale_factor, 2)
    return result


def scale_preset(data: dict, scale_factor: float, max_texture_scale_y: float = 3.0) -> dict:
    """
    Scale tree preset dimensions by the given factor.

    Scales:
    - branch.length (all levels - these are absolute values)
    - branch.radius["0"] only (levels 1+ are relative multipliers)
    - leaves.size

    Also caps bark.textureScale.y to reduce UV repetition.
    """
    result = json.loads(json.dumps(data))  # Deep copy

    # Scale branch dimensions
    if "branch" in result:
        branch = result["branch"]

        # Scale all length values (all levels are absolute)
        if "length" in branch and isinstance(branch["length"], dict):
            branch["length"] = scale_all_values(branch["length"], scale_factor)

        # Scale radius[0] only (levels 1+ are relative multipliers applied to parent radius)
        if "radius" in branch and isinstance(branch["radius"], dict):
            branch["radius"] = scale_level_zero_only(branch["radius"], scale_factor)

    # Scale leaf size proportionally
    if "leaves" in result:
        leaves = result["leaves"]
        if "size" in leaves:
            leaves["size"] = round(leaves["size"] * scale_factor, 2)

    # Cap bark textureScale.y to reduce UV repetition
    if "bark" in result:
        bark = result["bark"]
        if "textureScale" in bark:
            if bark["textureScale"].get("y", 1.0) > max_texture_scale_y:
                bark["textureScale"]["y"] = max_texture_scale_y

    return result


def format_json(data: dict) -> str:
    """Format JSON with reasonable precision and indentation."""
    return json.dumps(data, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Scale tree preset JSON files")
    parser.add_argument(
        "--scale-factor", "-s",
        type=float,
        default=0.25,
        help="Scale factor for dimensions (default: 0.25)"
    )
    parser.add_argument(
        "--max-texture-scale-y", "-t",
        type=float,
        default=3.0,
        help="Maximum value for bark.textureScale.y (default: 3.0)"
    )
    parser.add_argument(
        "--dry-run", "-n",
        action="store_true",
        help="Preview changes without writing files"
    )
    parser.add_argument(
        "--preset-dir", "-d",
        type=str,
        default=None,
        help="Directory containing preset JSON files (default: auto-detect)"
    )
    args = parser.parse_args()

    # Find preset directory
    if args.preset_dir:
        preset_dir = Path(args.preset_dir)
    else:
        # Auto-detect from script location
        script_dir = Path(__file__).parent
        preset_dir = script_dir.parent / "assets" / "trees" / "presets"

    if not preset_dir.exists():
        print(f"Error: Preset directory not found: {preset_dir}")
        return 1

    json_files = sorted(preset_dir.glob("*.json"))
    if not json_files:
        print(f"Error: No JSON files found in {preset_dir}")
        return 1

    print(f"Scale factor: {args.scale_factor}")
    print(f"Max textureScale.y: {args.max_texture_scale_y}")
    print(f"Preset directory: {preset_dir}")
    print(f"Found {len(json_files)} preset files")
    print()

    for json_path in json_files:
        print(f"Processing: {json_path.name}")

        with open(json_path, "r") as f:
            original = json.load(f)

        scaled = scale_preset(original, args.scale_factor, args.max_texture_scale_y)

        # Show changes
        if "branch" in original and "branch" in scaled:
            orig_len = original["branch"].get("length", {})
            new_len = scaled["branch"].get("length", {})
            if orig_len and new_len:
                for k in sorted(orig_len.keys()):
                    if k in new_len:
                        print(f"  length[{k}]: {orig_len[k]} -> {new_len[k]}")

            orig_rad = original["branch"].get("radius", {})
            new_rad = scaled["branch"].get("radius", {})
            if orig_rad and new_rad and "0" in orig_rad:
                print(f"  radius[0]: {orig_rad['0']} -> {new_rad['0']} (levels 1+ unchanged)")

        if "leaves" in original and "leaves" in scaled:
            orig_size = original["leaves"].get("size", 0)
            new_size = scaled["leaves"].get("size", 0)
            if orig_size:
                print(f"  leaf size: {orig_size} -> {new_size}")

        if "bark" in original and "bark" in scaled:
            orig_ts = original["bark"].get("textureScale", {}).get("y", 1)
            new_ts = scaled["bark"].get("textureScale", {}).get("y", 1)
            if orig_ts != new_ts:
                print(f"  textureScale.y: {orig_ts} -> {new_ts}")

        if not args.dry_run:
            with open(json_path, "w") as f:
                f.write(format_json(scaled))
                f.write("\n")
            print(f"  -> Written")
        else:
            print(f"  -> Dry run, not written")

        print()

    if args.dry_run:
        print("Dry run complete. Use without --dry-run to apply changes.")
    else:
        print(f"Scaled {len(json_files)} preset files.")

    return 0


if __name__ == "__main__":
    exit(main())
