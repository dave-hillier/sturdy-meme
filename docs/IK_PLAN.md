# Character IK Implementation Plan

This document outlines a phased approach to implementing Inverse Kinematics (IK) for character animation.

## Current State

The codebase has solid skeletal animation infrastructure:
- Skeleton hierarchy with joint transforms (`GLTFLoader.h`)
- Animation sampling and blending (`Animation.h/cpp`)
- Animation state machine for locomotion (`AnimationStateMachine.h/cpp`)
- GPU skinning pipeline (`skinned.vert`)
- glTF/FBX character loading

**No IK exists** - all phases build from scratch.

---

## Phase 1: IK Foundation

**Goal:** Basic infrastructure and simplest IK solver

- Create `IKSolver.h/cpp` with base solver interface
- Implement **Two-Bone IK** analytical solver (arms/legs)
  - Takes root, mid, end bone + target position
  - Handles pole vector for elbow/knee direction
- Add **joint limit constraints** (min/max angles per axis)
- Create debug visualization for IK chains and targets
- Integrate into `AnimatedCharacter::update()` post-animation sampling

**Deliverable:** Arms/legs can reach arbitrary target positions

---

## Phase 2: Look-At IK

**Goal:** Head and eyes track targets naturally

- Implement **look-at constraint** for single bone (head)
- Add **multi-bone look-at** (spine chain contribution)
- Weight blending between animation and IK
- Smooth interpolation to/from targets
- Angle limits to prevent unnatural neck rotation

**Deliverable:** Characters look at points of interest, other NPCs

---

## Phase 3: Foot Placement IK

**Goal:** Feet conform to terrain

- Add terrain raycasting from foot positions
- Implement **foot IK** using two-bone solver on legs
- Add **hip height adjustment** based on leg positions
- Implement **foot rotation** to match surface normal
- Add **foot locking** during stance phase (prevent sliding)
- Blend with locomotion animations

**Deliverable:** Characters stand naturally on slopes, stairs, uneven ground

---

## Phase 4: FABRIK Solver

**Goal:** Multi-joint chain solver for complex rigs

- Implement **FABRIK** (Forward And Backward Reaching IK)
  - Iterative solver, handles any chain length
  - Multiple end effectors support
- Add **spine IK** for reaching/bending
- Implement **tail/tentacle** procedural animation
- Chain priority system for competing constraints

**Deliverable:** Torso bends to reach, procedural appendages

---

## Phase 5: Hand IK & Interaction

**Goal:** Hands interact with world geometry

- Implement **hand placement** on surfaces (walls, railings)
- Add **grip IK** for holding objects of varying sizes
- Two-handed weapon/object handling
- Door handles, switches, levers
- Object carrying pose adjustment

**Deliverable:** Characters naturally grip and interact with objects

---

## Phase 6: Aim IK

**Goal:** Upper body adjusts for aiming

- Implement **aim constraint** with spine twist distribution
- Weapon pointing at targets
- Blend zones (lower body follows locomotion, upper body aims)
- Recoil/sway procedural animation
- Smooth transitions in/out of aiming

**Deliverable:** Characters aim weapons while moving

---

## Phase 7: Full-Body IK & Balance

**Goal:** Whole body procedural adjustments

- **Center of mass** calculation
- **Balance system** - weight distribution between feet
- **Lean/tilt** on angled surfaces
- **Stumble/recovery** from pushes
- Partial **ragdoll blending** with IK constraints

**Deliverable:** Characters maintain balance dynamically

---

## Phase 8: Climbing IK

**Goal:** Procedural climbing on geometry

- Implement **hold detection** (raycasting for hand/foot positions)
- Four-point IK (both hands, both feet)
- **Reach planning** - determine next hold
- Body positioning relative to holds
- Transition animations between holds
- Ledge grabbing and mantling

**Deliverable:** Characters climb arbitrary climbable geometry

---

## Phase Dependencies

| Phase | Feature | Builds On |
|-------|---------|-----------|
| 1 | Two-Bone IK + Foundation | Existing skeleton |
| 2 | Look-At IK | Phase 1 |
| 3 | Foot Placement | Phase 1 |
| 4 | FABRIK Solver | Phase 1 |
| 5 | Hand IK | Phase 1, 4 |
| 6 | Aim IK | Phase 2, 4 |
| 7 | Full-Body Balance | Phase 3, 4 |
| 8 | Climbing | Phase 5, 7 |

---

## Integration Points

IK will integrate with existing systems at these points:

1. **`Skeleton::computeGlobalTransforms()`** - Post-IK solving, compute finals
2. **`AnimationClip::sample()`** - Apply sampled animation before IK pass
3. **`AnimationStateMachine`** - Add IK blend modes/constraints
4. **`AnimatedCharacter::update()`** - Insert IK solving step after animation sampling
5. **Bone matrix computation** - Apply IK-modified transforms before skinning
6. **Renderer** - Debug visualization for IK targets/constraints

---

## AAA IK Features Reference

For reference, here are IK features commonly found in AAA games:

### Basic IK
- Look-at IK (head/eyes track targets)
- Aim IK (upper body adjusts for weapon aiming)
- Hand IK (hands grip objects, railings)
- Pointing/Gesturing

### Foot/Locomotion IK
- Foot placement on uneven terrain
- Foot locking (prevent sliding)
- Hip adjustment for slopes
- Lean/tilt on angled surfaces

### Advanced Full-Body IK
- Climbing IK (Uncharted, Assassin's Creed style)
- Cover system IK
- Ladder IK
- Ledge grabbing

### Physics-Driven IK
- Ragdoll blending with IK constraints
- Hit reactions
- Balance/stumble recovery

### Interaction IK
- Seat adjustment for vehicles/chairs
- Weapon handling (two-handed grips, reloading)
- Carrying objects
- NPC interaction (handshakes, pushing, pulling)

### Solver Types
- **Two-Bone IK** - Analytical, fast, for limbs
- **FABRIK** - Fast iterative, multi-joint chains
- **CCD** (Cyclic Coordinate Descent) - Another iterative approach
