#!/usr/bin/env python3
"""Convert BVH mocap files to the engine's training .npy format.

Parses BVH files (HIERARCHY + MOTION sections), builds joint hierarchy,
converts Euler rotations to quaternions (respecting per-joint CHANNELS order),
retargets from CMU skeleton to engine skeleton using retarget_map.json,
computes forward kinematics for global joint positions, and outputs .npy files.

Output .npy (saved with np.save, allow_pickle=True) contains a dict:
    joint_rotations: (num_frames, num_joints, 4) float32 - local quaternions [x,y,z,w]
    joint_positions: (num_frames, num_joints, 3) float32 - global positions
    root_positions:  (num_frames, 3)             float32 - root world position
    root_rotations:  (num_frames, 4)             float32 - root world rotation [x,y,z,w]
    fps:             float32 scalar
    joint_names:     list of str - joint names in training order

Usage:
    python tools/convert_mocap_to_training.py data/mocap/cmu/ data/calm/motions/ \\
        --retarget data/calm/retarget_map.json

    python tools/convert_mocap_to_training.py single_file.bvh data/calm/motions/ \\
        --retarget data/calm/retarget_map.json
"""

import argparse
import json
import logging
import sys
from pathlib import Path

import numpy as np

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger(__name__)

# ============================================================================
# Quaternion math (numpy, [x,y,z,w] convention matching GLM/engine)
# ============================================================================


def quat_identity():
    """Return identity quaternion [x,y,z,w]."""
    return np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float64)


def quat_multiply(q1, q2):
    """Hamilton product q1 * q2, both in [x,y,z,w] format."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return np.array([
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ], dtype=np.float64)


def quat_normalize(q):
    """Normalize a quaternion, returning identity if degenerate."""
    n = np.linalg.norm(q)
    if n < 1e-12:
        return quat_identity()
    return q / n


def quat_from_axis_angle(axis, angle_rad):
    """Create quaternion from axis and angle (radians). Returns [x,y,z,w]."""
    half = angle_rad * 0.5
    s = np.sin(half)
    c = np.cos(half)
    a = np.asarray(axis, dtype=np.float64)
    norm = np.linalg.norm(a)
    if norm < 1e-12:
        return quat_identity()
    a = a / norm
    return np.array([a[0] * s, a[1] * s, a[2] * s, c], dtype=np.float64)


def quat_from_euler_channels(angles_deg, channel_names):
    """Convert Euler angles to quaternion following BVH channel order.

    BVH applies rotations in the order listed in the CHANNELS line.
    Each rotation is applied as a post-multiplication (intrinsic/local axes).

    Args:
        angles_deg: list/array of rotation values in degrees
        channel_names: list of channel name strings, e.g. ['Zrotation','Xrotation','Yrotation']

    Returns:
        Quaternion [x,y,z,w].
    """
    q = quat_identity()
    axis_map = {
        'xrotation': np.array([1, 0, 0], dtype=np.float64),
        'yrotation': np.array([0, 1, 0], dtype=np.float64),
        'zrotation': np.array([0, 0, 1], dtype=np.float64),
    }
    for angle_deg, ch_name in zip(angles_deg, channel_names):
        key = ch_name.lower()
        if key not in axis_map:
            continue
        r = quat_from_axis_angle(axis_map[key], np.radians(float(angle_deg)))
        q = quat_multiply(q, r)
    return quat_normalize(q)


def quat_to_mat3(q):
    """Convert quaternion [x,y,z,w] to 3x3 rotation matrix (column-major convention)."""
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
    """Rotate 3D vector v by quaternion q."""
    m = quat_to_mat3(q)
    return m @ np.asarray(v, dtype=np.float64)


# ============================================================================
# BVH Parser
# ============================================================================


class BVHJoint:
    """A joint in the BVH hierarchy."""

    def __init__(self, name, parent_index=-1):
        self.name = name
        self.parent_index = parent_index
        self.offset = np.zeros(3, dtype=np.float64)
        self.channels = []          # e.g. ['Zrotation', 'Xrotation', 'Yrotation']
        self.children_indices = []
        self.channel_start = 0      # Index into the flat per-frame channel array
        self.is_end_site = False


class BVHData:
    """Parsed BVH file data."""

    def __init__(self):
        self.joints = []            # All joints (real + end sites filtered later)
        self.frame_time = 1.0 / 30.0
        self.num_frames = 0
        self.channel_data = None    # (num_frames, total_channels) float64

    @property
    def fps(self):
        if self.frame_time > 0:
            return 1.0 / self.frame_time
        return 30.0

    @property
    def real_joints(self):
        return [j for j in self.joints if not j.is_end_site]


def parse_bvh(filepath):
    """Parse a BVH file into a BVHData structure.

    Args:
        filepath: Path to the .bvh file.

    Returns:
        BVHData with hierarchy and per-frame motion data.
    """
    with open(filepath, "r") as f:
        lines = f.readlines()

    bvh = BVHData()
    idx = 0
    stack = []           # Stack of joint indices for nesting
    channel_offset = 0

    # ---- Parse HIERARCHY ----
    while idx < len(lines):
        line = lines[idx].strip()
        idx += 1

        if line == "HIERARCHY":
            continue

        tokens = line.split()
        if not tokens:
            continue

        if tokens[0] in ("ROOT", "JOINT"):
            name = tokens[1] if len(tokens) > 1 else "unnamed"
            parent_idx = stack[-1] if stack else -1
            joint = BVHJoint(name, parent_idx)
            joint_idx = len(bvh.joints)
            if parent_idx >= 0:
                bvh.joints[parent_idx].children_indices.append(joint_idx)
            bvh.joints.append(joint)
            # The matching '{' will push this joint onto the stack
            continue

        if tokens[0] == "End" and len(tokens) > 1 and tokens[1] == "Site":
            parent_idx = stack[-1] if stack else -1
            parent_name = bvh.joints[parent_idx].name if parent_idx >= 0 else "root"
            joint = BVHJoint(f"{parent_name}_End", parent_idx)
            joint.is_end_site = True
            joint_idx = len(bvh.joints)
            if parent_idx >= 0:
                bvh.joints[parent_idx].children_indices.append(joint_idx)
            bvh.joints.append(joint)
            continue

        if tokens[0] == "{":
            # Push the most recently added joint
            stack.append(len(bvh.joints) - 1)
            continue

        if tokens[0] == "}":
            if stack:
                stack.pop()
            continue

        if tokens[0] == "OFFSET" and len(tokens) >= 4:
            if stack:
                joint = bvh.joints[stack[-1]]
                joint.offset = np.array(
                    [float(tokens[1]), float(tokens[2]), float(tokens[3])],
                    dtype=np.float64,
                )
            continue

        if tokens[0] == "CHANNELS" and len(tokens) >= 2:
            if stack:
                joint = bvh.joints[stack[-1]]
                num_ch = int(tokens[1])
                joint.channels = tokens[2:2 + num_ch]
                joint.channel_start = channel_offset
                channel_offset += num_ch
            continue

        if tokens[0] == "MOTION":
            break

    total_channels = channel_offset

    # ---- Parse MOTION ----
    while idx < len(lines):
        line = lines[idx].strip()
        idx += 1

        if line.startswith("Frames:"):
            bvh.num_frames = int(line.split(":")[1].strip())
            continue

        if line.startswith("Frame Time:"):
            bvh.frame_time = float(line.split(":")[1].strip())
            continue

        # First line of actual frame data
        if line and not line.startswith("Frames") and not line.startswith("Frame"):
            frame_lines = [line]
            while idx < len(lines):
                fline = lines[idx].strip()
                idx += 1
                if fline:
                    frame_lines.append(fline)

            bvh.num_frames = len(frame_lines)
            bvh.channel_data = np.zeros(
                (bvh.num_frames, total_channels), dtype=np.float64
            )
            for fi, fl in enumerate(frame_lines):
                values = [float(x) for x in fl.split()]
                n = min(len(values), total_channels)
                bvh.channel_data[fi, :n] = values[:n]
            break

    return bvh


# ============================================================================
# Forward Kinematics
# ============================================================================


def _has_position_channels(joint):
    """Check whether a joint has position channels."""
    return any("position" in ch.lower() for ch in joint.channels)


def compute_fk(bvh, frame_idx):
    """Compute forward kinematics for all real joints at a given frame.

    Args:
        bvh: BVHData
        frame_idx: int

    Returns:
        local_rotations:  list of (4,) quaternions [x,y,z,w], one per real joint
        global_positions: list of (3,) vectors, one per real joint
        global_rotations: list of (4,) quaternions [x,y,z,w], one per real joint
    """
    real_joints = bvh.real_joints
    row = bvh.channel_data[frame_idx]

    # Map joint object -> real-joint list index
    joint_to_real_idx = {}
    for ri, j in enumerate(real_joints):
        joint_to_real_idx[id(j)] = ri

    # Also need a global map for parent lookup (parent might be an end-site, though unlikely)
    all_joint_to_real_idx = {}
    for ri, j in enumerate(real_joints):
        # Map by the original index in bvh.joints
        for ai, aj in enumerate(bvh.joints):
            if aj is j:
                all_joint_to_real_idx[ai] = ri
                break

    local_rotations = []
    global_positions = []
    global_rotations = []

    for ri, joint in enumerate(real_joints):
        # Extract rotation channels
        rot_angles = []
        rot_names = []
        translation = joint.offset.copy()

        for ci, ch in enumerate(joint.channels):
            val = row[joint.channel_start + ci]
            ch_lower = ch.lower()
            if ch_lower == "xposition":
                translation[0] = val
            elif ch_lower == "yposition":
                translation[1] = val
            elif ch_lower == "zposition":
                translation[2] = val
            elif "rotation" in ch_lower:
                rot_angles.append(val)
                rot_names.append(ch)

        # Local rotation
        if rot_angles:
            local_rot = quat_from_euler_channels(rot_angles, rot_names)
        else:
            local_rot = quat_identity()
        local_rotations.append(local_rot)

        # FK chain
        parent_real_idx = all_joint_to_real_idx.get(joint.parent_index, -1)
        if parent_real_idx < 0 or joint.parent_index < 0:
            # Root joint
            global_rotations.append(quat_normalize(local_rot))
            global_positions.append(translation.copy())
        else:
            parent_rot = global_rotations[parent_real_idx]
            parent_pos = global_positions[parent_real_idx]
            g_rot = quat_normalize(quat_multiply(parent_rot, local_rot))
            g_pos = parent_pos + quat_rotate_vec3(parent_rot, translation)
            global_rotations.append(g_rot)
            global_positions.append(g_pos)

    return local_rotations, global_positions, global_rotations


# ============================================================================
# CMU BVH name mapping
# ============================================================================

# Maps common CMU BVH joint names to the engine's bone names (as used in
# the skeleton / CharacterConfig). Multiple CMU variants map to the same
# engine name; first match wins during retargeting.
CMU_NAME_TO_ENGINE = {
    # Root / spine
    "Hips": "Hips", "hip": "Hips", "hips": "Hips",
    "Spine": "Spine", "spine": "Spine", "LowerBack": "Spine",
    "Spine1": "Spine1", "Spine2": "Spine1", "Chest": "Spine1", "chest": "Spine1",
    "Neck": "Neck", "Neck1": "Neck", "neck": "Neck",
    "Head": "Head", "head": "Head",
    # Right arm
    "RightShoulder": "RightArm", "RightArm": "RightArm", "RightUpArm": "RightArm",
    "RightForeArm": "RightForeArm", "RightLowArm": "RightForeArm",
    "RightHand": "RightHand",
    # Left arm
    "LeftShoulder": "LeftArm", "LeftArm": "LeftArm", "LeftUpArm": "LeftArm",
    "LeftForeArm": "LeftForeArm", "LeftLowArm": "LeftForeArm",
    "LeftHand": "LeftHand",
    # Right leg
    "RightUpLeg": "RightUpLeg", "RightThigh": "RightUpLeg", "RHipJoint": "RightUpLeg",
    "RightLeg": "RightLeg", "RightShin": "RightLeg",
    "RightFoot": "RightFoot",
    # Left leg
    "LeftUpLeg": "LeftUpLeg", "LeftThigh": "LeftUpLeg", "LHipJoint": "LeftUpLeg",
    "LeftLeg": "LeftLeg", "LeftShin": "LeftLeg",
    "LeftFoot": "LeftFoot",
}


def map_bvh_name_to_engine(bvh_name):
    """Map a BVH joint name to the engine's canonical name, or None."""
    if bvh_name in CMU_NAME_TO_ENGINE:
        return CMU_NAME_TO_ENGINE[bvh_name]
    # Case-insensitive fallback
    lower = bvh_name.lower()
    for key, val in CMU_NAME_TO_ENGINE.items():
        if key.lower() == lower:
            return val
    return None


# ============================================================================
# Training joint ordering
# ============================================================================

# This MUST match the order in CharacterConfig::getHumanoidBoneDefs().
# Each entry: (training_name, engine_bone_name, num_dofs, is_key_body)
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

# Training joint names in order (for output joint_names list)
TRAINING_JOINT_NAMES = [name for name, _, _, _ in HUMANOID_BONE_DEFS]

# Total DOFs (sum of num_dofs for all bones)
TOTAL_DOFS = sum(ndof for _, _, ndof, _ in HUMANOID_BONE_DEFS)  # 37

# Key bodies (for observation key body positions)
KEY_BODY_NAMES = [name for name, _, _, is_key in HUMANOID_BONE_DEFS if is_key]  # 5


# ============================================================================
# Retargeting
# ============================================================================


def load_retarget_map(path):
    """Load retarget_map.json.

    Returns:
        training_to_engine: dict mapping training name -> engine bone name
        scale_factor: float
    """
    with open(path, "r") as f:
        data = json.load(f)
    return data["training_to_engine_joint_map"], data.get("scale_factor", 1.0)


def build_bvh_to_training_map(bvh, retarget_map_path=None):
    """Build a mapping from training bone names to BVH real-joint indices.

    Uses the CMU name table and optionally a retarget_map.json.

    Args:
        bvh: BVHData
        retarget_map_path: Optional path to retarget_map.json

    Returns:
        Dict mapping training bone name -> BVH real-joint index (or -1)
    """
    real_joints = bvh.real_joints

    # Build engine_name -> BVH real-joint index (first match wins)
    engine_to_bvh_idx = {}
    for ri, joint in enumerate(real_joints):
        engine_name = map_bvh_name_to_engine(joint.name)
        if engine_name and engine_name not in engine_to_bvh_idx:
            engine_to_bvh_idx[engine_name] = ri

    # Build training_name -> engine_name from HUMANOID_BONE_DEFS
    # (this is the canonical mapping, not the retarget_map.json one)
    training_to_engine = {name: eng for name, eng, _, _ in HUMANOID_BONE_DEFS}

    # Override with retarget_map.json if provided
    if retarget_map_path:
        custom_map, _ = load_retarget_map(retarget_map_path)
        for t_name, e_name in custom_map.items():
            if t_name in training_to_engine:
                training_to_engine[t_name] = e_name

    # Now map training_name -> BVH index
    mapping = {}
    for training_name in TRAINING_JOINT_NAMES:
        engine_name = training_to_engine.get(training_name)
        if engine_name and engine_name in engine_to_bvh_idx:
            mapping[training_name] = engine_to_bvh_idx[engine_name]
        else:
            mapping[training_name] = -1
    return mapping


# ============================================================================
# Conversion
# ============================================================================


def convert_bvh(bvh_path, retarget_map_path=None, scale=1.0):
    """Convert a single BVH file to the training dict format.

    Args:
        bvh_path: Path to BVH file.
        retarget_map_path: Optional path to retarget_map.json.
        scale: Additional position scale factor.

    Returns:
        Dict with training arrays, or None on failure.
    """
    logger.info("Parsing: %s", bvh_path)
    bvh = parse_bvh(str(bvh_path))

    if bvh.num_frames == 0 or bvh.channel_data is None:
        logger.error("No frame data in %s", bvh_path)
        return None

    # Apply retarget map scale
    map_scale = 1.0
    if retarget_map_path:
        _, map_scale = load_retarget_map(retarget_map_path)
    total_scale = scale * map_scale

    logger.info("  Joints: %d real, Frames: %d, FPS: %.1f",
                len(bvh.real_joints), bvh.num_frames, bvh.fps)

    # Build retarget mapping
    tmap = build_bvh_to_training_map(bvh, retarget_map_path)

    found = [n for n, i in tmap.items() if i >= 0]
    missing = [n for n, i in tmap.items() if i < 0]
    logger.info("  Mapped %d/%d training joints", len(found), len(TRAINING_JOINT_NAMES))
    if missing:
        logger.warning("  Missing: %s", ", ".join(missing))

    num_frames = bvh.num_frames
    num_joints = len(TRAINING_JOINT_NAMES)

    # Allocate output
    joint_rotations = np.tile(
        np.array([0, 0, 0, 1], dtype=np.float32),
        (num_frames, num_joints, 1),
    )  # (F, J, 4) identity quats
    joint_positions = np.zeros((num_frames, num_joints, 3), dtype=np.float32)
    root_positions = np.zeros((num_frames, 3), dtype=np.float32)
    root_rotations = np.tile(
        np.array([0, 0, 0, 1], dtype=np.float32), (num_frames, 1)
    )

    root_bvh_idx = tmap.get("pelvis", -1)

    for fi in range(num_frames):
        local_rots, global_pos, global_rots = compute_fk(bvh, fi)

        # Root
        if root_bvh_idx >= 0:
            root_positions[fi] = (global_pos[root_bvh_idx] * total_scale).astype(np.float32)
            root_rotations[fi] = global_rots[root_bvh_idx].astype(np.float32)
        elif len(global_pos) > 0:
            root_positions[fi] = (global_pos[0] * total_scale).astype(np.float32)
            root_rotations[fi] = global_rots[0].astype(np.float32)

        # Per training joint
        for ji, t_name in enumerate(TRAINING_JOINT_NAMES):
            bvh_idx = tmap.get(t_name, -1)
            if bvh_idx >= 0 and bvh_idx < len(local_rots):
                joint_rotations[fi, ji] = local_rots[bvh_idx].astype(np.float32)
                joint_positions[fi, ji] = (global_pos[bvh_idx] * total_scale).astype(np.float32)

        if fi > 0 and fi % 1000 == 0:
            logger.info("  Frame %d/%d", fi, num_frames)

    logger.info("  Done: %d frames", num_frames)

    return {
        "joint_rotations": joint_rotations,
        "joint_positions": joint_positions,
        "root_positions": root_positions,
        "root_rotations": root_rotations,
        "fps": np.float32(bvh.fps),
        "joint_names": TRAINING_JOINT_NAMES,
    }


def save_training_data(data, output_path):
    """Save training data dict to .npy."""
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    np.save(str(output_path), data, allow_pickle=True)
    logger.info("Saved: %s", output_path)


def convert_file(bvh_path, output_dir, retarget_map_path=None, scale=1.0):
    """Convert one BVH file and save as .npy.

    Returns True on success.
    """
    data = convert_bvh(bvh_path, retarget_map_path, scale)
    if data is None:
        return False
    out = Path(output_dir) / (Path(bvh_path).stem + ".npy")
    save_training_data(data, out)
    return True


# ============================================================================
# CLI
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Convert BVH mocap files to .npy training format",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Convert a directory of BVH files:
    python tools/convert_mocap_to_training.py data/mocap/cmu/ data/calm/motions/ \\
        --retarget data/calm/retarget_map.json

  Convert a single BVH file:
    python tools/convert_mocap_to_training.py walk.bvh data/calm/motions/ \\
        --retarget data/calm/retarget_map.json
        """,
    )
    parser.add_argument("input", type=Path,
                        help="Input BVH file or directory of BVH files")
    parser.add_argument("output", type=Path,
                        help="Output directory for .npy files")
    parser.add_argument("--retarget", type=Path, required=True,
                        help="Path to retarget_map.json")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="Additional position scale factor (default: 1.0)")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose logging")

    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    if not args.retarget.exists():
        logger.error("Retarget map not found: %s", args.retarget)
        sys.exit(1)

    # Collect input files
    if args.input.is_file():
        bvh_files = [args.input]
    elif args.input.is_dir():
        bvh_files = sorted(args.input.glob("**/*.bvh"))
        if not bvh_files:
            logger.error("No BVH files found in %s", args.input)
            sys.exit(1)
        logger.info("Found %d BVH files", len(bvh_files))
    else:
        logger.error("Input not found: %s", args.input)
        sys.exit(1)

    # Convert all
    ok = 0
    fail = 0
    for i, bvh_path in enumerate(bvh_files):
        logger.info("[%d/%d] %s", i + 1, len(bvh_files), bvh_path.name)
        try:
            if convert_file(bvh_path, args.output, str(args.retarget), args.scale):
                ok += 1
            else:
                fail += 1
        except Exception as e:
            logger.error("Failed: %s — %s", bvh_path, e)
            fail += 1

    logger.info("Complete: %d succeeded, %d failed out of %d", ok, fail, len(bvh_files))
    if fail > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
