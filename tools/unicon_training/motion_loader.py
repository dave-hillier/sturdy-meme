"""Load motion data from BVH and FBX files for training.

Converts skeletal animation into the dict format expected by UniConEnv:
    {
        "clip_name": {
            "fps": float,
            "frames": [
                {
                    "root_pos": ndarray (3,),
                    "root_rot": ndarray (4,),  # w,x,y,z
                    "joint_positions": ndarray (J, 3),
                    "joint_rotations": ndarray (J, 4),
                },
                ...
            ]
        },
        ...
    }
"""

import json
import numpy as np
from pathlib import Path
from typing import Dict, Any, Optional


def load_bvh(path: str, num_joints: int = 20) -> Dict[str, Any]:
    """Load a BVH motion capture file.

    BVH files contain a hierarchy section and a motion section.
    This parser handles the standard BVH format used by CMU Mocap.
    """
    with open(path, "r") as f:
        content = f.read()

    lines = content.strip().split("\n")
    idx = 0

    # Parse hierarchy to get joint names and offsets
    joint_names = []
    joint_offsets = []
    joint_parents = []
    parent_stack = [-1]

    while idx < len(lines):
        line = lines[idx].strip()
        if line.startswith("ROOT") or line.startswith("JOINT"):
            name = line.split()[-1]
            joint_names.append(name)
            joint_parents.append(parent_stack[-1])
        elif line.startswith("End Site"):
            # Skip end site blocks
            idx += 1
            while idx < len(lines) and "}" not in lines[idx]:
                idx += 1
            idx += 1
            continue
        elif line.startswith("OFFSET"):
            parts = line.split()
            joint_offsets.append([float(parts[1]), float(parts[2]), float(parts[3])])
        elif line == "{":
            if joint_names:
                parent_stack.append(len(joint_names) - 1)
        elif line == "}":
            parent_stack.pop()
        elif line.startswith("MOTION"):
            idx += 1
            break
        idx += 1

    # Parse motion data
    num_frames = 0
    frame_time = 1.0 / 30.0

    while idx < len(lines):
        line = lines[idx].strip()
        if line.startswith("Frames:"):
            num_frames = int(line.split(":")[1].strip())
        elif line.startswith("Frame Time:"):
            frame_time = float(line.split(":")[1].strip())
            idx += 1
            break
        idx += 1

    fps = 1.0 / frame_time
    frames = []

    for frame_idx in range(num_frames):
        if idx >= len(lines):
            break
        values = [float(v) for v in lines[idx].strip().split()]
        idx += 1

        # First 3 values are root translation, then 3 rotation values per joint
        root_pos = np.array(values[0:3], dtype=np.float32)
        # Convert from BVH coordinate system (Z-up) to Y-up if needed
        # Standard BVH: X-right, Y-up, Z-forward
        root_pos = root_pos * 0.01  # cm to m

        # Parse rotations (Euler angles in degrees)
        rot_offset = 3
        joint_rotations = np.zeros((num_joints, 4), dtype=np.float32)
        joint_rotations[:, 0] = 1.0  # identity quaternion w

        for j in range(min(len(joint_names), num_joints)):
            if rot_offset + 3 <= len(values):
                # BVH typically uses ZXY or ZYX order
                rx = np.radians(values[rot_offset])
                ry = np.radians(values[rot_offset + 1])
                rz = np.radians(values[rot_offset + 2])
                joint_rotations[j] = _euler_to_quat(rx, ry, rz)
                rot_offset += 3

        # Compute joint positions from offsets and rotations (forward kinematics)
        joint_positions = np.zeros((num_joints, 3), dtype=np.float32)
        joint_positions[0] = root_pos
        offsets = np.array(joint_offsets[:num_joints], dtype=np.float32) * 0.01

        for j in range(1, min(len(joint_names), num_joints)):
            parent = joint_parents[j]
            if parent >= 0 and parent < num_joints:
                joint_positions[j] = joint_positions[parent] + offsets[j]

        frames.append({
            "root_pos": root_pos,
            "root_rot": joint_rotations[0],
            "joint_positions": joint_positions,
            "joint_rotations": joint_rotations,
        })

    return {"fps": fps, "frames": frames}


def load_motion_directory(
    motion_dir: str,
    num_joints: int = 20,
    extensions: tuple = (".bvh",),
) -> Dict[str, Dict[str, Any]]:
    """Load all motion files from a directory.

    Returns:
        Dictionary mapping clip names to motion data.
    """
    motion_dir = Path(motion_dir)
    motions = {}

    if not motion_dir.exists():
        return motions

    for ext in extensions:
        for filepath in motion_dir.glob(f"**/*{ext}"):
            name = filepath.stem
            try:
                if ext == ".bvh":
                    motions[name] = load_bvh(str(filepath), num_joints)
            except Exception as e:
                print(f"Warning: failed to load {filepath}: {e}")

    return motions


def generate_standing_motion(
    num_joints: int = 20,
    duration_seconds: float = 5.0,
    fps: float = 60.0,
) -> Dict[str, Any]:
    """Generate a simple standing-still motion for testing/bootstrap."""
    num_frames = int(duration_seconds * fps)
    frames = []

    for _ in range(num_frames):
        frames.append({
            "root_pos": np.array([0.0, 1.0, 0.0], dtype=np.float32),
            "root_rot": np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            "joint_positions": np.zeros((num_joints, 3), dtype=np.float32),
            "joint_rotations": np.tile(
                np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                (num_joints, 1),
            ),
        })

    return {"fps": fps, "frames": frames}


def _euler_to_quat(rx: float, ry: float, rz: float) -> np.ndarray:
    """Convert Euler angles (radians, ZYX order) to quaternion (w,x,y,z)."""
    cx, sx = np.cos(rx / 2), np.sin(rx / 2)
    cy, sy = np.cos(ry / 2), np.sin(ry / 2)
    cz, sz = np.cos(rz / 2), np.sin(rz / 2)

    w = cx * cy * cz + sx * sy * sz
    x = sx * cy * cz - cx * sy * sz
    y = cx * sy * cz + sx * cy * sz
    z = cx * cy * sz - sx * sy * cz

    return np.array([w, x, y, z], dtype=np.float32)
