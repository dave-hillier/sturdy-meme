# UniCon Implementation Plan

[UniCon: Universal Neural Controller for Physics-based Character Motion](https://research.nvidia.com/labs/toronto-ai/unicon/) — a target-frame-tracking approach to physics-based character animation using reinforcement learning.

UniCon is implemented as a strategy within the shared `src/ml/` framework, alongside CALM. Both share the general-purpose ML infrastructure (Tensor, MLPNetwork, ModelLoader, CharacterConfig) and training environment (VecEnv, CharacterEnv).

## Architecture

```
src/ml/                     # Shared ML infrastructure
├── Tensor.h                # Lightweight tensor for inference
├── MLPNetwork.h            # Generic MLP (ReLU/Tanh/ELU activations)
├── ModelLoader.h           # Binary weight loading (.bin format)
├── CharacterConfig.h       # DOF mappings, observation/action dims
├── ObservationExtractor.h  # AMP-style temporal observation stacking
├── ActionApplier.h         # Policy actions → skeleton poses
├── calm/                   # CALM strategy (latent-space behaviors)
│   ├── Controller.h        # Latent management + LLC inference
│   └── LowLevelController.h
└── unicon/                 # UniCon strategy (target-frame tracking)
    ├── Controller.h        # Obs → MLP → torques loop
    └── StateEncoder.h      # UniCon Eq.4 observation encoding

src/training/               # Shared training infrastructure
├── CharacterEnv.h          # Single-character RL environment
├── VecEnv.h                # Vectorized N-character training
├── RewardComputer.h        # Task-based reward functions
├── MotionLibrary.h         # FBX motion loading for resets
└── bindings.cpp            # pybind11 Python bindings
```

## Phase Summary

| Phase | Topic | Status |
|-------|-------|--------|
| 1 | Extended Physics API | **Complete** |
| 2 | Articulated Body System | **Complete** |
| 3 | Humanoid State Encoding | **Complete** |
| 4 | MLP Inference Engine | **Complete** (shared `ml::MLPNetwork` with ELU support) |
| 5 | Low-Level Motion Executor | **Complete** (`ml::unicon::Controller`) |
| 6 | High-Level Motion Schedulers | Not started |
| 7 | Rendering Integration | Not started |
| 8 | Training Pipeline | **Complete** (shared VecEnv + pybind11) |
| 9 | NPC Integration | Not started |
| 10 | Polish & Advanced | Not started |

## Next Steps

1. **Convert motion data**: `python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/`
2. **Train via Python**: Use pybind11 VecEnv with PPO to train a UniCon policy
3. **Phase 6**: Build motion schedulers (mocap playback, keyboard control)
4. **Phase 7**: Connect physics-driven skeleton to the rendering pipeline

## Remaining Phases

### Phase 6: High-Level Motion Schedulers

Schedulers produce target frames for the UniCon controller:
- **MocapScheduler**: plays back a single motion clip as target frames
- **MotionStitchingScheduler**: sequences clips with SLERP blending
- **KeyboardScheduler**: maps WASD input to motion selection

### Phase 7: Rendering Integration

Add `PhysicsBased` animation mode to `AnimatedCharacter`. When active, `ArticulatedBody::writeToSkeleton()` feeds the existing skinned mesh pipeline.

### Phase 9: NPC Integration

LOD-based physics policy: full UniCon for close NPCs (< 25m), kinematic animation beyond. All NPCs share one `ml::MLPNetwork` instance; each has its own `ArticulatedBody`.

### Phase 10: Polish

- Perturbation response (random impulses → natural recovery)
- Terrain adaptation (uneven ground, slopes)
- Style transfer via motion stitching

## Key References

- [UniCon Paper](https://nv-tlabs.github.io/unicon/resources/main.pdf)
- [UniCon Project Page](https://research.nvidia.com/labs/toronto-ai/unicon/)
