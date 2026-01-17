# Hierarchical Pose System

Shared infrastructure for hierarchical animation across skeletal characters and trees. Enables code reuse between animation blending and tree LOD transitions.

## Architecture

```
Core Types (src/core/)
├── HierarchicalPose.h    - NodePose, HierarchyPose
├── PoseBlend.h           - Blending functions (lerp, slerp, additive)
├── NodeMask.h            - Per-node weighting
├── LODLayerController.h  - Multi-layer LOD blending
└── AnimatedHierarchy.h   - Type-erased unified interface

Animation (src/animation/)
├── AnimationBlend.h      - Type aliases (BonePose = NodePose)
└── BoneMask.h            - Composes NodeMask with skeleton factories

Vegetation (src/vegetation/)
├── TreeSkeleton.h        - Tree branch hierarchy
├── TreeWindPose.h        - CPU-side wind animation
└── TreeAnimatedHierarchy.h - Tree-specific adapter
```

## Core Types

### NodePose
Single node transform with translation, rotation (quaternion), and scale:

```cpp
struct NodePose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity
    glm::vec3 scale{1.0f};

    glm::mat4 toMatrix() const;
    static NodePose fromMatrix(const glm::mat4& matrix);
    static NodePose identity();
};
```

### HierarchyPose
Container of NodePose for entire hierarchy. Provides vector-like access:

```cpp
struct HierarchyPose {
    std::vector<NodePose> nodePoses;

    void resize(size_t count);
    size_t size() const;
    bool empty() const;
    NodePose& operator[](size_t i);
    // Iterator support: begin(), end()
};
```

### PoseBlend
Blending operations for poses:

```cpp
namespace PoseBlend {
    // Interpolation
    NodePose blend(const NodePose& a, const NodePose& b, float t);
    void blend(const HierarchyPose& a, const HierarchyPose& b, float t, HierarchyPose& out);

    // Masked blending (per-node weights)
    void blendMasked(const HierarchyPose& a, const HierarchyPose& b,
                     const std::vector<float>& nodeWeights, HierarchyPose& out);

    // Additive blending
    NodePose additive(const NodePose& base, const NodePose& delta, float weight = 1.0f);
    void additiveMasked(const HierarchyPose& base, const HierarchyPose& delta,
                        const std::vector<float>& nodeWeights, HierarchyPose& out);
}
```

### NodeMask
Per-node weighting for selective animation:

```cpp
class NodeMask {
    float getWeight(size_t nodeIndex) const;
    void setWeight(size_t nodeIndex, float weight);
    NodeMask inverted() const;

    // Factory methods
    static NodeMask fromDepthRange(size_t count, const std::vector<int>& depths,
                                   int minDepth, int maxDepth);
    static NodeMask fromSubtree(size_t count, const std::vector<int32_t>& parents,
                                const std::vector<size_t>& rootNodes);
};
```

## LOD Layer System

### LODLayerController
Manages multiple animation layers with staggered LOD blending:

```cpp
LODLayerController controller;
controller.initialize(nodeCount);

// Add layers with different fade timings
LODLayer* outer = controller.addLayer("outer_branches");
controller.setLayerStagger("outer_branches", 0.0f, 0.6f);  // Fades first

LODLayer* trunk = controller.addLayer("trunk");
controller.setLayerStagger("trunk", 0.6f, 1.0f);  // Fades last

// Update based on distance/LOD factor
controller.setLODBlendFactor(lodFactor);  // 0=full detail, 1=simplified

// Compute blended pose
HierarchyPose result = controller.computeFinalPose(basePose);
```

### Preset Configurations

```cpp
// Tree LOD: outer branches fade first, trunk last
controller.configureTreeLOD(nodeLevels, maxLevel);

// Character LOD: extremities fade first, core last
controller.configureCharacterLOD(nodeDepths, maxDepth);

// Simple linear: all nodes fade together
controller.configureLinearLOD(nodeCount);
```

### Stagger Timing
Each layer has start/end factors controlling when it fades:

```
LOD Factor:  0.0 -------- 0.5 -------- 1.0
             Full Detail            Simplified

Outer:       [====fade====]              (0.0 - 0.6)
Primary:          [====fade====]         (0.3 - 0.8)
Trunk:                   [====fade====]  (0.6 - 1.0)
```

## Tree Animation

### TreeSkeleton
Represents tree branch hierarchy:

```cpp
struct TreeBranch {
    std::string name;
    int32_t parentIndex;  // -1 for root
    glm::mat4 restPoseLocal;
    float radius, length;
    int level;  // 0=trunk, 1=primary, 2+=outer
};

class TreeSkeleton {
    static TreeSkeleton fromTreeMeshData(const TreeMeshData& data);
    HierarchyPose getRestPose() const;
    NodeMask trunkMask() const;       // Level 0 only
    NodeMask flexibilityMask() const; // Weights by level
};
```

### TreeWindPose
CPU-side wind animation matching GPU shader:

```cpp
// Calculate per-tree oscillation
TreeOscillation osc = TreeWindPose::calculateOscillation(worldPos, windParams);

// Generate wind pose deltas
HierarchyPose windPose = TreeWindPose::calculateWindPose(skeleton, osc, windParams);

// Or with custom flexibility
NodeMask flex = skeleton.flexibilityMask();
HierarchyPose windPose = TreeWindPose::calculateWindPoseMasked(skeleton, osc, windParams, flex);
```

### TreeAnimatedHierarchy
Complete tree animation with wind and LOD:

```cpp
TreeAnimatedHierarchy treeAnim;
treeAnim.initialize(skeleton, worldPosition);
treeAnim.enableWindLayer();  // Add wind as additive layer
treeAnim.setWindParams(windParams);
treeAnim.setLODFactor(0.5f);

// Each frame
treeAnim.update(deltaTime);
HierarchyPose pose = treeAnim.currentPose();
```

## Unified Interface

### AnimatedHierarchy
Type-erased wrapper using composition (not inheritance):

```cpp
// Create from callbacks
AnimatedHierarchy anim(
    []() { return nodeCount; },
    []() { return restPose; },
    []() { return currentPose; },
    [](float dt) { /* update */ }
);

// Or convert from specific types
AnimatedHierarchy anim = treeAnim.toAnimatedHierarchy();

// Uniform interface
anim.update(deltaTime);
HierarchyPose pose = anim.computeFinalPose();
```

## Animation System Integration

The skeletal animation system uses type aliases for compatibility:

```cpp
// In AnimationBlend.h
using BonePose = NodePose;
using SkeletonPose = HierarchyPose;

// BoneMask composes NodeMask
class BoneMask {
    NodeMask mask_;  // Internal composition

    // Skeleton-specific factories
    static BoneMask upperBody(const Skeleton& skeleton);
    static BoneMask fromBoneNames(const Skeleton& skeleton, ...);
};
```

## Design Principles

1. **Composition over inheritance**: `BoneMask` composes `NodeMask`, `AnimatedHierarchy` uses callbacks
2. **Type aliases for compatibility**: `BonePose = NodePose` maintains existing API
3. **Generic core, specific adapters**: Core types are domain-agnostic; animation/tree code provides factories
4. **Staggered LOD blending**: Different hierarchy levels fade at different rates for smooth transitions
