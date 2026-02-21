"""Training package for CALM/AMP physics-based character animation.

Provides PyTorch training code for:
  - AMP (Adversarial Motion Priors): LLC with fixed z=0
  - CALM (Conditional Adversarial Latent Models): LLC + MotionEncoder with per-clip z
  - HLC (High-Level Controller): Task-specific controller with frozen LLC

Network architectures match the C++ inference engine in src/ml/ so that trained
weights can be exported and loaded for real-time GPU inference.
"""

from training.networks import (
    StyleMLP,
    MainMLP,
    MuHead,
    LLCPolicy,
    ValueNetwork,
    AMPDiscriminator,
    MotionEncoder,
    HLCPolicy,
)

__all__ = [
    "StyleMLP",
    "MainMLP",
    "MuHead",
    "LLCPolicy",
    "ValueNetwork",
    "AMPDiscriminator",
    "MotionEncoder",
    "HLCPolicy",
]
