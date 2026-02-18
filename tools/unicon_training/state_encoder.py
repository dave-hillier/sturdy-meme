"""Python StateEncoder matching the C++ implementation in src/unicon/StateEncoder.cpp.

Encodes character state as an observation vector for the MLP policy.
Uses the same heading-local coordinate frame and component ordering
so that trained weights transfer directly to the C++ runtime.
"""

import numpy as np
from typing import List, Optional


def quat_yaw(q: np.ndarray) -> float:
    """Extract yaw angle from quaternion (w,x,y,z format, Y-up)."""
    w, x, y, z = q
    return np.arctan2(2.0 * (w * y + x * z), 1.0 - 2.0 * (y * y + z * z))


def quat_from_axis_angle(axis: np.ndarray, angle: float) -> np.ndarray:
    """Create quaternion (w,x,y,z) from axis-angle."""
    half = angle * 0.5
    s = np.sin(half)
    return np.array([np.cos(half), axis[0] * s, axis[1] * s, axis[2] * s])


def quat_inverse(q: np.ndarray) -> np.ndarray:
    """Inverse of unit quaternion (w,x,y,z)."""
    return np.array([q[0], -q[1], -q[2], -q[3]])


def quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Quaternion multiplication (w,x,y,z format)."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return np.array([
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    ])


def quat_rotate_vec(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Rotate vector v by quaternion q (w,x,y,z)."""
    qv = np.array([0.0, v[0], v[1], v[2]])
    result = quat_mul(quat_mul(q, qv), quat_inverse(q))
    return result[1:4]


class StateEncoder:
    """Builds observation vectors matching the C++ StateEncoder exactly.

    Observation layout:
        [o(X_t), o(X~_{t+1}), ..., o(X~_{t+tau}), y(X_t, X~_{t+1}), ..., y(X_t, X~_{t+tau})]

    Where o(X) encodes in heading-local frame:
        - Root height (1)
        - Root rotation quaternion (4)
        - Joint positions relative to root (3*J)
        - Joint rotation quaternions (4*J)
        - Root linear velocity (3)
        - Root angular velocity (3)
        - Joint angular velocities (3*J)
    Total per frame: 11 + 10*J

    Where y(X, X~) encodes root offset:
        - Horizontal offset x,z (2)
        - Height offset y (1)
        - Rotation offset quaternion (4)
    Total per offset: 7
    """

    ROOT_OFFSET_DIM = 7

    def __init__(self, num_joints: int, tau: int = 1):
        self.num_joints = num_joints
        self.tau = tau

    @property
    def frame_encoding_dim(self) -> int:
        return 11 + 10 * self.num_joints

    @property
    def observation_dim(self) -> int:
        return (1 + self.tau) * self.frame_encoding_dim + self.tau * self.ROOT_OFFSET_DIM

    def encode(
        self,
        root_pos: np.ndarray,
        root_rot: np.ndarray,
        root_lin_vel: np.ndarray,
        root_ang_vel: np.ndarray,
        joint_positions: np.ndarray,
        joint_rotations: np.ndarray,
        joint_ang_vels: np.ndarray,
        target_frames: List[dict],
    ) -> np.ndarray:
        """Build the full observation vector.

        Args:
            root_pos: (3,) root world position
            root_rot: (4,) root rotation quaternion (w,x,y,z)
            root_lin_vel: (3,) root linear velocity
            root_ang_vel: (3,) root angular velocity
            joint_positions: (J, 3) world-space joint positions
            joint_rotations: (J, 4) world-space joint rotations (w,x,y,z)
            joint_ang_vels: (J, 3) world-space joint angular velocities
            target_frames: list of tau dicts, each with keys:
                root_pos, root_rot, root_lin_vel, root_ang_vel,
                joint_positions, joint_rotations, joint_ang_vels

        Returns:
            observation: (observation_dim,) float32 array
        """
        obs = np.zeros(self.observation_dim, dtype=np.float32)
        offset = 0

        # Encode current state o(X_t)
        n = self._encode_frame(
            root_pos, root_rot, root_lin_vel, root_ang_vel,
            joint_positions, joint_rotations, joint_ang_vels,
            obs, offset,
        )
        offset += n

        # Encode target frames o(X~_{t+k})
        for k in range(self.tau):
            if k < len(target_frames):
                tf = target_frames[k]
                n = self._encode_frame(
                    tf["root_pos"], tf["root_rot"],
                    tf["root_lin_vel"], tf["root_ang_vel"],
                    tf["joint_positions"], tf["joint_rotations"],
                    tf["joint_ang_vels"],
                    obs, offset,
                )
            else:
                n = self.frame_encoding_dim
            offset += n

        # Encode root offsets y(X_t, X~_{t+k})
        for k in range(self.tau):
            if k < len(target_frames):
                tf = target_frames[k]
                n = self._encode_root_offset(
                    root_pos, root_rot,
                    tf["root_pos"], tf["root_rot"],
                    obs, offset,
                )
            else:
                n = self.ROOT_OFFSET_DIM
            offset += n

        return obs

    def _encode_frame(
        self,
        root_pos: np.ndarray,
        root_rot: np.ndarray,
        root_lin_vel: np.ndarray,
        root_ang_vel: np.ndarray,
        joint_positions: np.ndarray,
        joint_rotations: np.ndarray,
        joint_ang_vels: np.ndarray,
        out: np.ndarray,
        offset: int,
    ) -> int:
        """Encode a single frame into heading-local coordinates.
        Matches C++ StateEncoder::encodeFrame exactly."""
        idx = offset

        # Heading rotation (yaw only around Y axis)
        yaw = quat_yaw(root_rot)
        heading_rot = quat_from_axis_angle(np.array([0.0, 1.0, 0.0]), yaw)
        heading_inv = quat_inverse(heading_rot)

        # 1. Root height
        out[idx] = root_pos[1]  # Y-up
        idx += 1

        # 2. Root rotation in heading-local frame
        local_rot = quat_mul(heading_inv, root_rot)
        out[idx:idx + 4] = local_rot
        idx += 4

        # 3. Joint positions relative to root in heading frame (3*J)
        for i in range(self.num_joints):
            rel_pos = quat_rotate_vec(heading_inv, joint_positions[i] - root_pos)
            out[idx:idx + 3] = rel_pos
            idx += 3

        # 4. Joint rotation quaternions in heading frame (4*J)
        for i in range(self.num_joints):
            local_jrot = quat_mul(heading_inv, joint_rotations[i])
            out[idx:idx + 4] = local_jrot
            idx += 4

        # 5. Root linear velocity in heading frame
        local_lin_vel = quat_rotate_vec(heading_inv, root_lin_vel)
        out[idx:idx + 3] = local_lin_vel
        idx += 3

        # 6. Root angular velocity in heading frame
        local_ang_vel = quat_rotate_vec(heading_inv, root_ang_vel)
        out[idx:idx + 3] = local_ang_vel
        idx += 3

        # 7. Joint angular velocities in heading frame (3*J)
        for i in range(self.num_joints):
            local_jav = quat_rotate_vec(heading_inv, joint_ang_vels[i])
            out[idx:idx + 3] = local_jav
            idx += 3

        return idx - offset

    def _encode_root_offset(
        self,
        actual_pos: np.ndarray,
        actual_rot: np.ndarray,
        target_pos: np.ndarray,
        target_rot: np.ndarray,
        out: np.ndarray,
        offset: int,
    ) -> int:
        """Encode root offset y(X, X~).
        Matches C++ StateEncoder::encodeRootOffset exactly."""
        idx = offset

        yaw = quat_yaw(actual_rot)
        heading_rot = quat_from_axis_angle(np.array([0.0, 1.0, 0.0]), yaw)
        heading_inv = quat_inverse(heading_rot)

        world_offset = target_pos - actual_pos
        local_offset = quat_rotate_vec(heading_inv, world_offset)

        # 1. Horizontal offset (x, z)
        out[idx] = local_offset[0]
        out[idx + 1] = local_offset[2]
        idx += 2

        # 2. Height offset (y)
        out[idx] = local_offset[1]
        idx += 1

        # 3. Rotation offset quaternion
        rot_offset = quat_mul(heading_inv, target_rot)
        out[idx:idx + 4] = rot_offset
        idx += 4

        return self.ROOT_OFFSET_DIM
