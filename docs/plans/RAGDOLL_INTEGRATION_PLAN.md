# Ragdoll Physics Integration Plan

Integrate Jolt ragdoll physics with the CALM animation system so that NPC characters are physically simulated bodies driven by learned motor policies, not kinematic skeletons.

## Background

Currently CALM outputs target joint angles that are applied kinematically — the skeleton pose is set directly, bypassing physics. The `CharacterController` capsule and the animated skeleton are disconnected: the capsule handles collision/movement, the skeleton is purely visual.

Jolt provides a full ragdoll system (`JPH::Ragdoll`) that creates per-bone rigid bodies connected by motorised constraints. The key API is `Ragdoll::DriveToPoseUsingMotors()` — it takes a target pose and the constraint motors apply torques each physics step to reach it, while still respecting collisions, gravity, and external forces.

### What This Enables

- Characters react physically to impacts, explosions, terrain features
- Balance emerges from the policy rather than being faked
- Smooth transitions between animated and ragdoll states (death, knockback)
- CALM policy outputs become motor targets rather than kinematic overrides
- Physics feedback (contact forces, body velocities) feeds back into CALM observations for more realistic motion

### Architecture Overview

```
CALM Policy → target joint angles
                   ↓
         Ragdoll motor targets (DriveToPoseUsingMotors)
                   ↓
         Jolt physics step (constraints, collisions, gravity)
                   ↓
         Ragdoll body transforms
                   ↓
         Read back → SkeletonPose → GPU skinning
         Read back → CALMObservationExtractor (physics feedback)
```

---

## Phase R1: Ragdoll Body Builder

Create per-bone rigid bodies matching the engine's `Skeleton` hierarchy.

### R1.1 Collision Layer

Add a `RAGDOLL` collision layer to `JoltLayerConfig.h`:
- Ragdoll bones collide with `NON_MOVING` (terrain, walls) and `MOVING` (dynamic objects)
- Ragdoll bones do NOT collide with `CHARACTER` capsules (avoids fighting with the character controller)
- Adjacent ragdoll bones within the same ragdoll do NOT collide with each other (handled by Jolt's `DisableParentChildCollisions`)

### R1.2 Bone Shape Estimation

Each skeleton bone needs a collision shape. Build capsule shapes from the joint hierarchy:

```
For each joint with a parent:
    direction = childWorldPos - parentWorldPos
    length = glm::length(direction)
    shape = CapsuleShape(halfHeight = length/2 - radius, radius = estimated)
    position = midpoint between parent and child
    rotation = align capsule axis to direction vector
```

Radius estimation heuristic:
- Use a fraction of bone length (e.g. `length * 0.15`)
- Cap at reasonable min/max (0.02m – 0.15m)
- Override per-bone via config for specific body parts (torso wider, fingers thinner)

Store shape config per-archetype, not per-NPC. The `CALMCharacterConfig` already maps CALM DOFs to skeleton joints, so the bone set is well-defined.

### R1.3 RagdollBuilder Class

**File:** `src/physics/RagdollBuilder.h/cpp`

```cpp
struct BoneShapeOverride {
    float radius = -1.0f;       // -1 = auto-estimate
    float lengthScale = 1.0f;   // Scale the auto-computed length
    float massScale = 1.0f;     // Relative mass adjustment
};

struct RagdollConfig {
    float defaultBoneRadius = 0.05f;
    float radiusFraction = 0.15f;
    float totalMass = 70.0f;                        // kg, distributed by bone volume
    float linearDamping = 0.1f;
    float angularDamping = 0.3f;
    std::unordered_map<std::string, BoneShapeOverride> boneOverrides;
};

class RagdollBuilder {
public:
    // Build Jolt RagdollSettings from engine Skeleton + bind pose
    static JPH::Ref<JPH::RagdollSettings> build(
        const Skeleton& skeleton,
        const std::vector<glm::mat4>& globalBindPose,
        const RagdollConfig& config);
};
```

**Build process:**
1. Create a `JPH::Skeleton` mirroring the engine `Skeleton` joint hierarchy
2. For each joint: compute a capsule shape from parent-to-child direction and estimated radius
3. Create `JPH::BodyCreationSettings` per bone (dynamic, RAGDOLL layer)
4. Distribute mass proportional to bone volume (capsule volume = π·r²·(4r/3 + h))
5. Call `RagdollSettings::Stabilize()` to set mass/inertia for constraint stability
6. Call `RagdollSettings::DisableParentChildCollisions()` to prevent self-intersection
7. Return the settings (shared per-archetype, instantiated per-NPC)

### Incremental checkpoint
After R1: Create a ragdoll from a humanoid skeleton. Add it to the physics world. It collapses under gravity like a limp body. Bones have correct proportions and don't self-intersect.

---

## Phase R2: Joint Constraints with Motors

Connect ragdoll bones with motorised constraints matching anatomical joint limits.

### R2.1 Constraint Type Selection

Use `SwingTwistConstraint` for humanoid joints — it naturally models the swing cone + twist range of biological joints:
- **Swing**: how far the bone can deviate from its parent's axis (cone angle)
- **Twist**: how far it can rotate around its own axis

This maps cleanly to CALM's per-DOF action space where each DOF is a rotation axis.

### R2.2 Joint Limit Presets

Define anatomical limits per bone name, stored in the ragdoll config:

```cpp
struct JointLimitPreset {
    float swingYHalfAngle;   // radians — lateral swing
    float swingZHalfAngle;   // radians — forward/back swing
    float twistMin;           // radians
    float twistMax;           // radians
};
```

Default humanoid presets:
| Joint | Swing Y | Swing Z | Twist Min | Twist Max |
|-------|---------|---------|-----------|-----------|
| Neck | 30° | 40° | -30° | 30° |
| Spine | 20° | 30° | -20° | 20° |
| Shoulder | 90° | 80° | -90° | 90° |
| Elbow | 5° | 130° | -5° | 5° |
| Wrist | 30° | 60° | -40° | 40° |
| Hip | 80° | 100° | -30° | 30° |
| Knee | 5° | 130° | -5° | 5° |
| Ankle | 20° | 40° | -15° | 15° |

### R2.3 Motor Configuration

Each constraint gets motors configured for CALM-driven pose tracking:

```cpp
// Motor settings for CALM-driven joints
JPH::MotorSettings motorSettings;
motorSettings.mSpringSettings.mFrequency = 8.0f;    // Hz — responsiveness
motorSettings.mSpringSettings.mDamping = 0.8f;       // Critical damping ratio
motorSettings.SetTorqueLimit(maxTorque);              // Per-joint torque cap
```

The frequency/damping replace the explicit PD controller (kp/kd) from the CALM config — Jolt's motor spring IS a PD controller internally:
- `frequency` → controls proportional gain (like kp)
- `damping` → controls derivative gain (like kd)

This means the `CALMCharacterConfig::pdKp/pdKd` values can be converted to motor spring settings rather than implementing a separate PD controller.

### R2.4 Integration into RagdollBuilder

Extend `RagdollBuilder::build()`:
1. For each non-root joint, create a `SwingTwistConstraintSettings`
2. Set the constraint reference frame from the bind pose (parent-to-child orientation)
3. Apply joint limit presets by matching bone names
4. Configure motors with the spring settings
5. Assign to `RagdollSettings::Part::mToParent`
6. Call `CalculateConstraintPriorities()` for solver stability

### Incremental checkpoint
After R2: Ragdoll dropped from height lands and holds a T-pose via motors (all targets = bind pose). Joints bend on impact but spring back. Increasing gravity causes the ragdoll to sag — motors fight but can't fully overcome it. Adjusting motor strength changes the stiffness.

---

## Phase R3: Ragdoll Instance Manager

Manage ragdoll lifecycles and bridge between engine archetypes and Jolt ragdoll instances.

### R3.1 RagdollInstance

**File:** `src/physics/RagdollInstance.h/cpp`

```cpp
class RagdollInstance {
public:
    RagdollInstance(JPH::Ref<JPH::Ragdoll> ragdoll,
                    const Skeleton& skeleton);
    ~RagdollInstance();  // Removes from physics world

    // Set target pose from CALM actions (converted to SkeletonPose)
    // Motors drive toward this pose over time
    void driveToTargetPose(const SkeletonPose& targetPose);

    // Hard-set pose (for initialization or teleporting)
    void setPoseImmediate(const SkeletonPose& pose);

    // Read the current physics-resolved pose back
    void readPose(SkeletonPose& outPose) const;

    // Read body velocities for CALM observation
    void readBodyVelocities(std::vector<glm::vec3>& linearVels,
                            std::vector<glm::vec3>& angularVels) const;

    // Root transform (pelvis body)
    glm::vec3 getRootPosition() const;
    glm::quat getRootRotation() const;
    glm::vec3 getRootVelocity() const;

    // State management
    void activate();
    void deactivate();         // Remove from sim, hold last pose
    bool isActive() const;

    // Access for external impulses (hits, explosions)
    void addImpulse(int boneIndex, const glm::vec3& impulse);
    void addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& worldPos);

    // Motor strength control (for blend-to-ragdoll transitions)
    void setMotorStrength(float strength);  // 0=limp, 1=full CALM control

private:
    JPH::Ref<JPH::Ragdoll> ragdoll_;
    const Skeleton* skeleton_;

    // Index mapping: engine joint index → ragdoll body index
    std::vector<int> jointToBodyIndex_;
    std::vector<int> bodyToJointIndex_;
};
```

### R3.2 Archetype-Level Ragdoll Settings

Extend `CALMArchetype` (in `CALMAnimationIntegration.h`) to hold shared ragdoll settings:

```cpp
struct CALMArchetype {
    // ... existing fields ...
    JPH::Ref<JPH::RagdollSettings> ragdollSettings;  // Shared, built once
    RagdollConfig ragdollConfig;
};
```

### R3.3 Per-NPC Ragdoll Lifecycle

Extend `CALMNPCInstance` to optionally own a ragdoll:

```cpp
struct CALMNPCInstance {
    // ... existing fields ...
    std::unique_ptr<RagdollInstance> ragdoll;  // nullptr if kinematic-only
    bool usePhysics = false;                    // Toggle kinematic vs physics mode
};
```

LOD integration:
- **Real** (< 25m): Full ragdoll active, CALM motors every frame
- **Bulk** (25–50m): Ragdoll deactivated, use cached kinematic pose
- **Virtual** (> 50m): No ragdoll, no pose update

When transitioning from Bulk→Real, re-activate the ragdoll and `setPoseImmediate()` from the cached pose to avoid a pop.

### Incremental checkpoint
After R3: Spawn multiple NPCs with ragdolls. They hold their current animation pose via motors. Throwing a physics box at them causes physical reactions — they stumble, get pushed, then recover. NPCs beyond 25m have ragdolls deactivated to save performance.

---

## Phase R4: CALM–Ragdoll Integration

Wire the CALM controller to drive ragdoll motors instead of setting skeleton poses directly.

### R4.1 Modified CALMController Update Loop

The current `CALMController::update()` flow:
```
1. Extract observation from skeleton + CharacterController
2. Step latent (interpolation/resample)
3. LLC policy: (latent, obs) → actions
4. Clamp actions
5. Apply actions to skeleton (kinematic)
```

New physics-aware flow:
```
1. Read pose from ragdoll → observation skeleton
2. Extract observation from ragdoll pose + ragdoll velocities
3. Step latent
4. LLC policy: (latent, obs) → actions
5. Clamp actions
6. Convert actions to target SkeletonPose
7. ragdoll.driveToTargetPose(targetPose)
   (Jolt motors apply forces during next physics step)
8. After physics step: read back ragdoll pose for rendering
```

### R4.2 Physics-Aware Observation

Extend `CALMObservationExtractor` to accept ragdoll state instead of `CharacterController`:

```cpp
void extractFrameFromRagdoll(const RagdollInstance& ragdoll,
                              const Skeleton& skeleton,
                              float deltaTime);
```

Key differences from kinematic extraction:
- Root position/velocity: from ragdoll root body, not `CharacterController`
- DOF velocities: from ragdoll body angular velocities (exact), not finite differences (noisy approximation)
- Key body positions: from ragdoll body positions (physically accurate)

This gives the policy much better state information for balance and reactivity.

### R4.3 Root Motion Handling

The root body (pelvis) needs special treatment:
- CALM's root displacement drives the ragdoll root body
- But the ragdoll root must still collide with terrain
- Solution: the root body is a dynamic capsule that gets a velocity target from CALM rather than a position target
- Gravity and terrain contact modify the actual root trajectory

### R4.4 Action Application Mode Switch

Extend `CALMActionApplier` with a method that converts actions to a `SkeletonPose` without applying them directly:

```cpp
SkeletonPose actionsToTargetPose(const Tensor& actions,
                                  const Skeleton& skeleton) const;
```

The existing `applyToSkeleton()` remains for kinematic mode. The new method just builds the target pose that gets fed to `RagdollInstance::driveToTargetPose()`.

### R4.5 Dual-Mode Controller

`CALMController` supports both modes via the `usePhysics` flag on `CALMNPCInstance`:

```
if (instance.usePhysics && instance.ragdoll) {
    // Physics path: read ragdoll → observe → infer → drive motors
    controller.updatePhysics(dt, skeleton, *instance.ragdoll, outPose);
} else {
    // Kinematic path: observe → infer → set pose directly (existing)
    controller.update(dt, skeleton, physics, outPose);
}
```

### Incremental checkpoint
After R4: An NPC walks via CALM policy with ragdoll motors. Push it with a physics object — it stumbles but the policy tries to recover balance. The motion is physically grounded: feet contact terrain, body sways naturally. Switching `usePhysics` off/on shows the difference between kinematic and physics-driven animation.

---

## Phase R5: Character Controller Integration

Reconcile the ragdoll root body with the existing `CharacterController` capsule.

### R5.1 The Problem

Currently each NPC has a `CharacterVirtual` capsule for movement collision. With ragdoll, the pelvis body handles root position. These two can fight.

### R5.2 Solution: Capsule Follows Ragdoll

When ragdoll is active:
1. The `CharacterVirtual` capsule is repositioned each frame to match the ragdoll root body
2. The capsule provides ground detection (`isOnGround()`) which feeds into the observation
3. The capsule does NOT drive movement — the ragdoll root body does
4. If the ragdoll root moves to an invalid position (stuck in geometry), the capsule's depenetration handles recovery

When ragdoll is deactivated (LOD transition):
1. The capsule resumes normal movement control
2. The last ragdoll pose is cached for rendering

### R5.3 Implementation

Add to `RagdollInstance`:
```cpp
void syncCharacterController(CharacterController& controller) const;
```

This sets the character controller position to match the ragdoll root, keeping collision detection in sync.

### Incremental checkpoint
After R5: NPC with ragdoll walks up a slope — feet and body follow terrain properly. Standing on a moving platform, the ragdoll rides it. Walking off a ledge, the ragdoll falls and the capsule follows. Ground detection works correctly for the CALM observation.

---

## Phase R6: Animated-to-Ragdoll Transitions

Smooth blending between fully animated (kinematic) and fully physical (ragdoll) states.

### R6.1 Motor Strength Blending

`RagdollInstance::setMotorStrength(float)` scales the motor torque limits:
- `1.0` = full CALM control (normal gameplay)
- `0.0` = limp ragdoll (death, knockback)
- Intermediate values = partial control (staggering, recovering)

### R6.2 Transition Sequences

**Animated → Ragdoll (e.g. death, explosion):**
1. `setPoseImmediate(currentAnimPose)` — snap ragdoll to current visual pose
2. `activate()` — add ragdoll to physics world
3. `setMotorStrength(0.0)` — go limp
4. Apply impulse at hit point
5. Switch rendering to ragdoll-driven pose

**Ragdoll → Animated (e.g. getting up):**
1. Gradually increase `setMotorStrength()` from 0→1 over ~0.5s
2. CALM policy automatically produces a get-up motion (if trained for it)
3. Once stable and motors at full strength, optionally deactivate ragdoll

**Stagger/Hit Reaction:**
1. `setMotorStrength(0.3)` — partial control
2. Apply impulse from hit
3. CALM policy fights to maintain balance at reduced strength
4. Ramp `setMotorStrength()` back to 1.0 over ~0.3s

### Incremental checkpoint
After R6: Hit an NPC — it staggers physically then recovers balance. Kill an NPC — it goes fully limp and ragdolls. Ragdoll transitions are smooth with no pops or teleporting.

---

## Integration Points with Existing Code

### Files Modified
| File | Change |
|------|--------|
| `src/physics/JoltLayerConfig.h` | Add `RAGDOLL` layer + collision rules |
| `src/physics/PhysicsSystem.h/cpp` | Expose `JPH::PhysicsSystem*` for ragdoll creation |
| `src/ml/CALMObservation.h/cpp` | Add `extractFrameFromRagdoll()` |
| `src/ml/CALMActionApplier.h/cpp` | Add `actionsToTargetPose()` |
| `src/ml/CALMController.h/cpp` | Add `updatePhysics()` method |
| `src/ml/CALMAnimationIntegration.h/cpp` | Add ragdoll fields to archetype/instance |
| `src/ml/CALMCharacterConfig.h` | Add motor spring config (frequency, damping) per joint |

### Files Created
| File | Purpose |
|------|---------|
| `src/physics/RagdollBuilder.h/cpp` | Build `JPH::RagdollSettings` from engine Skeleton |
| `src/physics/RagdollInstance.h/cpp` | Per-NPC ragdoll lifecycle, pose I/O, motor control |
| `src/physics/JointLimitPresets.h` | Default humanoid joint limits |
| `tests/test_ragdoll.cpp` | Tests for builder, instance, pose round-trip |

### Files NOT Modified
- `CALMLowLevelController` — unchanged, still outputs action tensors
- `CALMLatentSpace` — unchanged, latent management is orthogonal
- `CALMHighLevelController` — unchanged, task→latent mapping is orthogonal
- `CALMBehaviorFSM` — unchanged, state machine is orthogonal
- `CALMGPUInference` — unchanged, GPU batching is orthogonal
- `MLPNetwork`, `ModelLoader`, `Tensor` — unchanged, inference infrastructure is orthogonal

### Data Flow: Before and After

**Before (kinematic):**
```
CharacterController.getPosition() ──→ observation
CharacterController.getVelocity() ──→ observation
Skeleton.joints[i].localTransform ──→ observation
                                         ↓
                                    CALM policy
                                         ↓
                                    actions (joint angles)
                                         ↓
                              CALMActionApplier.applyToSkeleton()
                                         ↓
                                    SkeletonPose (kinematic)
                                         ↓
                                    GPU skinning
```

**After (physics):**
```
RagdollInstance.getRootPosition()  ──→ observation
RagdollInstance.getRootVelocity()  ──→ observation
RagdollInstance.readPose()         ──→ observation (joint angles from physics)
RagdollInstance.readBodyVelocities() → observation (exact angular velocities)
                                         ↓
                                    CALM policy
                                         ↓
                                    actions (joint angles)
                                         ↓
                              CALMActionApplier.actionsToTargetPose()
                                         ↓
                              RagdollInstance.driveToTargetPose()
                                         ↓
                                    Jolt physics step
                                         ↓
                              RagdollInstance.readPose() → SkeletonPose
                                         ↓
                                    GPU skinning
```

---

## Performance Considerations

- Ragdoll bodies per NPC: ~15–20 (humanoid)
- Constraints per NPC: ~14–19
- Target: support 20–30 simultaneous ragdolls at 60fps
- LOD deactivation keeps ragdoll count bounded
- `RagdollSettings` are shared per archetype — only `Ragdoll` instances are per-NPC
- Consider using Jolt's `MotionQuality::LinearCast` for fast-moving ragdoll parts to avoid tunnelling
