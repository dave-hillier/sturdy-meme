#!/usr/bin/env python3
"""Convert FBX animation files to the engine's training .npy format.

Uses the ufbx library to parse FBX files (Mixamo, etc.), extract per-frame
animation data via evaluate_transform, compute FK with a hardcoded Mixamo
skeleton hierarchy, retarget to engine skeleton, and output .npy files
identical to those from convert_mocap_to_training.py.

Output .npy (saved with np.save, allow_pickle=True) contains a dict:
    joint_rotations: (num_frames, num_joints, 4) float32 - local quaternions [x,y,z,w]
    joint_positions: (num_frames, num_joints, 3) float32 - global positions
    root_positions:  (num_frames, 3)             float32 - root world position
    root_rotations:  (num_frames, 4)             float32 - root world rotation [x,y,z,w]
    fps:             float32 scalar
    joint_names:     list of str - joint names in training order

Usage:
    python tools/convert_fbx_to_training.py assets/characters/fbx/ data/calm/motions/ \\
        --retarget data/calm/retarget_map.json

    python tools/convert_fbx_to_training.py "assets/characters/fbx/sword and shield idle.fbx" \\
        data/calm/motions/ --retarget data/calm/retarget_map.json
"""

import argparse
import json
import logging
import sys
from pathlib import Path

import numpy as np

try:
    import ufbx
except ImportError:
    print("Error: ufbx not installed. Run: pip install ufbx", file=sys.stderr)
    sys.exit(1)

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger(__name__)

# ============================================================================
# Quaternion math (numpy, [x,y,z,w] convention matching GLM/engine)
# ============================================================================


def quat_identity():
    return np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float64)


def quat_multiply(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return np.array([
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ], dtype=np.float64)


def quat_normalize(q):
    n = np.linalg.norm(q)
    if n < 1e-12:
        return quat_identity()
    return q / n


def quat_to_mat3(q):
    x, y, z, w = q
    x2, y2, z2 = x * 2.0, y * 2.0, z * 2.0
    xx, xy, xz = x * x2, x * y2, x * z2
    yy, yz, zz = y * y2, y * z2, z * z2
    wx, wy, wz = w * x2, w * y2, w * z2
    return np.array([
        [1.0 - (yy + zz), xy - wz, xz + wy],
        [xy + wz, 1.0 - (xx + zz), yz - wx],
        [xz - wy, yz + wx, 1.0 - (xx + yy)],
    ], dtype=np.float64)


def quat_rotate_vec3(q, v):
    return quat_to_mat3(q) @ np.asarray(v, dtype=np.float64)


# ============================================================================
# Training joint ordering (MUST match CharacterConfig::getHumanoidBoneDefs())
# ============================================================================

HUMANOID_BONE_DEFS = [
    ("pelvis",          "Hips",         3, False),
    ("abdomen",         "Spine",        3, False),
    ("chest",           "Spine1",       3, False),
    ("neck",            "Neck",         3, False),
    ("head",            "Head",         3, True),
    ("right_upper_arm", "RightArm",     3, False),
    ("right_lower_arm", "RightForeArm", 1, False),
    ("right_hand",      "RightHand",    0, True),
    ("left_upper_arm",  "LeftArm",      3, False),
    ("left_lower_arm",  "LeftForeArm",  1, False),
    ("left_hand",       "LeftHand",     0, True),
    ("right_thigh",     "RightUpLeg",   3, False),
    ("right_shin",      "RightLeg",     1, False),
    ("right_foot",      "RightFoot",    3, True),
    ("left_thigh",      "LeftUpLeg",    3, False),
    ("left_shin",       "LeftLeg",      1, False),
    ("left_foot",       "LeftFoot",     3, True),
]

TRAINING_JOINT_NAMES = [name for name, _, _, _ in HUMANOID_BONE_DEFS]

# Mixamo skeleton hierarchy (after stripping "mixamorig:" prefix).
# Maps bone name -> parent bone name. Hips is the root (parent=None).
# This covers the standard Mixamo skeleton used by all Mixamo animations.
MIXAMO_HIERARCHY = {
    "Hips": None,
    "Spine": "Hips",
    "Spine1": "Spine",
    "Spine2": "Spine1",
    "Neck": "Spine2",
    "Head": "Neck",
    "LeftShoulder": "Spine2",
    "LeftArm": "LeftShoulder",
    "LeftForeArm": "LeftArm",
    "LeftHand": "LeftForeArm",
    "RightShoulder": "Spine2",
    "RightArm": "RightShoulder",
    "RightForeArm": "RightArm",
    "RightHand": "RightForeArm",
    "LeftUpLeg": "Hips",
    "LeftLeg": "LeftUpLeg",
    "LeftFoot": "LeftUpLeg",      # Will be corrected below
    "RightUpLeg": "Hips",
    "RightLeg": "RightUpLeg",
    "RightFoot": "RightUpLeg",    # Will be corrected below
}
# Fix foot parents (LeftLeg/RightLeg, not UpLeg)
MIXAMO_HIERARCHY["LeftFoot"] = "LeftLeg"
MIXAMO_HIERARCHY["RightFoot"] = "RightLeg"

# Topological order for FK (parents before children)
MIXAMO_FK_ORDER = [
    "Hips",
    "Spine", "LeftUpLeg", "RightUpLeg",
    "Spine1",
    "LeftLeg", "RightLeg",
    "Spine2",
    "LeftFoot", "RightFoot",
    "Neck", "LeftShoulder", "RightShoulder",
    "Head",
    "LeftArm", "RightArm",
    "LeftForeArm", "RightForeArm",
    "LeftHand", "RightHand",
]

# Maps Mixamo bone name -> engine bone name (for retargeting)
MIXAMO_TO_ENGINE = {
    "Hips": "Hips",
    "Spine": "Spine",
    "Spine1": "Spine1",
    "Spine2": "Spine1",      # Fold extra spine into Spine1
    "Neck": "Neck",
    "Head": "Head",
    "LeftShoulder": "LeftArm",
    "LeftArm": "LeftArm",
    "LeftForeArm": "LeftForeArm",
    "LeftHand": "LeftHand",
    "RightShoulder": "RightArm",
    "RightArm": "RightArm",
    "RightForeArm": "RightForeArm",
    "RightHand": "RightHand",
    "LeftUpLeg": "LeftUpLeg",
    "LeftLeg": "LeftLeg",
    "LeftFoot": "LeftFoot",
    "RightUpLeg": "RightUpLeg",
    "RightLeg": "RightLeg",
    "RightFoot": "RightFoot",
}


def strip_mixamo_prefix(name):
    prefix = "mixamorig:"
    if name.startswith(prefix):
        return name[len(prefix):]
    return name


# ============================================================================
# Retargeting
# ============================================================================


def load_retarget_map(path):
    with open(path, "r") as f:
        data = json.load(f)
    return data["training_to_engine_joint_map"], data.get("scale_factor", 1.0)


def _find_nodes(scene):
    """Find ufbx node objects for all Mixamo bones we need.

    Returns dict: clean_bone_name -> ufbx node object
    """
    nodes = {}
    needed = set(MIXAMO_FK_ORDER)
    for i in range(len(scene.nodes)):
        node = scene.nodes[i]
        clean = strip_mixamo_prefix(node.name)
        if clean in needed:
            nodes[clean] = node
    return nodes


def _build_training_map(found_bones, retarget_map_path=None):
    """Build mapping from training bone names to Mixamo bone names.

    Returns dict: training_name -> mixamo_bone_name (or None)
    """
    # Default: training_name -> engine_name from HUMANOID_BONE_DEFS
    training_to_engine = {name: eng for name, eng, _, _ in HUMANOID_BONE_DEFS}

    # Override with retarget_map.json if provided
    if retarget_map_path:
        custom_map, _ = load_retarget_map(retarget_map_path)
        for t_name, e_name in custom_map.items():
            if t_name in training_to_engine:
                training_to_engine[t_name] = e_name

    # Invert MIXAMO_TO_ENGINE: engine_name -> first matching mixamo bone
    # Prefer the more specific bone (e.g. LeftArm over LeftShoulder)
    engine_to_mixamo = {}
    for mx_name, eng_name in MIXAMO_TO_ENGINE.items():
        if eng_name not in engine_to_mixamo:
            engine_to_mixamo[eng_name] = mx_name
        else:
            # Prefer the bone whose name matches the engine name exactly
            if mx_name == eng_name:
                engine_to_mixamo[eng_name] = mx_name

    tmap = {}
    for t_name in TRAINING_JOINT_NAMES:
        eng_name = training_to_engine.get(t_name)
        mx_name = engine_to_mixamo.get(eng_name)
        if mx_name and mx_name in found_bones:
            tmap[t_name] = mx_name
        else:
            tmap[t_name] = None
    return tmap


# ============================================================================
# Conversion
# ============================================================================


def convert_fbx(fbx_path, retarget_map_path=None, scale=1.0, target_fps=30.0):
    """Convert a single FBX file to the training dict format.

    Uses per-node evaluate_transform (stable in ufbx Python bindings) and
    computes world transforms via manual FK with the hardcoded Mixamo
    skeleton hierarchy.
    """
    logger.info("Loading: %s", fbx_path)

    try:
        scene = ufbx.load_file(str(fbx_path))
    except Exception as e:
        logger.error("Failed to load %s: %s", fbx_path, e)
        return None

    if not scene.anim_stacks:
        logger.error("No animation stacks in %s", fbx_path)
        return None

    stack = scene.anim_stacks[0]
    anim = stack.anim

    # Apply retarget map scale
    map_scale = 1.0
    if retarget_map_path:
        _, map_scale = load_retarget_map(retarget_map_path)
    total_scale = scale * map_scale

    # Find ufbx nodes for Mixamo bones
    mx_nodes = _find_nodes(scene)
    logger.info("  Found %d/%d Mixamo bones", len(mx_nodes), len(MIXAMO_FK_ORDER))

    # Build training -> Mixamo mapping
    tmap = _build_training_map(set(mx_nodes.keys()), retarget_map_path)

    found = [n for n, m in tmap.items() if m is not None]
    missing = [n for n, m in tmap.items() if m is None]
    logger.info("  Mapped %d/%d training joints", len(found), len(TRAINING_JOINT_NAMES))
    if missing:
        logger.warning("  Missing: %s", ", ".join(missing))

    # Calculate frame count
    duration = stack.time_end - stack.time_begin
    if duration <= 0:
        logger.error("Invalid animation duration in %s", fbx_path)
        return None

    frame_time = 1.0 / target_fps
    num_frames = int(duration * target_fps) + 1
    num_frames = max(2, num_frames)

    logger.info("  Duration: %.2fs, Frames: %d @ %.0f fps",
                duration, num_frames, target_fps)

    num_joints = len(TRAINING_JOINT_NAMES)

    # Allocate output
    joint_rotations = np.tile(
        np.array([0, 0, 0, 1], dtype=np.float32), (num_frames, num_joints, 1))
    joint_positions = np.zeros((num_frames, num_joints, 3), dtype=np.float32)
    root_positions = np.zeros((num_frames, 3), dtype=np.float32)
    root_rotations = np.tile(
        np.array([0, 0, 0, 1], dtype=np.float32), (num_frames, 1))

    # FK order restricted to bones we actually found
    fk_order = [b for b in MIXAMO_FK_ORDER if b in mx_nodes]

    for fi in range(num_frames):
        t = stack.time_begin + fi * frame_time

        # Evaluate local transforms for all FK bones
        local_rots = {}
        local_trans = {}
        for bone_name in fk_order:
            node = mx_nodes[bone_name]
            xf = ufbx.evaluate_transform(anim, node, t)
            local_rots[bone_name] = np.array(
                [xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w],
                dtype=np.float64)
            local_trans[bone_name] = np.array(
                [xf.translation.x, xf.translation.y, xf.translation.z],
                dtype=np.float64)

        # Compute world transforms via FK
        world_rots = {}
        world_pos = {}
        for bone_name in fk_order:
            parent_name = MIXAMO_HIERARCHY[bone_name]
            lr = quat_normalize(local_rots[bone_name])
            lt = local_trans[bone_name]

            if parent_name is None or parent_name not in world_rots:
                world_rots[bone_name] = lr
                world_pos[bone_name] = lt.copy()
            else:
                pr = world_rots[parent_name]
                pp = world_pos[parent_name]
                world_rots[bone_name] = quat_normalize(quat_multiply(pr, lr))
                world_pos[bone_name] = pp + quat_rotate_vec3(pr, lt)

        # Extract root (Hips)
        if "Hips" in world_pos:
            root_positions[fi] = (world_pos["Hips"] * total_scale).astype(np.float32)
            root_rotations[fi] = world_rots["Hips"].astype(np.float32)

        # Extract per training joint
        for ji, t_name in enumerate(TRAINING_JOINT_NAMES):
            mx_name = tmap.get(t_name)
            if mx_name is None or mx_name not in world_pos:
                continue
            joint_rotations[fi, ji] = quat_normalize(
                local_rots[mx_name]).astype(np.float32)
            joint_positions[fi, ji] = (
                world_pos[mx_name] * total_scale).astype(np.float32)

        if fi > 0 and fi % 500 == 0:
            logger.info("  Frame %d/%d", fi, num_frames)

    logger.info("  Done: %d frames", num_frames)

    return {
        "joint_rotations": joint_rotations,
        "joint_positions": joint_positions,
        "root_positions": root_positions,
        "root_rotations": root_rotations,
        "fps": np.float32(target_fps),
        "joint_names": TRAINING_JOINT_NAMES,
    }


def save_training_data(data, output_path):
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    np.save(str(output_path), data, allow_pickle=True)
    logger.info("Saved: %s", output_path)


def convert_file(fbx_path, output_dir, retarget_map_path=None, scale=1.0,
                 target_fps=30.0):
    data = convert_fbx(fbx_path, retarget_map_path, scale, target_fps)
    if data is None:
        return False
    out = Path(output_dir) / (Path(fbx_path).stem + ".npy")
    save_training_data(data, out)
    return True


# ============================================================================
# CLI
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Convert FBX animation files to .npy training format",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Convert all FBX files in a directory:
    python tools/convert_fbx_to_training.py assets/characters/fbx/ data/calm/motions/ \\
        --retarget data/calm/retarget_map.json --scale 0.01

  Convert a single FBX file:
    python tools/convert_fbx_to_training.py "assets/characters/fbx/sword and shield idle.fbx" \\
        data/calm/motions/ --retarget data/calm/retarget_map.json --scale 0.01
        """,
    )
    parser.add_argument("input", type=Path,
                        help="Input FBX file or directory of FBX files")
    parser.add_argument("output", type=Path,
                        help="Output directory for .npy files")
    parser.add_argument("--retarget", type=Path, default=None,
                        help="Path to retarget_map.json (optional)")
    parser.add_argument("--scale", type=float, default=0.01,
                        help="Position scale factor (default: 0.01 for cm->m)")
    parser.add_argument("--fps", type=float, default=30.0,
                        help="Target sample rate (default: 30)")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose logging")

    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    if args.retarget and not args.retarget.exists():
        logger.error("Retarget map not found: %s", args.retarget)
        sys.exit(1)

    retarget_path = str(args.retarget) if args.retarget else None

    # Collect input files
    if args.input.is_file():
        fbx_files = [args.input]
    elif args.input.is_dir():
        fbx_files = sorted(args.input.glob("**/*.fbx"))
        # Exclude the Y Bot mesh-only file (no animations typically)
        fbx_files = [f for f in fbx_files if f.name != "Y Bot.fbx"]
        if not fbx_files:
            logger.error("No FBX files found in %s", args.input)
            sys.exit(1)
        logger.info("Found %d FBX animation files", len(fbx_files))
    else:
        logger.error("Input not found: %s", args.input)
        sys.exit(1)

    # Convert all
    ok = 0
    fail = 0
    for i, fbx_path in enumerate(fbx_files):
        logger.info("[%d/%d] %s", i + 1, len(fbx_files), fbx_path.name)
        try:
            if convert_file(fbx_path, args.output, retarget_path, args.scale,
                            args.fps):
                ok += 1
            else:
                fail += 1
        except Exception as e:
            logger.error("Failed: %s — %s", fbx_path, e)
            fail += 1

    logger.info("Complete: %d succeeded, %d failed out of %d",
                ok, fail, len(fbx_files))
    if fail > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
