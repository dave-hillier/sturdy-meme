#!/usr/bin/env python3
"""Motion dataset manager for AMP/CALM training.

Loads converted .npy motion clips, provides random frame sampling for the
AMP discriminator, and computes AMP observation vectors matching the C++
ObservationExtractor exactly.

Classes:
    MotionClip:             Single motion clip loaded from .npy
    MotionDataset:          Collection of clips with weighted random sampling
    AMPObservationComputer: Computes the observation vector from motion frames

CLI usage:
    # Generate a YAML manifest from a directory of .npy files
    python tools/motion_dataset.py --generate-manifest data/calm/motions/ \\
        --output data/calm/motions/manifest.yaml

    # Validate a manifest (check all files exist, shapes are consistent)
    python tools/motion_dataset.py --validate data/calm/motions/manifest.yaml

    # Print observation layout info
    python tools/motion_dataset.py --info
"""

import argparse
import logging
import math
import sys
from pathlib import Path

import numpy as np

try:
    import yaml
except ImportError:
    yaml = None

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
logger = logging.getLogger(__name__)


# ============================================================================
# Bone / DOF definitions — MUST match CharacterConfig::getHumanoidBoneDefs()
# ============================================================================

# Each entry: (training_name, engine_bone_name, num_dofs, is_key_body)
# This defines the canonical ordering for joints, DOFs, and key bodies.
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
TOTAL_DOFS = sum(ndof for _, _, ndof, _ in HUMANOID_BONE_DEFS)  # 37
KEY_BODY_NAMES = [name for name, _, _, is_key in HUMANOID_BONE_DEFS if is_key]  # 5
NUM_KEY_BODIES = len(KEY_BODY_NAMES)

# Observation dim: root_h(1) + rot6d(6) + vel(3) + angvel(3)
#                  + dof_pos(37) + dof_vel(37) + key_body(5*3)
OBS_DIM = 1 + 6 + 3 + 3 + TOTAL_DOFS + TOTAL_DOFS + NUM_KEY_BODIES * 3  # 102

# DOF mapping: list of (joint_index_in_TRAINING_JOINT_NAMES, axis) pairs.
# Axes: 0=X, 1=Y, 2=Z. Joints with N DOFs contribute axes [0..N-1].
DOF_MAPPINGS = []
for _ji, (_, _, _ndof, _) in enumerate(HUMANOID_BONE_DEFS):
    for _ax in range(_ndof):
        DOF_MAPPINGS.append((_ji, _ax))

# Key body joint indices (into TRAINING_JOINT_NAMES)
KEY_BODY_INDICES = [
    i for i, (_, _, _, is_key) in enumerate(HUMANOID_BONE_DEFS) if is_key
]


# ============================================================================
# Quaternion / rotation math (numpy, [x,y,z,w] convention matching GLM)
# ============================================================================


def quat_identity():
    return np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float64)


def quat_multiply(q1, q2):
    """Hamilton product q1 * q2. Both in [x,y,z,w]."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return np.array([
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ], dtype=np.float64)


def quat_inverse(q):
    """Conjugate (inverse for unit quaternion)."""
    return np.array([-q[0], -q[1], -q[2], q[3]], dtype=np.float64)


def quat_normalize(q):
    n = np.linalg.norm(q)
    if n < 1e-12:
        return quat_identity()
    return q / n


def quat_to_mat3(q):
    """Quaternion [x,y,z,w] -> 3x3 rotation matrix.

    The resulting matrix is row-indexed: M[row][col].
    GLM stores column-major, but the element values are the same.
    """
    x, y, z, w = q
    x2, y2, z2 = x * 2.0, y * 2.0, z * 2.0
    xx, xy, xz = x * x2, x * y2, x * z2
    yy, yz, zz = y * y2, y * z2, z * z2
    wx, wy, wz = w * x2, w * y2, w * z2
    return np.array([
        [1.0 - (yy + zz), xy - wz,         xz + wy],
        [xy + wz,         1.0 - (xx + zz), yz - wx],
        [xz - wy,         yz + wx,         1.0 - (xx + yy)],
    ], dtype=np.float64)


def quat_rotate_vec3(q, v):
    """Rotate vector v by quaternion q."""
    return quat_to_mat3(q) @ np.asarray(v, dtype=np.float64)


def quat_from_axis_angle(axis, angle):
    half = angle * 0.5
    s = math.sin(half)
    c = math.cos(half)
    a = np.asarray(axis, dtype=np.float64)
    n = np.linalg.norm(a)
    if n < 1e-12:
        return quat_identity()
    a = a / n
    return np.array([a[0] * s, a[1] * s, a[2] * s, c], dtype=np.float64)


# ============================================================================
# Observation math — matches C++ ObservationExtractor exactly
# ============================================================================


def get_heading_angle(q):
    """Project q * (0,0,1) onto XZ plane, return atan2(fwd.x, fwd.z).

    Matches C++ ObservationExtractor::getHeadingAngle.
    """
    forward = quat_rotate_vec3(q, np.array([0.0, 0.0, 1.0]))
    return math.atan2(forward[0], forward[2])


def remove_heading(q):
    """Remove heading from quaternion: angleAxis(-heading, Y) * q.

    Matches C++ ObservationExtractor::removeHeading.
    """
    heading = get_heading_angle(q)
    heading_quat = quat_from_axis_angle(np.array([0.0, 1.0, 0.0]), -heading)
    return quat_normalize(quat_multiply(heading_quat, q))


def quat_to_tan_norm_6d(q):
    """Convert quaternion to 6D representation: first two columns of rotation matrix.

    GLM is column-major: m[col][row]. In C++ code:
        out[0] = m[0][0], out[1] = m[0][1], out[2] = m[0][2]  -> column 0
        out[3] = m[1][0], out[4] = m[1][1], out[5] = m[1][2]  -> column 1

    In our row-major numpy matrix, column k = M[:, k].

    Matches C++ ObservationExtractor::quatToTanNorm6D.
    """
    m = quat_to_mat3(q)
    col0 = m[:, 0]  # [m[0,0], m[1,0], m[2,0]]
    col1 = m[:, 1]  # [m[0,1], m[1,1], m[2,1]]
    return np.array([
        col0[0], col0[1], col0[2],
        col1[0], col1[1], col1[2],
    ], dtype=np.float64)


def mat3_to_euler_xyz(m):
    """Extract Euler XYZ angles from 3x3 rotation matrix.

    Matches C++ ObservationExtractor::matrixToEulerXYZ.

    IMPORTANT: GLM is column-major. C++ accesses m[col][row].
    In our row-major numpy, C++'s m[c][r] corresponds to our m[r][c].

    C++ code:
        sy = m[0][2]     -> col=0, row=2 -> our m[2,0]
        atan2(-m[1][2], m[2][2]) -> col=1,row=2 and col=2,row=2
                                 -> our m[2,1] and m[2,2]
        asin(sy)
        atan2(-m[0][1], m[0][0]) -> col=0,row=1 and col=0,row=0
                                 -> our m[1,0] and m[0,0]
    Gimbal:
        atan2(m[2][1], m[1][1]) -> col=2,row=1 and col=1,row=1
                                -> our m[1,2] and m[1,1]
    """
    sy = m[2, 0]   # C++: m[0][2]
    if abs(sy) < 0.99999:
        x = math.atan2(-m[2, 1], m[2, 2])  # C++: atan2(-m[1][2], m[2][2])
        y = math.asin(sy)                    # C++: asin(m[0][2])
        z = math.atan2(-m[1, 0], m[0, 0])  # C++: atan2(-m[0][1], m[0][0])
    else:
        # Gimbal lock
        x = math.atan2(m[1, 2], m[1, 1])   # C++: atan2(m[2][1], m[1][1])
        y = math.copysign(math.pi / 2.0, sy)
        z = 0.0
    return np.array([x, y, z], dtype=np.float64)


def rotate_to_heading_frame(vec, heading_angle):
    """Rotate world-space vector into heading frame.

    cosH = cos(-heading), sinH = sin(-heading)
    localX =  cosH * worldX + sinH * worldZ
    localY =  worldY
    localZ = -sinH * worldX + cosH * worldZ

    Matches C++ ObservationExtractor root velocity / key body rotation.
    """
    cosH = math.cos(-heading_angle)
    sinH = math.sin(-heading_angle)
    return np.array([
        cosH * vec[0] + sinH * vec[2],
        vec[1],
        -sinH * vec[0] + cosH * vec[2],
    ], dtype=np.float64)


# ============================================================================
# AMPObservationComputer
# ============================================================================


class AMPObservationComputer:
    """Computes AMP observation vectors from motion data.

    Exactly replicates C++ ObservationExtractor so Python training and
    C++ inference produce identical observations.

    Observation layout (OBS_DIM = 102):
        [0]            root height (1)
        [1..6]         root rotation, heading-invariant 6D (6)
        [7..9]         local root velocity in heading frame (3)
        [10..12]       local root angular velocity in heading frame (3)
        [13..49]       DOF positions (37)
        [50..86]       DOF velocities (37)
        [87..101]      key body positions in root-relative heading frame (5*3=15)
    """

    def __init__(self):
        self.obs_dim = OBS_DIM
        self.dof_mappings = DOF_MAPPINGS
        self.key_body_indices = KEY_BODY_INDICES
        self.num_dofs = TOTAL_DOFS

    def compute_frame(self, root_pos, root_rot, joint_local_rots, joint_global_pos,
                      prev_root_pos=None, prev_root_rot=None,
                      prev_joint_local_rots=None, dt=1.0 / 30.0):
        """Compute a single-frame AMP observation vector.

        Args:
            root_pos: (3,) root world position
            root_rot: (4,) root world rotation quaternion [x,y,z,w]
            joint_local_rots: (num_training_joints, 4) local quaternions
            joint_global_pos: (num_training_joints, 3) global positions
            prev_root_pos: (3,) or None (first frame)
            prev_root_rot: (4,) or None (first frame)
            prev_joint_local_rots: (num_training_joints, 4) or None
            dt: time delta between frames in seconds

        Returns:
            (obs_dim,) float64 observation vector
        """
        obs = np.zeros(self.obs_dim, dtype=np.float64)
        idx = 0

        root_pos = np.asarray(root_pos, dtype=np.float64)
        root_rot = quat_normalize(np.asarray(root_rot, dtype=np.float64))

        has_prev = (prev_root_pos is not None and prev_root_rot is not None
                    and dt > 0)

        # ---- 1) Root height (1) ----
        obs[idx] = root_pos[1]
        idx += 1

        # ---- 2) Root rotation — heading-invariant 6D (6) ----
        heading_free = remove_heading(root_rot)
        rot6d = quat_to_tan_norm_6d(heading_free)
        obs[idx:idx + 6] = rot6d
        idx += 6

        # ---- 3) Root velocity in heading frame (3) ----
        heading_angle = get_heading_angle(root_rot)

        if has_prev:
            prev_root_pos = np.asarray(prev_root_pos, dtype=np.float64)
            world_vel = (root_pos - prev_root_pos) / dt
            local_vel = rotate_to_heading_frame(world_vel, heading_angle)
        else:
            local_vel = np.zeros(3, dtype=np.float64)
        obs[idx:idx + 3] = local_vel
        idx += 3

        # ---- 4) Root angular velocity in heading frame (3) ----
        if has_prev:
            prev_root_rot = quat_normalize(np.asarray(prev_root_rot, dtype=np.float64))
            delta_rot = quat_normalize(
                quat_multiply(root_rot, quat_inverse(prev_root_rot))
            )
            w_clamped = float(np.clip(delta_rot[3], -1.0, 1.0))
            angle = 2.0 * math.acos(w_clamped)
            sin_half = math.sqrt(1.0 - w_clamped * w_clamped)
            if sin_half > 1e-6:
                axis = delta_rot[:3] / sin_half
            else:
                axis = np.array([0.0, 1.0, 0.0], dtype=np.float64)
            ang_vel_world = axis * (angle / dt)
            local_ang_vel = rotate_to_heading_frame(ang_vel_world, heading_angle)
        else:
            local_ang_vel = np.zeros(3, dtype=np.float64)
        obs[idx:idx + 3] = local_ang_vel
        idx += 3

        # ---- 5) DOF positions (N=37) ----
        dof_pos = self._extract_dof_positions(joint_local_rots)
        obs[idx:idx + self.num_dofs] = dof_pos
        idx += self.num_dofs

        # ---- 6) DOF velocities (N=37) ----
        if has_prev and prev_joint_local_rots is not None:
            prev_dof_pos = self._extract_dof_positions(prev_joint_local_rots)
            dof_vel = (dof_pos - prev_dof_pos) / dt
        else:
            dof_vel = np.zeros(self.num_dofs, dtype=np.float64)
        obs[idx:idx + self.num_dofs] = dof_vel
        idx += self.num_dofs

        # ---- 7) Key body positions in root-relative heading frame (K*3) ----
        for kb_ji in self.key_body_indices:
            world_pos = np.asarray(joint_global_pos[kb_ji], dtype=np.float64)
            rel_pos = world_pos - root_pos
            local_pos = rotate_to_heading_frame(rel_pos, heading_angle)
            obs[idx:idx + 3] = local_pos
            idx += 3

        assert idx == self.obs_dim, f"Size mismatch: {idx} != {self.obs_dim}"
        return obs

    def _extract_dof_positions(self, joint_local_rots):
        """Extract DOF angles from local joint rotations via Euler XYZ decomposition.

        Matches C++ ObservationExtractor::extractDOFFeatures.
        """
        dof_pos = np.zeros(self.num_dofs, dtype=np.float64)
        for d, (ji, axis) in enumerate(self.dof_mappings):
            q = quat_normalize(np.asarray(joint_local_rots[ji], dtype=np.float64))
            m = quat_to_mat3(q)
            euler = mat3_to_euler_xyz(m)
            dof_pos[d] = euler[axis]
        return dof_pos

    def compute_clip_observations(self, clip, fps=None):
        """Compute AMP observations for every frame of a MotionClip.

        Args:
            clip: MotionClip instance.
            fps: Override FPS (default: clip.fps).

        Returns:
            (num_frames, obs_dim) float32 array.
        """
        if fps is None:
            fps = clip.fps
        dt = 1.0 / fps if fps > 0 else 1.0 / 30.0

        observations = np.zeros((clip.num_frames, self.obs_dim), dtype=np.float32)

        for fi in range(clip.num_frames):
            prev_rp = clip.root_positions[fi - 1] if fi > 0 else None
            prev_rr = clip.root_rotations[fi - 1] if fi > 0 else None
            prev_lr = clip.joint_rotations[fi - 1] if fi > 0 else None

            obs = self.compute_frame(
                root_pos=clip.root_positions[fi],
                root_rot=clip.root_rotations[fi],
                joint_local_rots=clip.joint_rotations[fi],
                joint_global_pos=clip.joint_positions[fi],
                prev_root_pos=prev_rp,
                prev_root_rot=prev_rr,
                prev_joint_local_rots=prev_lr,
                dt=dt,
            )
            observations[fi] = obs.astype(np.float32)

        return observations


# ============================================================================
# MotionClip
# ============================================================================


class MotionClip:
    """A single motion clip loaded from a .npy file.

    Attributes:
        name:             str
        fps:              float
        num_frames:       int
        joint_rotations:  (num_frames, num_joints, 4) float32 [x,y,z,w]
        joint_positions:  (num_frames, num_joints, 3) float32
        root_positions:   (num_frames, 3) float32
        root_rotations:   (num_frames, 4) float32 [x,y,z,w]
        joint_names:      list of str
        tags:             list of str
        observations:     (num_frames, obs_dim) float32 or None (cached)
    """

    def __init__(self, name="", tags=None):
        self.name = name
        self.fps = 30.0
        self.num_frames = 0
        self.joint_rotations = None
        self.joint_positions = None
        self.root_positions = None
        self.root_rotations = None
        self.joint_names = []
        self.tags = tags or []
        self.observations = None

    @classmethod
    def from_npy(cls, path, tags=None):
        """Load a MotionClip from a .npy file (as saved by convert_mocap_to_training.py).

        Args:
            path: Path to .npy file.
            tags: Optional semantic tags.

        Returns:
            MotionClip instance.
        """
        path = Path(path)
        data = np.load(str(path), allow_pickle=True).item()

        clip = cls(name=path.stem, tags=tags)
        clip.joint_rotations = np.asarray(data["joint_rotations"], dtype=np.float32)
        clip.joint_positions = np.asarray(data["joint_positions"], dtype=np.float32)
        clip.root_positions = np.asarray(data["root_positions"], dtype=np.float32)
        clip.root_rotations = np.asarray(data["root_rotations"], dtype=np.float32)
        clip.fps = float(data.get("fps", 30.0))
        clip.num_frames = clip.joint_rotations.shape[0]

        jnames = data.get("joint_names", [])
        clip.joint_names = list(jnames) if not isinstance(jnames, list) else jnames

        return clip

    def compute_observations(self, computer=None):
        """Compute and cache AMP observations for all frames.

        Args:
            computer: AMPObservationComputer (created if None).

        Returns:
            (num_frames, obs_dim) float32 array.
        """
        if computer is None:
            computer = AMPObservationComputer()
        self.observations = computer.compute_clip_observations(self)
        return self.observations

    def duration(self):
        """Duration in seconds."""
        if self.num_frames > 0 and self.fps > 0:
            return self.num_frames / self.fps
        return 0.0


# ============================================================================
# MotionDataset
# ============================================================================


class MotionDataset:
    """Collection of motion clips with random sampling for AMP training.

    Supports loading from YAML manifest or directory, and provides
    batch sampling for discriminator training.
    """

    def __init__(self):
        self.clips = []
        self._total_frames = 0
        self._obs_computer = AMPObservationComputer()

    def add_clip(self, clip):
        """Add a clip to the dataset."""
        self.clips.append(clip)
        self._total_frames = 0  # invalidate cache

    @classmethod
    def from_manifest(cls, manifest_path):
        """Load from a YAML manifest.

        Expected YAML:
            motions:
              - file: walk_forward.npy
                fps: 30
                tags: [walk, locomotion]
        """
        if yaml is None:
            logger.error("PyYAML required. Install: pip install pyyaml")
            return None

        manifest_path = Path(manifest_path)
        with open(manifest_path, "r") as f:
            manifest = yaml.safe_load(f)

        base_dir = manifest_path.parent
        dataset = cls()

        for entry in manifest.get("motions", []):
            npy_path = base_dir / entry["file"]
            if not npy_path.exists():
                logger.warning("Clip not found, skipping: %s", npy_path)
                continue
            tags = entry.get("tags", [])
            try:
                clip = MotionClip.from_npy(npy_path, tags=tags)
                if "fps" in entry:
                    clip.fps = float(entry["fps"])
                dataset.add_clip(clip)
                logger.info("  Loaded: %s (%d frames, %.1fs)",
                            clip.name, clip.num_frames, clip.duration())
            except Exception as e:
                logger.error("  Failed to load %s: %s", npy_path, e)

        logger.info("Dataset: %d clips, %d total frames",
                     len(dataset.clips), dataset.total_frames)
        return dataset

    @classmethod
    def from_directory(cls, directory, tags=None):
        """Load all .npy files from a directory."""
        directory = Path(directory)
        dataset = cls()

        for npy_path in sorted(directory.glob("*.npy")):
            try:
                clip = MotionClip.from_npy(npy_path, tags=tags)
                dataset.add_clip(clip)
            except Exception as e:
                logger.error("  Failed to load %s: %s", npy_path, e)

        logger.info("Dataset: %d clips, %d total frames",
                     len(dataset.clips), dataset.total_frames)
        return dataset

    @property
    def total_frames(self):
        if self._total_frames == 0 and self.clips:
            self._total_frames = sum(c.num_frames for c in self.clips)
        return self._total_frames

    @property
    def obs_dim(self):
        return self._obs_computer.obs_dim

    def compute_all_observations(self):
        """Compute and cache AMP observations for all clips."""
        logger.info("Computing observations for %d clips...", len(self.clips))
        for clip in self.clips:
            if clip.observations is None:
                clip.compute_observations(self._obs_computer)
        logger.info("Observations computed.")

    def sample_frames(self, batch_size, rng=None):
        """Sample random frames weighted by clip duration.

        Args:
            batch_size: Number of frames.
            rng: numpy random Generator.

        Returns:
            List of (clip_index, frame_index) tuples.
        """
        if not self.clips:
            return []
        if rng is None:
            rng = np.random.default_rng()

        weights = np.array([c.num_frames for c in self.clips], dtype=np.float64)
        weights /= weights.sum()

        clip_indices = rng.choice(len(self.clips), size=batch_size, p=weights)
        samples = []
        for ci in clip_indices:
            fi = rng.integers(0, self.clips[ci].num_frames)
            samples.append((int(ci), int(fi)))
        return samples

    def sample_observations(self, batch_size, rng=None):
        """Sample a batch of AMP observations.

        Computes on demand if not cached.

        Args:
            batch_size: Number of observations.
            rng: numpy random Generator.

        Returns:
            (batch_size, obs_dim) float32 array.
        """
        samples = self.sample_frames(batch_size, rng)
        obs_batch = np.zeros((batch_size, self.obs_dim), dtype=np.float32)

        for i, (ci, fi) in enumerate(samples):
            clip = self.clips[ci]
            if clip.observations is None:
                clip.compute_observations(self._obs_computer)
            obs_batch[i] = clip.observations[fi]

        return obs_batch

    def get_clips_by_tag(self, tag):
        """Get all clips matching a tag."""
        return [c for c in self.clips if tag in c.tags]


# ============================================================================
# Manifest generation / validation
# ============================================================================


def generate_manifest(directory, output_path):
    """Generate a YAML manifest from a directory of .npy motion files.

    Args:
        directory: Path to directory of .npy files.
        output_path: Output YAML path.
    """
    if yaml is None:
        logger.error("PyYAML required. Install: pip install pyyaml")
        sys.exit(1)

    directory = Path(directory)
    output_path = Path(output_path)

    npy_files = sorted(directory.glob("*.npy"))
    if not npy_files:
        logger.error("No .npy files in %s", directory)
        sys.exit(1)

    logger.info("Scanning %d .npy files in %s", len(npy_files), directory)

    entries = []
    for npy_path in npy_files:
        try:
            data = np.load(str(npy_path), allow_pickle=True).item()
            fps = float(data.get("fps", 30.0))
            num_frames = data["joint_rotations"].shape[0]
            duration = num_frames / fps if fps > 0 else 0

            entry = {
                "file": npy_path.name,
                "fps": int(round(fps)),
                "tags": _infer_tags(npy_path.stem),
            }
            entries.append(entry)
            logger.info("  %s: %d frames, %.1fs, tags=%s",
                        npy_path.name, num_frames, duration, entry["tags"])
        except Exception as e:
            logger.error("  Failed: %s: %s", npy_path, e)

    manifest = {"motions": entries}

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        yaml.dump(manifest, f, default_flow_style=None, sort_keys=False)

    logger.info("Manifest: %s (%d entries)", output_path, len(entries))


def validate_manifest(manifest_path):
    """Validate a manifest: check files exist and shapes are correct.

    Returns True if all checks pass.
    """
    if yaml is None:
        logger.error("PyYAML required.")
        return False

    manifest_path = Path(manifest_path)
    with open(manifest_path, "r") as f:
        manifest = yaml.safe_load(f)

    base_dir = manifest_path.parent
    entries = manifest.get("motions", [])
    logger.info("Validating %d entries in %s", len(entries), manifest_path)

    ok = 0
    errors = 0

    for entry in entries:
        npy_path = base_dir / entry["file"]
        if not npy_path.exists():
            logger.error("  MISSING: %s", npy_path)
            errors += 1
            continue

        try:
            data = np.load(str(npy_path), allow_pickle=True).item()

            required = ["joint_rotations", "joint_positions",
                        "root_positions", "root_rotations"]
            missing_keys = [k for k in required if k not in data]
            if missing_keys:
                logger.error("  %s: missing keys %s", entry["file"], missing_keys)
                errors += 1
                continue

            jr = data["joint_rotations"]
            jp = data["joint_positions"]
            rp = data["root_positions"]
            rr = data["root_rotations"]
            nf = jr.shape[0]

            if jr.shape[2] != 4:
                logger.error("  %s: joint_rotations %s (need [F,J,4])",
                             entry["file"], jr.shape)
                errors += 1
            elif jp.shape != (nf, jr.shape[1], 3):
                logger.error("  %s: joint_positions %s mismatch", entry["file"], jp.shape)
                errors += 1
            elif rp.shape != (nf, 3):
                logger.error("  %s: root_positions %s", entry["file"], rp.shape)
                errors += 1
            elif rr.shape != (nf, 4):
                logger.error("  %s: root_rotations %s", entry["file"], rr.shape)
                errors += 1
            else:
                ok += 1
                logger.info("  OK: %s (%d frames, %d joints)",
                            entry["file"], nf, jr.shape[1])

        except Exception as e:
            logger.error("  %s: %s", entry["file"], e)
            errors += 1

    logger.info("Result: %d OK, %d errors", ok, errors)
    return errors == 0


def _infer_tags(stem):
    """Infer semantic tags from a clip filename stem."""
    stem_lower = stem.lower()
    tag_keywords = {
        "walk": ["walk", "locomotion"],
        "run": ["run", "locomotion"],
        "sprint": ["run", "sprint", "locomotion"],
        "jog": ["run", "jog", "locomotion"],
        "idle": ["idle"],
        "stand": ["idle"],
        "crouch": ["crouch"],
        "sneak": ["crouch", "sneak"],
        "kick": ["kick", "strike"],
        "punch": ["punch", "strike"],
        "strike": ["strike"],
        "jump": ["jump"],
        "turn": ["turn"],
    }
    tags = []
    for keyword, ktags in tag_keywords.items():
        if keyword in stem_lower:
            for t in ktags:
                if t not in tags:
                    tags.append(t)
    return tags if tags else ["unknown"]


# ============================================================================
# CLI
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Motion dataset manager for AMP/CALM training",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Generate manifest:
    python tools/motion_dataset.py --generate-manifest data/calm/motions/ \\
        --output data/calm/motions/manifest.yaml

  Validate manifest:
    python tools/motion_dataset.py --validate data/calm/motions/manifest.yaml

  Print observation layout:
    python tools/motion_dataset.py --info
        """,
    )
    parser.add_argument("--generate-manifest", type=Path, metavar="DIR",
                        help="Generate YAML manifest from .npy directory")
    parser.add_argument("--output", type=Path,
                        help="Output path for manifest")
    parser.add_argument("--validate", type=Path, metavar="MANIFEST",
                        help="Validate a manifest YAML")
    parser.add_argument("--info", action="store_true",
                        help="Print observation layout info")

    args = parser.parse_args()

    if args.info:
        print("AMP Observation Layout")
        print("=" * 60)
        print(f"  Root height:           1   [0]")
        print(f"  Root rotation 6D:      6   [1..6]")
        print(f"  Root velocity:         3   [7..9]")
        print(f"  Root angular velocity: 3   [10..12]")
        print(f"  DOF positions:         {TOTAL_DOFS}  [13..{13+TOTAL_DOFS-1}]")
        print(f"  DOF velocities:        {TOTAL_DOFS}  [{13+TOTAL_DOFS}..{13+2*TOTAL_DOFS-1}]")
        kstart = 13 + 2 * TOTAL_DOFS
        print(f"  Key body positions:    {NUM_KEY_BODIES*3}  [{kstart}..{OBS_DIM-1}]")
        print(f"  ---")
        print(f"  Total observation dim: {OBS_DIM}")
        print(f"  Total DOFs:            {TOTAL_DOFS}")
        print(f"  Key bodies ({NUM_KEY_BODIES}): {KEY_BODY_NAMES}")
        print()
        print("DOF Mapping (index -> joint, axis):")
        for d, (ji, axis) in enumerate(DOF_MAPPINGS):
            axis_name = "XYZ"[axis]
            print(f"  DOF {d:2d}: {TRAINING_JOINT_NAMES[ji]:20s} axis={axis_name}")
        print()
        print("Training Joint Order:")
        for i, name in enumerate(TRAINING_JOINT_NAMES):
            bdef = HUMANOID_BONE_DEFS[i]
            flags = []
            if bdef[2] > 0:
                flags.append(f"{bdef[2]} DOFs")
            if bdef[3]:
                flags.append("key_body")
            print(f"  {i:2d}: {name:20s} ({bdef[1]:16s}) {', '.join(flags)}")
        return

    if args.generate_manifest:
        if args.output is None:
            args.output = args.generate_manifest / "manifest.yaml"
        generate_manifest(args.generate_manifest, args.output)
        return

    if args.validate:
        success = validate_manifest(args.validate)
        sys.exit(0 if success else 1)

    parser.print_help()


if __name__ == "__main__":
    main()
