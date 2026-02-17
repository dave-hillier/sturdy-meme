# UniCon Implementation Plan

Implementation plan for [UniCon: Universal Neural Controller for Physics-based Character Motion](https://research.nvidia.com/labs/toronto-ai/unicon/) — a two-level framework for physics-based character animation using reinforcement learning.

## Background

UniCon separates character control into two layers:

1. **High-level motion scheduler** — generates target motion frames (from mocap data, keyboard input, video, or motion stitching)
2. **Low-level motion executor** — an RL-trained MLP policy that outputs joint torques to physically track the target frames

The key insight is that the low-level executor, once trained on thousands of motions, generalizes to unseen motions and can be paired with arbitrary high-level schedulers without retraining. The policy uses a torque-based controller (not PD), which avoids overfitting to specific motions.

### Paper Specifications

- **Humanoid**: 20 rigid bodies, 35 degrees of freedom, 1.8m height, 70kg mass
- **Policy network**: MLP with 3 hidden layers of 1024 units each
- **Observation**: current character state + τ future target frames (encoded as local joint positions, rotations, velocities, plus relative root offsets)
- **Action**: joint torques (effort factors 50–600 per joint)
- **Reward**: weighted sum of root position/rotation + joint rotation/position + joint angular velocity matching (weights: 0.2, 0.2, 0.1, 0.4, 0.1)
- **Training**: PPO with 4096 parallel agents, 64 samples/iteration/worker, constrained multi-objective optimization (α=0.1 per reward term)
- **Physics**: 60 Hz simulation, 4 substeps

## Current Codebase State

**What exists:**
- Jolt Physics with 60 Hz fixed timestep, 4 substeps — matches UniCon's physics spec
- Skinned mesh rendering pipeline (GPU bone matrix skinning, 128 bones max)
- Full skeleton/animation system: clips, state machine, motion matching, blend space, layers
- 5 IK solvers (TwoBoneIK, FootPlacement, LookAt, Straddle, Climbing)
- NPC system with LOD-based update scheduling and archetype-based crowd rendering
- FBX and glTF loaders with skeleton/animation extraction
- Cloth simulation with Verlet integration and distance constraints (reference pattern)

**What's missing:**
- Jolt constraint/joint system (supported by Jolt, not yet exposed)
- Torque/force application API (only impulse exposed)
- Articulated rigid body representation
- Neural network inference runtime
- Animation-physics synchronization layer

## Implementation Phases

Each phase produces a compilable, runnable build with visible results.

---

### Phase 1: Extended Physics API

Expose the Jolt APIs needed for articulated body simulation.

**Files to modify:**
- `src/physics/PhysicsSystem.h` — add force/torque/angular velocity API
- `src/physics/PhysicsSystem.cpp` — implement new methods

**New API surface:**
```cpp
void applyForce(PhysicsBodyID id, const glm::vec3& force, const glm::vec3& point);
void applyTorque(PhysicsBodyID id, const glm::vec3& torque);
void setAngularVelocity(PhysicsBodyID id, const glm::vec3& angVel);
glm::vec3 getAngularVelocity(PhysicsBodyID id) const;
glm::quat getBodyRotation(PhysicsBodyID id) const;
```

Also extend `PhysicsBodyInfo` to include `angularVelocity`.

**Verification:** Existing physics tests still pass. Can apply a torque to a box and observe spinning behavior in the scene.

---

### Phase 2: Articulated Body System

Create a multi-rigid-body structure connected by Jolt constraints, driven from a skeleton definition.

**New files:**
- `src/physics/ArticulatedBody.h`
- `src/physics/ArticulatedBody.cpp`

**Design:**

```cpp
struct BodyPartDef {
    std::string name;
    int32_t skeletonJointIndex;
    int32_t parentPartIndex;          // -1 for root (pelvis)
    glm::vec3 halfExtents;            // Capsule/box dimensions
    float mass;
    glm::vec3 localAnchorInParent;    // Constraint attachment point in parent
    glm::vec3 localAnchorInChild;     // Constraint attachment point in child
    glm::vec3 twistAxis;              // Primary rotation axis
    glm::vec2 twistLimits;            // Min/max twist angle
    glm::vec2 swingLimits;            // Cone swing limits
};

struct ArticulatedBodyConfig {
    std::vector<BodyPartDef> parts;
    float globalScale = 1.0f;
};

class ArticulatedBody {
public:
    bool create(PhysicsSystem& physics, const ArticulatedBodyConfig& config,
                const glm::vec3& rootPosition);
    void destroy(PhysicsSystem& physics);

    // State extraction (for building observation vectors)
    void getState(std::vector<glm::vec3>& positions,
                  std::vector<glm::quat>& rotations,
                  std::vector<glm::vec3>& linearVelocities,
                  std::vector<glm::vec3>& angularVelocities) const;

    // Torque application (from policy output)
    void applyTorques(PhysicsSystem& physics,
                      const std::vector<glm::vec3>& torques);

    // Sync physics state → skeleton for rendering
    void writeToSkeleton(Skeleton& skeleton) const;

    size_t getPartCount() const;
    PhysicsBodyID getPartBodyID(size_t index) const;

private:
    std::vector<PhysicsBodyID> bodyIDs_;
    std::vector<int32_t> jointIndices_;    // Maps part → skeleton joint
    // Jolt constraint handles stored internally
};
```

This wraps the Jolt `SwingTwistConstraint` (or `SixDOFConstraint`) to connect body parts with anatomically plausible joint limits.

**Humanoid template factory:**

```cpp
// Builds a standard 20-body humanoid matching UniCon's specification
ArticulatedBodyConfig createHumanoidConfig(const Skeleton& skeleton);
```

Maps skeleton joints to rigid body parts:
- **Pelvis** (root) — box
- **Torso** — 3 capsules (lower spine, upper spine, chest)
- **Head** — sphere/capsule
- **Arms** — 2×3 capsules (upper arm, forearm, hand)
- **Legs** — 2×3 capsules (thigh, shin, foot)

Total: 20 parts, matching the paper.

**Verification:** Spawn an articulated humanoid, let it fall under gravity. Body parts stay connected by constraints, ragdoll settles on terrain. Visible in the renderer via debug wireframe or basic box/capsule rendering.

---

### Phase 3: Humanoid State Encoding

Build the observation vector matching UniCon's specification.

**New files:**
- `src/unicon/StateEncoder.h`
- `src/unicon/StateEncoder.cpp`

**Observation vector construction** (per UniCon Equation 4):

```
s_t = [o(X̃_t), o(X_{t+1}), ..., o(X_{t+τ}), y(X_{t+1}), ..., y(X_{t+τ})]
```

Where `o(X)` encodes a character state in the root's local frame:
- Root height (z-component only)
- Root rotation quaternion (4)
- Joint positions relative to root (3J)
- Joint rotation quaternions (4J)
- Root linear velocity in local frame (3)
- Root angular velocity in local frame (3)
- Joint linear velocities in local frame (3J)
- Joint angular velocities in local frame (4J)

And `y(X̃, X)` encodes the relative root offset between actual and target states.

```cpp
class StateEncoder {
public:
    // Configure for a specific humanoid
    void configure(size_t numJoints, size_t targetFrameCount);

    // Build observation vector from physics state + target frames
    void encode(const ArticulatedBody& body,
                const std::vector<TargetFrame>& targetFrames,
                std::vector<float>& observation) const;

    size_t getObservationDim() const;

private:
    size_t numJoints_;
    size_t tau_;  // Number of future target frames
};

struct TargetFrame {
    glm::vec3 rootPosition;
    glm::quat rootRotation;
    std::vector<glm::vec3> jointPositions;
    std::vector<glm::quat> jointRotations;
    std::vector<glm::vec3> jointVelocities;
    std::vector<glm::vec3> jointAngularVelocities;
};
```

**Verification:** Log observation vector dimensions and sample values. Confirm they match expected sizes based on joint count and target frame count.

---

### Phase 4: MLP Inference Engine

Lightweight neural network forward pass for the trained policy.

**New files:**
- `src/unicon/MLPPolicy.h`
- `src/unicon/MLPPolicy.cpp`

The UniCon policy is a simple feedforward MLP (3 hidden layers × 1024 units). No need for a full ML framework — matrix multiplication + activation is sufficient.

```cpp
struct MLPLayer {
    std::vector<float> weights;  // [outputDim × inputDim], row-major
    std::vector<float> biases;   // [outputDim]
    size_t inputDim;
    size_t outputDim;
};

class MLPPolicy {
public:
    // Load weights from file (ONNX-derived or custom format)
    bool loadWeights(const std::string& path);

    // Forward pass: observation → action (torques)
    void evaluate(const std::vector<float>& observation,
                  std::vector<float>& action) const;

    size_t getInputDim() const;
    size_t getOutputDim() const;

private:
    std::vector<MLPLayer> layers_;
    // Scratch buffers for intermediate activations
    mutable std::vector<float> buffer0_, buffer1_;
};
```

**Activation function:** ELU or ReLU (paper doesn't specify; ReLU is the common default for PPO policies, try both).

**Weight format:** Define a simple binary format or use a conversion script to export from PyTorch/ONNX → flat binary with a header describing layer dimensions. This keeps dependencies minimal.

Optionally, consider an ONNX Runtime integration behind a compile-time flag for users who want GPU inference or more complex models.

**Verification:** Load a small test network with known weights, verify output against a PyTorch reference. Measure inference latency — should be well under 1ms for a 3×1024 MLP.

---

### Phase 5: Low-Level Motion Executor

The core runtime that connects observation → policy → torques → physics each timestep.

**New files:**
- `src/unicon/MotionExecutor.h`
- `src/unicon/MotionExecutor.cpp`

```cpp
class MotionExecutor {
public:
    struct Config {
        size_t targetFrameCount = 1;  // τ (number of future target frames)
        float earlyTerminationThreshold = 0.1f;  // α per reward term
        std::string policyWeightsPath;
    };

    bool init(const Config& config, const ArticulatedBodyConfig& bodyConfig);

    // Called each physics step (60 Hz)
    void step(PhysicsSystem& physics,
              ArticulatedBody& body,
              const std::vector<TargetFrame>& targetFrames);

    // Check if character has deviated too far from target
    bool shouldTerminate() const;

    // Reward evaluation (for debugging/visualization)
    float getLastReward() const;
    float getRewardTerm(size_t index) const;

private:
    MLPPolicy policy_;
    StateEncoder encoder_;
    std::vector<float> observation_;
    std::vector<float> action_;
    float lastReward_ = 0.0f;
};
```

**Torque scaling:** The policy outputs normalized torques. Each joint has an effort factor (50–600 range from the paper) that scales the output to physical torque magnitudes. These effort factors are part of the `ArticulatedBodyConfig`.

**Reward evaluation** (for debug overlay, not training):
```
r = 0.2·r_rootPos + 0.2·r_rootRot + 0.1·r_jointPos + 0.4·r_jointRot + 0.1·r_jointAngVel
```
Each term: `exp(-k · ||target - actual||²)`

**Verification:** With random policy weights, the executor runs without crashes. The humanoid applies random torques and flails physically. Reward terms are computed and displayed in the debug GUI.

---

### Phase 6: High-Level Motion Schedulers

Implement the schedulers that produce target frames for the executor.

**New files:**
- `src/unicon/MotionScheduler.h`
- `src/unicon/MotionScheduler.cpp`

```cpp
// Base interface
class MotionScheduler {
public:
    virtual ~MotionScheduler() = default;
    virtual void update(float deltaTime) = 0;
    virtual std::vector<TargetFrame> getTargetFrames(size_t count) const = 0;
    virtual bool isFinished() const = 0;
};
```

#### 6a: Motion Dataset Scheduler

Plays back a single motion clip as target frames. This is the simplest scheduler and the one used during training.

```cpp
class MocapScheduler : public MotionScheduler {
public:
    void setClip(const AnimationClip* clip, const Skeleton& skeleton);
    void update(float deltaTime) override;
    std::vector<TargetFrame> getTargetFrames(size_t count) const override;
    bool isFinished() const override;

private:
    const AnimationClip* clip_ = nullptr;
    const Skeleton* skeleton_ = nullptr;
    float time_ = 0.0f;
};
```

Converts `AnimationClip` keyframes into `TargetFrame` format by sampling the clip at future timesteps.

**Verification:** Play a walk cycle. The executor tracks it physically — character walks with physics-based foot contacts instead of kinematic playback.

#### 6b: Motion Stitching Scheduler

Sequences multiple motions with SLERP blending at transitions.

```cpp
class MotionStitchingScheduler : public MotionScheduler {
public:
    void queueMotion(const AnimationClip* clip, float blendDuration = 0.2f);
    // ...
private:
    struct QueueEntry {
        const AnimationClip* clip;
        float blendDuration;
    };
    std::queue<QueueEntry> queue_;
    // Current + next clip with blend state
};
```

**Verification:** Queue walk → jump → walk. Character physically transitions between motions.

#### 6c: Keyboard Control Scheduler

Maps keyboard input to motion selection. This is the interactive application from the paper.

```cpp
class KeyboardScheduler : public MotionScheduler {
public:
    void setMotionLibrary(const std::vector<const AnimationClip*>& clips,
                          const std::vector<std::string>& tags);
    void handleInput(const glm::vec3& moveDirection, float speed, bool jump);
    // ...
};
```

Uses the existing `InputSystem` to drive motion selection. Tags on clips (e.g., "walk_forward", "walk_left", "run", "jump") determine which clip to target based on input state.

**Verification:** WASD controls character with physics-based locomotion. Character reacts to direction changes with physical momentum.

---

### Phase 7: Rendering Integration

Connect the physics-simulated character to the existing skinned mesh rendering pipeline.

**Files to modify:**
- `src/animation/AnimatedCharacter.h/.cpp` — add physics-driven mode
- `src/npc/NPCSimulation.h/.cpp` — option to use physics-based NPCs

**Approach:**

The `ArticulatedBody::writeToSkeleton()` method copies physics body transforms into skeleton joint local transforms. From there, the existing pipeline takes over:
1. `Skeleton` → compute global transforms
2. Global transforms → bone matrices (multiply by inverse bind)
3. Upload bone matrices to UBO
4. GPU vertex shader skins the mesh

```cpp
// In AnimatedCharacter, add a new playback mode:
enum class AnimationMode {
    Player,          // Existing AnimationPlayer
    StateMachine,    // Existing
    LayerController, // Existing
    MotionMatching,  // Existing
    BlendSpace,      // Existing
    PhysicsBased     // NEW: UniCon executor drives skeleton
};
```

When `PhysicsBased` is active, `update()` skips animation sampling and instead reads transforms from the `ArticulatedBody`.

**IK integration:** Foot placement IK can still run as a post-process on top of the physics-derived pose for fine ground contact adjustment, though the physics simulation should handle most ground interaction naturally.

**Debug visualization:**
- Wireframe rendering of articulated body parts (capsules/boxes)
- Joint constraint limits (cone visualization)
- Reward term heatmap on body parts
- Target vs actual pose overlay (ghost mesh)

Add these to the ImGui debug panel alongside existing debug views.

**Verification:** A physics-driven character renders with the same skinned mesh as kinematic characters. Switching between animation modes shows the difference in motion quality.

---

### Phase 8: Training Pipeline (Offline, Python)

The RL training happens outside the game engine. This phase creates the tooling.

**New files:**
- `tools/unicon_training/` — Python training scripts
- `tools/unicon_training/environment.py` — Jolt-based simulation environment
- `tools/unicon_training/policy.py` — PyTorch MLP policy
- `tools/unicon_training/train.py` — PPO training loop
- `tools/unicon_training/export.py` — Export trained weights for C++ inference

**Environment design:**

Option A: Use a Python physics binding (e.g., `pybullet`, `mujoco`, or `jolt-python`) to create a training environment matching the C++ simulation as closely as possible.

Option B: Build the training environment in C++ and expose it via pybind11, ensuring exact physics parity with the runtime.

Option B is preferred for sim-to-real transfer fidelity but requires more infrastructure.

**Training specs from the paper:**
- PPO with clipped objective + KL penalty
- 4096 parallel agents (GPU-parallelized)
- Constrained multi-objective optimization: terminate episode if any reward term < 0.1
- Motion balancer: hierarchical sampling to handle dataset imbalance
- RSIS (Reactive State Initialization): start episodes k=5–10 frames ahead with noise
- Policy variance controller: linear annealing of per-joint log-std

**Motion data:**
- CMU Mocap Database (2758 train / 594 test motions, 752k / 187k frames)
- Or use any FBX/glTF animation library that the existing loaders can parse

**Export format:**
```python
# export.py — save weights as flat binary
def export_policy(model, path):
    with open(path, 'wb') as f:
        # Header: num_layers, then per-layer: input_dim, output_dim
        # Body: weights (row-major float32), biases (float32)
        ...
```

**Verification:** Train on a small motion subset (e.g., 10 walk clips). Export weights. Load in C++ engine and observe the character tracking walk motions physically.

---

### Phase 9: NPC Integration

Enable physics-based animation for NPCs in the crowd system.

**Files to modify:**
- `src/npc/NPCSimulation.h/.cpp`
- `src/npc/NPCRenderer.h/.cpp`

**LOD-based physics policy:**

Physics-based animation is expensive (policy inference + physics simulation per NPC). Use the existing LOD tiers:

| LOD | Distance | Animation Mode |
|-----|----------|---------------|
| Real (2) | < 25m | Full UniCon (physics + policy) |
| Bulk (1) | 25–50m | Kinematic animation (existing motion matching/state machine) |
| Virtual (0) | > 50m | Simplified animation with LOD bone masking |

Only NPCs in the "Real" tier run the physics executor. When an NPC transitions from Bulk → Real, its articulated body is spawned and initialized from the current animation pose. When transitioning Real → Bulk, the physics body is destroyed and animation resumes kinematically.

**Shared policy:** All NPCs share a single `MLPPolicy` instance (weights are read-only at inference time). Each NPC has its own `ArticulatedBody` and `MotionExecutor` state.

**Batch inference optimization:** For multiple physics-based NPCs, batch their observation vectors into a single matrix and run one batched MLP forward pass. This is cache-friendly and avoids per-NPC overhead.

```cpp
class BatchedMLPPolicy {
public:
    void evaluateBatch(const std::vector<const float*>& observations,
                       std::vector<float*>& actions,
                       size_t batchSize) const;
};
```

**Verification:** Spawn 5–10 NPCs near the camera. Nearest ones use physics-based animation, distant ones use kinematic. Transition between modes is visually smooth.

---

### Phase 10: Polish and Advanced Features

#### 10a: Perturbation Response
Apply random impulses to physics characters and observe natural recovery behavior (a key UniCon capability — zero-shot robustness to perturbation).

#### 10b: Video-to-Simulation
Integrate a 3D pose estimator (e.g., via network socket) to drive the high-level scheduler from video input. The executor physically reproduces the estimated poses.

#### 10c: Style Transfer
Use the motion stitching scheduler with motions of different styles (e.g., confident walk → sneaky walk) and observe natural physical blending.

#### 10d: Terrain Adaptation
The physics simulation naturally handles uneven terrain — characters step over obstacles, maintain balance on slopes. Verify with the existing terrain height system.

---

## Dependency Graph

```
Phase 1: Extended Physics API
    ↓
Phase 2: Articulated Body System
    ↓
Phase 3: State Encoder ←──────────────────┐
    ↓                                      │
Phase 4: MLP Inference ──→ Phase 8: Training Pipeline (parallel)
    ↓                              ↓
Phase 5: Motion Executor ←── trained weights
    ↓
Phase 6: High-Level Schedulers
    ↓
Phase 7: Rendering Integration
    ↓
Phase 9: NPC Integration
    ↓
Phase 10: Polish & Advanced
```

Phases 1–7 are sequential and form the core implementation path. Phase 8 (training) can proceed in parallel once Phases 3–4 define the observation/action format. Phase 9 builds on the integrated system. Phase 10 items are independent enhancements.

## Testing Strategy

Each phase includes its own verification step (described above). Additionally:

- **Unit test the MLP:** Known input/output pairs from a reference PyTorch model
- **Physics stability:** Run the articulated body for 10,000 steps without NaN or constraint explosion
- **Visual regression:** Side-by-side comparison of kinematic vs physics-based animation for the same clip
- **Performance profiling:** Measure per-frame cost of policy inference + physics for N characters; target < 2ms for a single character at 60 Hz
- **Manual testing for each phase:**
  - Phase 2: Drop ragdoll from height, observe physically plausible collapse
  - Phase 5: Play back walk clip, observe tracking fidelity
  - Phase 6b: Queue motions, observe smooth transitions
  - Phase 6c: WASD control, observe responsive physics-based locomotion
  - Phase 7: Switch animation modes mid-gameplay, verify visual continuity
  - Phase 9: Walk among NPCs, observe LOD transitions between physics/kinematic

## File Organization

```
src/
├── unicon/
│   ├── MLPPolicy.h/.cpp           # Neural network inference
│   ├── StateEncoder.h/.cpp        # Observation vector construction
│   ├── MotionExecutor.h/.cpp      # Low-level executor loop
│   ├── MotionScheduler.h/.cpp     # High-level scheduler interface + implementations
│   └── UniConSystem.h/.cpp        # Top-level system managing all UniCon components
├── physics/
│   ├── ArticulatedBody.h/.cpp     # NEW: Multi-rigid-body with constraints
│   ├── PhysicsSystem.h/.cpp       # MODIFIED: Extended force/torque API
│   └── ...
├── animation/
│   ├── AnimatedCharacter.h/.cpp   # MODIFIED: Add PhysicsBased mode
│   └── ...
└── npc/
    ├── NPCSimulation.h/.cpp       # MODIFIED: LOD-based physics animation
    └── ...

tools/
└── unicon_training/               # Python training pipeline
    ├── environment.py
    ├── policy.py
    ├── train.py
    └── export.py
```

## Key References

- [UniCon Paper (PDF)](https://nv-tlabs.github.io/unicon/resources/main.pdf)
- [UniCon Project Page](https://research.nvidia.com/labs/toronto-ai/unicon/)
- [arXiv:2011.15119](https://arxiv.org/abs/2011.15119)
- [Jolt Physics Constraints Documentation](https://jrouwe.github.io/JoltPhysics/)
- [CMU Mocap Database](http://mocap.cs.cmu.edu/)
