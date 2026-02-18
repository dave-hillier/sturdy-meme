"""Reward computation matching UniCon paper specifications.

Reward terms from the paper:
    r = 0.2 * r_rootPos + 0.2 * r_rootRot + 0.1 * r_jointPos
      + 0.4 * r_jointRot + 0.1 * r_jointAngVel

Each term: exp(-k * ||target - actual||^2)

Constrained multi-objective: terminate episode if any reward term < alpha (0.1).
"""

import numpy as np
from typing import Tuple

from .config import RewardConfig
from .state_encoder import quat_mul, quat_inverse


def compute_reward(
    actual_root_pos: np.ndarray,
    actual_root_rot: np.ndarray,
    actual_joint_pos: np.ndarray,
    actual_joint_rot: np.ndarray,
    actual_joint_ang_vel: np.ndarray,
    target_root_pos: np.ndarray,
    target_root_rot: np.ndarray,
    target_joint_pos: np.ndarray,
    target_joint_rot: np.ndarray,
    target_joint_ang_vel: np.ndarray,
    config: RewardConfig,
) -> Tuple[float, np.ndarray, bool]:
    """Compute the multi-term reward.

    Returns:
        total_reward: weighted sum of all terms
        terms: array of 5 individual reward terms
        should_terminate: True if any term < alpha (constrained MO)
    """
    terms = np.zeros(5, dtype=np.float32)

    # Root position: exp(-k * ||target - actual||^2)
    root_pos_err = np.sum((target_root_pos - actual_root_pos) ** 2)
    terms[0] = np.exp(-config.k_root_pos * root_pos_err)

    # Root rotation: exp(-k * ||q_diff||^2) where q_diff = q_target * q_actual^-1
    rot_diff = quat_mul(target_root_rot, quat_inverse(actual_root_rot))
    # Use imaginary part magnitude as rotation error (small for small angles)
    root_rot_err = np.sum(rot_diff[1:4] ** 2)
    terms[1] = np.exp(-config.k_root_rot * root_rot_err)

    # Joint positions: average over joints
    joint_pos_err = np.mean(np.sum((target_joint_pos - actual_joint_pos) ** 2, axis=-1))
    terms[2] = np.exp(-config.k_joint_pos * joint_pos_err)

    # Joint rotations: average over joints
    joint_rot_errs = []
    for i in range(actual_joint_rot.shape[0]):
        jrot_diff = quat_mul(target_joint_rot[i], quat_inverse(actual_joint_rot[i]))
        joint_rot_errs.append(np.sum(jrot_diff[1:4] ** 2))
    joint_rot_err = np.mean(joint_rot_errs) if joint_rot_errs else 0.0
    terms[3] = np.exp(-config.k_joint_rot * joint_rot_err)

    # Joint angular velocities: average over joints
    joint_angvel_err = np.mean(
        np.sum((target_joint_ang_vel - actual_joint_ang_vel) ** 2, axis=-1)
    )
    terms[4] = np.exp(-config.k_joint_ang_vel * joint_angvel_err)

    # Weighted sum
    weights = np.array([
        config.w_root_pos,
        config.w_root_rot,
        config.w_joint_pos,
        config.w_joint_rot,
        config.w_joint_ang_vel,
    ])
    total_reward = float(np.dot(weights, terms))

    # Constrained multi-objective termination
    should_terminate = bool(np.any(terms < config.alpha))

    return total_reward, terms, should_terminate


def compute_reward_batch(
    actual_root_pos: np.ndarray,
    actual_root_rot: np.ndarray,
    actual_joint_pos: np.ndarray,
    actual_joint_rot: np.ndarray,
    actual_joint_ang_vel: np.ndarray,
    target_root_pos: np.ndarray,
    target_root_rot: np.ndarray,
    target_joint_pos: np.ndarray,
    target_joint_rot: np.ndarray,
    target_joint_ang_vel: np.ndarray,
    config: RewardConfig,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Batched reward computation for vectorized environments.

    All inputs have an additional leading batch dimension (B, ...).

    Returns:
        total_rewards: (B,)
        terms: (B, 5)
        should_terminate: (B,) bool
    """
    B = actual_root_pos.shape[0]
    terms = np.zeros((B, 5), dtype=np.float32)

    # Root position error
    root_pos_err = np.sum((target_root_pos - actual_root_pos) ** 2, axis=-1)  # (B,)
    terms[:, 0] = np.exp(-config.k_root_pos * root_pos_err)

    # Root rotation error (simplified: use imaginary part of q_diff)
    # For batched: quat_mul isn't vectorized, so we compute directly
    # q_diff = target * inv(actual)
    inv_actual = actual_root_rot.copy()
    inv_actual[:, 1:4] *= -1
    # Quaternion product w component
    w = (target_root_rot[:, 0] * inv_actual[:, 0]
         - np.sum(target_root_rot[:, 1:4] * inv_actual[:, 1:4], axis=-1))
    xyz = (target_root_rot[:, 0:1] * inv_actual[:, 1:4]
           + inv_actual[:, 0:1] * target_root_rot[:, 1:4]
           + np.cross(target_root_rot[:, 1:4], inv_actual[:, 1:4]))
    root_rot_err = np.sum(xyz ** 2, axis=-1)  # (B,)
    terms[:, 1] = np.exp(-config.k_root_rot * root_rot_err)

    # Joint positions: (B, J, 3) -> average over joints
    joint_pos_err = np.mean(
        np.sum((target_joint_pos - actual_joint_pos) ** 2, axis=-1), axis=-1
    )  # (B,)
    terms[:, 2] = np.exp(-config.k_joint_pos * joint_pos_err)

    # Joint rotations: simplified as quaternion distance
    inv_actual_j = actual_joint_rot.copy()  # (B, J, 4)
    inv_actual_j[:, :, 1:4] *= -1
    # Per-joint quaternion product imaginary part magnitude
    xyz_j = (target_joint_rot[:, :, 0:1] * inv_actual_j[:, :, 1:4]
             + inv_actual_j[:, :, 0:1] * target_joint_rot[:, :, 1:4]
             + np.cross(target_joint_rot[:, :, 1:4], inv_actual_j[:, :, 1:4]))
    joint_rot_err = np.mean(np.sum(xyz_j ** 2, axis=-1), axis=-1)  # (B,)
    terms[:, 3] = np.exp(-config.k_joint_rot * joint_rot_err)

    # Joint angular velocities
    joint_angvel_err = np.mean(
        np.sum((target_joint_ang_vel - actual_joint_ang_vel) ** 2, axis=-1), axis=-1
    )  # (B,)
    terms[:, 4] = np.exp(-config.k_joint_ang_vel * joint_angvel_err)

    # Weighted sum
    weights = np.array([
        config.w_root_pos, config.w_root_rot, config.w_joint_pos,
        config.w_joint_rot, config.w_joint_ang_vel,
    ])
    total_rewards = terms @ weights  # (B,)

    # Constrained MO termination
    should_terminate = np.any(terms < config.alpha, axis=-1)  # (B,)

    return total_rewards, terms, should_terminate
