# Plan: Animated Player Character with Skeletal Animation

## Overview

Replace the player capsule with an animated humanoid character supporting idle, walk, run, and jump animations. This requires implementing:
- glTF model loading with skeletal data
- GPU skinning pipeline
- Animation state machine with blending

## Character Asset

**Model Source**: [Quaternius Universal Base Characters](https://quaternius.itch.io/universal-base-characters) or [Poly.Pizza Animated Human](https://poly.pizza/m/c3Ibh9I3udk)
- Low-poly humanoid character in glTF format
- CC0 license (public domain)
- Compatible with Universal Animation Library

**Animation Source**: [Quaternius Universal Animation Library](https://quaternius.itch.io/universal-animation-library)
- 120+ animations including locomotion (walk, run, jump, idle)
- CC0 license
- Compatible with Universal Base Characters rig

**Setup**:
1. Download character model from Quaternius or Poly.Pizza
2. Download animation library from Quaternius
3. Place character model as `assets/characters/player.glb`
4. Place animation files in `assets/characters/animations/`

**Known Issues**:
- Quaternius models are very small (~8cm tall), require ~20x scale to be human-sized
- Some models may need Y-axis rotation to face correct direction

## Progress

### Phase 1: COMPLETE
- glTF loader implemented using fastgltf library
- Mesh extraction working (positions, normals, UVs, tangents)
- Skeleton data structures defined (Joint, Skeleton)
- Character renders in T-pose at player position
- 20x scale applied to compensate for small model size

### Phases 2-4: COMPLETE
- SkinnedVertex format with bone indices and weights
- SkinnedMesh class for GPU-ready skinned meshes
- Skinned vertex shader and shadow shader created
- Animation data loading from glTF (8 animations: Idle, Walk, Run, Jump, Punch, Death, Working)
- AnimationPlayer class for playback with looping
- AnimatedCharacter class combining mesh + skeleton + animations
- CPU skinning implemented and working
- Character animates with Idle animation by default
- Fixed bind pose reset issue (limbs were knotted into body)

### Phase 5: COMPLETE
- AnimationStateMachine class for state-based animation selection
- Crossfade blending between animation states (0.15-0.25s transitions)
- Automatic state transitions based on movement speed:
  - idle when speed < 0.1
  - walk when speed 0.1-3.0
  - run when speed > 3.0
  - jump when jumping (non-looping, returns to locomotion on land)
- Connected to player physics system for ground detection
- Character smoothly transitions between idle/walk/run/jump based on player input

## Implementation Phases

Each phase results in a working, renderable state.

---

### Phase 1: glTF Static Mesh Loading (COMPLETE)

**Goal**: Load and display character model in T-pose using existing PBR pipeline.

**Files to create**:
- `src/GLTFLoader.h/cpp` - Parse glTF using fastgltf library

**Files to modify**:
- `vcpkg.json` - Add fastgltf dependency
- `CMakeLists.txt` - Add fastgltf, new source files
- `src/SceneBuilder.cpp` - Load character mesh, keep capsule as fallback

**Key tasks**:
1. Add fastgltf to vcpkg.json
2. Create GLTFLoader that extracts mesh data (positions, normals, UVs, tangents)
3. Convert to existing `Mesh` format (ignore skeleton initially)
4. Add character mesh to scene at player position

**Visible result**: Character in T-pose renders at player position.

---

### Phase 2: Skeleton Data Structures

**Goal**: Load skeleton hierarchy and optionally visualize bones for debugging.

**Files to create**:
- `src/Skeleton.h/cpp` - Joint hierarchy, inverse bind matrices, global transform computation

**Files to modify**:
- `src/GLTFLoader.cpp` - Extract skin data (joints, inverse bind matrices)

**Data structures**:
```cpp
struct Joint {
    std::string name;
    int32_t parentIndex;  // -1 for root
    glm::mat4 inverseBindMatrix;
    glm::mat4 localTransform;
};

struct Skeleton {
    std::vector<Joint> joints;
    void computeGlobalTransforms(std::vector<glm::mat4>& outGlobalTransforms);
};
```

**Visible result**: Skeleton data loaded, debug bone visualization available.

---

### Phase 3: Skinned Vertex Format & GPU Pipeline

**Goal**: Extend vertex format with bone weights, create skinned rendering pipeline.

**Files to create**:
- `src/SkinnedVertex.h` - Extended vertex format
- `src/SkinnedMesh.h/cpp` - Mesh with skinning data and skeleton reference
- `shaders/skinned.vert` - GPU skinning vertex shader
- `shaders/skinned_shadow.vert` - Shadow pass for skinned meshes

**Files to modify**:
- `src/Renderer.h/cpp` - Add skinned pipeline, bone buffer (binding 10)
- `src/ShadowSystem.h/cpp` - Support skinned mesh shadow rendering
- `CMakeLists.txt` - Add new shaders to compilation

**SkinnedVertex format**:
```cpp
struct SkinnedVertex {
    glm::vec3 position;     // location 0
    glm::vec3 normal;       // location 1
    glm::vec2 texCoord;     // location 2
    glm::vec4 tangent;      // location 3
    glm::uvec4 boneIndices; // location 4 (4 bone influences)
    glm::vec4 boneWeights;  // location 5
};
```

**Bone matrix storage**: UBO at binding 10, max 128 bones

**Visible result**: Character renders in bind pose using skinned pipeline (identical to Phase 1 visually).

---

### Phase 4: Animation Data & Playback

**Goal**: Load animation clips and play single animation.

**Files to create**:
- `src/Animation.h/cpp` - Animation clips, keyframes, sampling
- `src/AnimationPlayer.h/cpp` - Time-based animation playback

**Files to modify**:
- `src/GLTFLoader.cpp` - Extract animation data from glTF

**Data structures**:
```cpp
struct AnimationChannel {
    int32_t jointIndex;
    std::vector<float> times;
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
};

struct AnimationClip {
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;

    void sample(float time, Skeleton& skeleton);
};

class AnimationPlayer {
    void setAnimation(AnimationClip* clip);
    void update(float deltaTime);
    void applyToSkeleton(Skeleton& skeleton);
};
```

**Visible result**: Character plays idle animation in a loop.

---

### Phase 5: Animation State Machine

**Goal**: Blend between animations based on player movement state.

**Files to create**:
- `src/AnimationStateMachine.h/cpp` - States, transitions, crossfade blending

**Files to modify**:
- `src/Player.h/cpp` - Connect movement to animation state machine

**State machine**:
```cpp
struct AnimationState {
    std::string name;
    AnimationClip* clip;
    float speed;
    bool looping;
};

struct AnimationTransition {
    std::string from, to;
    float blendDuration;
    std::function<bool()> condition;
};

class AnimationStateMachine {
    void addState(AnimationState state);
    void addTransition(AnimationTransition transition);
    void update(float deltaTime, Skeleton& skeleton);

    // Input for conditions
    float movementSpeed;
    bool isGrounded;
    bool isJumping;
};
```

**Transitions**:
- idle -> walk: speed > 0.1
- walk -> run: speed > 3.0
- walk/run -> idle: speed < 0.1
- any -> jump: isJumping && !isGrounded
- jump -> idle/walk: isGrounded

**Visible result**: Character smoothly transitions between idle/walk/run/jump based on player input.

---

### Phase 6: Integration & Polish

**Goal**: Full integration with player system, shadows, final polish.

**Files to modify**:
- `src/Player.h/cpp` - Own AnimatedCharacter, update animation each frame
- `src/SceneBuilder.cpp` - Final character setup
- `src/Renderer.cpp` - Render skinned character with shadows

**Tasks**:
1. Create `AnimatedCharacter` class combining SkinnedMesh + Skeleton + AnimationStateMachine
2. Replace capsule mesh pointer with character in player's Renderable
3. Update bone matrices each frame before rendering
4. Ensure shadow mapping works correctly
5. Remove capsule fallback code once stable

**Visible result**: Fully animated player character with proper shadows.

---

## File Structure Summary

```
src/
  GLTFLoader.h/cpp           # glTF parsing (fastgltf)
  Skeleton.h/cpp             # Joint hierarchy and transforms
  Animation.h/cpp            # Animation clips and sampling
  AnimationPlayer.h/cpp      # Single animation playback
  AnimationStateMachine.h/cpp # State-based blending
  SkinnedVertex.h            # Extended vertex format
  SkinnedMesh.h/cpp          # Mesh with skinning data
  AnimatedCharacter.h/cpp    # High-level character class

shaders/
  skinned.vert               # GPU skinning vertex shader
  skinned_shadow.vert        # Shadow pass skinning

assets/
  characters/
    player.glb               # Character model with animations
```

## Critical Files to Read Before Implementation

- [Mesh.h](src/Mesh.h) - Current Vertex struct, buffer patterns
- [shader.vert](shaders/shader.vert) - Current vertex shader, UBO layout
- [Renderer.cpp](src/Renderer.cpp) - Pipeline creation, descriptor sets, render loop
- [SceneBuilder.cpp](src/SceneBuilder.cpp) - Player capsule creation at index 8-9
- [Player.h](src/Player.h) - Player state (position, velocity, jumping)
- [ShadowSystem.h](src/ShadowSystem.h) - Shadow pass callback pattern
- [CMakeLists.txt](CMakeLists.txt) - Build system, shader compilation

## Dependencies

- **fastgltf** - glTF 2.0 loader (via vcpkg)
  - Modern C++17, header-only option
  - Fast SIMD JSON parsing
  - No additional dependencies

## Technical Notes

- **Bone limit**: 128 bones (UBO at binding 10)
- **Bone influences per vertex**: 4
- **Animation interpolation**: Linear for translation/scale, SLERP for rotation
- **Crossfade blending**: 0.15-0.25 seconds between states
- **Vertex shader skinning**: Transform position, normal, tangent by weighted bone matrices
