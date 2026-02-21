"""Vectorized environment wrapper for C++ physics simulation.

Provides a gym-like Python API for the C++ vectorized environment used in
training. Falls back to a DummyVecEnv with random observations when the
C++ module is not available, enabling testing of the training loop without
a compiled physics backend.

The C++ module ``jolt_training`` is expected to expose:
  - ``JoltVecEnv(num_envs, skeleton_path)``: Constructor
  - ``reset() -> np.ndarray``: Reset all envs, return obs (num_envs, obs_dim)
  - ``step(actions: np.ndarray) -> tuple``: Step all envs, return (obs, rewards, dones, infos)
  - ``obs_dim``, ``action_dim``: Property accessors
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
        self.episode_length = episode_length

        self._step_counts = np.zeros(num_envs, dtype=np.int32)
        self._rng = np.random.default_rng(seed=42)

    def reset(self) -> np.ndarray:
        """Reset all environments and return initial observations.

        Returns:
            Observations array, shape (num_envs, obs_dim), float32.
        """
        self._step_counts[:] = 0
        return self._random_obs()

    def step(
        self, actions: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[dict]]:
        """Step all environments with the given actions.

        Args:
            actions: Action array, shape (num_envs, action_dim), float32.

        Returns:
            Tuple of (obs, rewards, dones, infos):
                obs: shape (num_envs, obs_dim)
                rewards: shape (num_envs,)
                dones: shape (num_envs,) boolean
                infos: list of dicts (one per env), may contain "episode" key
                    with "r" (total reward) and "l" (episode length) on termination
        """
        self._step_counts += 1

        obs = self._random_obs()

        # Small random rewards simulating a sparse task reward
        rewards = self._rng.standard_normal(self.num_envs).astype(np.float32) * 0.1

        # Episodes terminate after fixed length
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

        # Auto-reset terminated envs
        terminated = np.where(dones)[0]
        if len(terminated) > 0:
            self._step_counts[terminated] = 0
            obs[terminated] = self._random_obs_subset(len(terminated))

        return obs, rewards, dones.astype(np.float32), infos

    def _random_obs(self) -> np.ndarray:
        """Generate random observation for all envs."""
        return self._rng.standard_normal(
            (self.num_envs, self.obs_dim)
        ).astype(np.float32) * 0.1

    def _random_obs_subset(self, count: int) -> np.ndarray:
        """Generate random observation for a subset of envs."""
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
        obs_dim: Observation dimension (used by DummyVecEnv fallback).
        action_dim: Action dimension (used by DummyVecEnv fallback).
    """

    def __init__(
        self,
        num_envs: int = 4096,
        skeleton_path: Optional[str] = None,
        obs_dim: int = 102,
        action_dim: int = 37,
    ):
        self.num_envs = num_envs
        self.using_cpp = False

        if _has_cpp_env and skeleton_path is not None:
            try:
                self._env = jolt_training.JoltVecEnv(num_envs, skeleton_path)
                self.obs_dim = self._env.obs_dim
                self.action_dim = self._env.action_dim
                self.using_cpp = True
                logger.info(
                    "Using C++ JoltVecEnv: %d envs, obs_dim=%d, action_dim=%d",
                    num_envs,
                    self.obs_dim,
                    self.action_dim,
                )
                return
            except Exception as e:
                logger.warning(
                    "Failed to create C++ JoltVecEnv, falling back to DummyVecEnv: %s",
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
        logger.info(
            "Using DummyVecEnv: %d envs, obs_dim=%d, action_dim=%d",
            num_envs,
            self.obs_dim,
            self.action_dim,
        )

    def reset(self) -> np.ndarray:
        """Reset all environments.

        Returns:
            Initial observations, shape (num_envs, obs_dim), float32.
        """
        return self._env.reset()

    def step(
        self, actions: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[dict]]:
        """Step all environments with the given actions.

        Args:
            actions: Action array, shape (num_envs, action_dim), float32.

        Returns:
            Tuple of (obs, rewards, dones, infos).
        """
        return self._env.step(actions)
