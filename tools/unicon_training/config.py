"""Training configuration matching UniCon paper specifications."""

from dataclasses import dataclass, field
from typing import List


@dataclass
class HumanoidConfig:
    """Humanoid body specification matching the C++ ArticulatedBody."""
    num_bodies: int = 20
    total_mass_kg: float = 70.0
    height_m: float = 1.8
    # Degrees of freedom (joints)
    num_dof: int = 35

    # Per-joint torque effort factors (50-600 range from paper).
    # Order matches ArticulatedBody part indices:
    # 0=pelvis, 1=lower_spine, 2=upper_spine, 3=chest, 4=head,
    # 5=l_upper_arm, 6=l_forearm, 7=l_hand, 8=r_upper_arm, 9=r_forearm,
    # 10=r_hand, 11=l_thigh, 12=l_shin, 13=l_foot, 14=r_thigh,
    # 15=r_shin, 16=r_foot, 17=l_shoulder, 18=r_shoulder, 19=neck
    effort_factors: List[float] = field(default_factory=lambda: [
        200.0,  # pelvis
        300.0,  # lower_spine
        300.0,  # upper_spine
        200.0,  # chest
        50.0,   # head
        150.0,  # l_upper_arm
        100.0,  # l_forearm
        50.0,   # l_hand
        150.0,  # r_upper_arm
        100.0,  # r_forearm
        50.0,   # r_hand
        600.0,  # l_thigh
        400.0,  # l_shin
        200.0,  # l_foot
        600.0,  # r_thigh
        400.0,  # r_shin
        200.0,  # r_foot
        150.0,  # l_shoulder
        150.0,  # r_shoulder
        100.0,  # neck
    ])


@dataclass
class PolicyConfig:
    """MLP architecture matching the C++ MLPPolicy."""
    hidden_layers: int = 3
    hidden_dim: int = 1024
    activation: str = "elu"  # ELU activation on hidden layers, linear output
    # tau: number of future target frames in the observation
    tau: int = 1


@dataclass
class RewardConfig:
    """Reward weights from UniCon paper."""
    # r = w_rp * r_rootPos + w_rr * r_rootRot + w_jp * r_jointPos
    #   + w_jr * r_jointRot + w_jav * r_jointAngVel
    w_root_pos: float = 0.2
    w_root_rot: float = 0.2
    w_joint_pos: float = 0.1
    w_joint_rot: float = 0.4
    w_joint_ang_vel: float = 0.1

    # Exponential kernel scale factors
    k_root_pos: float = 10.0
    k_root_rot: float = 5.0
    k_joint_pos: float = 10.0
    k_joint_rot: float = 2.0
    k_joint_ang_vel: float = 0.1

    # Constrained multi-objective: terminate if any reward term < alpha
    alpha: float = 0.1


@dataclass
class PPOConfig:
    """PPO hyperparameters."""
    num_envs: int = 4096
    samples_per_env: int = 64
    num_epochs: int = 5
    minibatch_size: int = 512
    learning_rate: float = 3e-4
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_epsilon: float = 0.2
    value_loss_coeff: float = 0.5
    entropy_coeff: float = 0.01
    max_grad_norm: float = 0.5
    # KL divergence target for adaptive clipping
    kl_target: float = 0.01

    # Total training iterations
    num_iterations: int = 10000
    # Save checkpoint every N iterations
    checkpoint_interval: int = 100
    # Log metrics every N iterations
    log_interval: int = 10


@dataclass
class RSISConfig:
    """Reactive State Initialization Scheme (RSIS) from the paper.
    Start episodes k frames ahead in the reference motion with noise."""
    min_offset_frames: int = 5
    max_offset_frames: int = 10
    position_noise_std: float = 0.02
    rotation_noise_std: float = 0.05
    velocity_noise_std: float = 0.1


@dataclass
class TrainingConfig:
    """Top-level training configuration."""
    humanoid: HumanoidConfig = field(default_factory=HumanoidConfig)
    policy: PolicyConfig = field(default_factory=PolicyConfig)
    reward: RewardConfig = field(default_factory=RewardConfig)
    ppo: PPOConfig = field(default_factory=PPOConfig)
    rsis: RSISConfig = field(default_factory=RSISConfig)

    # Physics simulation
    physics_dt: float = 1.0 / 60.0  # 60 Hz
    physics_substeps: int = 4

    # Motion data
    motion_dir: str = "assets/motions"
    output_dir: str = "generated/unicon"

    # Policy variance annealing
    initial_log_std: float = -1.0
    final_log_std: float = -3.0
    log_std_anneal_iterations: int = 5000

    device: str = "cuda"  # "cuda" or "cpu"
    seed: int = 42
