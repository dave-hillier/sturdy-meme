"""Vectorized environment wrapper for C++ physics simulation.

Provides a gym-like Python API for the C++ vectorized environment used in
training. Falls back to a DummyVecEnv with random observations when the
C++ module is not available, enabling testing of the training loop without
a compiled physics backend.

The C++ module ``jolt_training`` exposes:
  - ``VecEnv(num_envs, skeleton_path, config=EnvConfig())``: Constructor
  - ``reset() -> np.ndarray``: Reset all envs, return obs (num_envs, obs_dim)
  - ``step(actions) -> (obs, rewards, dones)``: Step all envs
  - ``load_motions(directory) -> int``: Load FBX animations for resets
  - ``load_motion_file(path) -> int``: Load single FBX animation
  - ``reset_done_with_motions()``: Reset done envs with random motion frames
  - ``amp_observations() -> np.ndarray``: Get AMP observations
  - ``set_task(task_type, target)``: Set task goal
"""

import logging
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)

# Try to import the C++ vectorized environment module
_has_cpp_env = False
try:
    import jolt_training  # noqa: F401

    _has_cpp_env = True
    logger.info("C++ jolt_training module found")
except ImportError:
    logger.warning(
        "C++ jolt_training module not found; using DummyVecEnv fallback. "
        "Training loop will run with random observations."
    )


class DummyVecEnv:
    """Dummy vectorized environment for testing the training loop.

    Generates random observations and rewards without any actual physics
    simulation. Useful for verifying that the training pipeline runs
    end-to-end before the C++ environment is compiled.

    Args:
        num_envs: Number of parallel environments.
        obs_dim: Observation vector dimension (default matches CALM config).
        action_dim: Action vector dimension (default matches CALM config).
        episode_length: Number of steps before automatic episode reset.
    """

    def __init__(
        self,
        num_envs: int = 4096,
        obs_dim: int = 102,
        action_dim: int = 37,
        episode_length: int = 300,
    ):
        self.num_envs = num_envs
        self.obs_dim = obs_dim
        self.action_dim = action_dim
        self.amp_obs_dim = obs_dim
        self.policy_obs_dim = obs_dim
        self.episode_length = episode_length
        self.num_motions = 0
        self.motion_duration = 0.0

        self._step_counts = np.zeros(num_envs, dtype=np.int32)
        self._rng = np.random.default_rng(seed=42)

    def reset(self) -> np.ndarray:
        """Reset all environments and return initial observations."""
        self._step_counts[:] = 0
        return self._random_obs()

    def step(
        self, actions: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[dict]]:
        """Step all environments with the given actions."""
        self._step_counts += 1

        obs = self._random_obs()
        rewards = self._rng.standard_normal(self.num_envs).astype(np.float32) * 0.1
        dones = self._step_counts >= self.episode_length

        infos: list[dict] = []
        for i in range(self.num_envs):
            info: dict = {}
            if dones[i]:
                info["episode"] = {
                    "r": float(self._rng.standard_normal() * 10),
                    "l": int(self._step_counts[i]),
                }
            infos.append(info)

        terminated = np.where(dones)[0]
        if len(terminated) > 0:
            self._step_counts[terminated] = 0
            obs[terminated] = self._random_obs_subset(len(terminated))

        return obs, rewards, dones.astype(np.float32), infos

    def load_motions(self, directory: str) -> int:
        """No-op for dummy env. Returns 0."""
        logger.info("DummyVecEnv: load_motions('%s') — no-op", directory)
        return 0

    def load_motion_file(self, path: str) -> int:
        """No-op for dummy env. Returns 0."""
        logger.info("DummyVecEnv: load_motion_file('%s') — no-op", path)
        return 0

    def reset_done_with_motions(self):
        """Reset done envs (same as step auto-reset for dummy env)."""
        pass

    def amp_observations(self) -> np.ndarray:
        """Return random AMP observations."""
        return self._random_obs()

    def set_task(self, task_type, target):
        """No-op for dummy env."""
        pass

    def _random_obs(self) -> np.ndarray:
        return self._rng.standard_normal(
            (self.num_envs, self.obs_dim)
        ).astype(np.float32) * 0.1

    def _random_obs_subset(self, count: int) -> np.ndarray:
        return self._rng.standard_normal(
            (count, self.obs_dim)
        ).astype(np.float32) * 0.1


class VecEnvWrapper:
    """Gym-like wrapper around the C++ or dummy vectorized environment.

    Provides a consistent API regardless of whether the C++ physics backend
    is available. When the C++ module is missing, falls back to DummyVecEnv.

    Args:
        num_envs: Number of parallel environments.
        skeleton_path: Path to the character skeleton .glb file (for C++ env).
        motions_dir: Optional path to FBX animation directory for episode resets.
        obs_dim: Observation dimension (used by DummyVecEnv fallback).
        action_dim: Action dimension (used by DummyVecEnv fallback).
    """

    def __init__(
        self,
        num_envs: int = 4096,
        skeleton_path: Optional[str] = None,
        motions_dir: Optional[str] = None,
        obs_dim: int = 102,
        action_dim: int = 37,
    ):
        self.num_envs = num_envs
        self.using_cpp = False

        if _has_cpp_env and skeleton_path is not None:
            try:
                self._env = jolt_training.VecEnv(num_envs, skeleton_path)
                self.obs_dim = self._env.policy_obs_dim
                self.action_dim = self._env.action_dim
                self.amp_obs_dim = self._env.amp_obs_dim
                self.using_cpp = True
                logger.info(
                    "Using C++ VecEnv: %d envs, obs_dim=%d, amp_obs_dim=%d, action_dim=%d",
                    num_envs,
                    self.obs_dim,
                    self.amp_obs_dim,
                    self.action_dim,
                )

                # Load motions if directory provided
                if motions_dir:
                    n = self._env.load_motions(motions_dir)
                    logger.info("Loaded %d motion clips from %s", n, motions_dir)

                return
            except Exception as e:
                logger.warning(
                    "Failed to create C++ VecEnv, falling back to DummyVecEnv: %s",
                    e,
                )

        # Fallback to dummy environment
        self._env = DummyVecEnv(
            num_envs=num_envs,
            obs_dim=obs_dim,
            action_dim=action_dim,
        )
        self.obs_dim = obs_dim
        self.action_dim = action_dim
        self.amp_obs_dim = obs_dim

        # Load motions (no-op for dummy)
        if motions_dir:
            self._env.load_motions(motions_dir)

        logger.info(
            "Using DummyVecEnv: %d envs, obs_dim=%d, action_dim=%d",
            num_envs,
            self.obs_dim,
            self.action_dim,
        )

    def reset(self) -> np.ndarray:
        """Reset all environments."""
        return self._env.reset()

    def step(
        self, actions: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[dict]]:
        """Step all environments with the given actions."""
        if self.using_cpp:
            obs, rewards, dones = self._env.step(actions)
            # Build infos list (C++ env doesn't provide episode stats yet)
            infos = [{} for _ in range(self.num_envs)]
            return obs, rewards, dones.astype(np.float32), infos
        return self._env.step(actions)

    def reset_done_with_motions(self):
        """Reset done environments using random motion frames from loaded clips."""
        self._env.reset_done_with_motions()

    def amp_observations(self) -> np.ndarray:
        """Get current AMP observations for discriminator training."""
        return self._env.amp_observations()

    def load_motions(self, directory: str) -> int:
        """Load FBX animation files from a directory."""
        return self._env.load_motions(directory)

    def load_motion_file(self, path: str) -> int:
        """Load animations from a single FBX file."""
        return self._env.load_motion_file(path)

    def set_task(self, task_type, target):
        """Set the task goal for all environments."""
        self._env.set_task(task_type, target)
