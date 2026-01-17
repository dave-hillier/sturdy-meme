#pragma once

#include "HierarchicalPose.h"
#include "NodeMask.h"
#include "PoseBlend.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

// A single LOD layer that can blend with other layers.
// Similar to animation layers but for LOD transitions.
struct LODLayer {
    std::string name;
    float weight{1.0f};             // Layer weight [0, 1]
    NodeMask nodeMask;              // Per-node influence
    BlendMode blendMode{BlendMode::Override};
    bool enabled{true};

    // Source pose for this layer (populated by the layer's animation/pose system)
    HierarchyPose pose;

    LODLayer() = default;
    LODLayer(const std::string& name, size_t nodeCount)
        : name(name), nodeMask(nodeCount, 1.0f) {}
};

// Controls multiple LOD layers for smooth transitions.
// Enables staggered blending where different parts (e.g., leaves, branches, trunk)
// can fade at different rates during LOD transitions.
class LODLayerController {
public:
    LODLayerController() = default;

    // Initialize with number of nodes in the hierarchy
    void initialize(size_t nodeCount);

    // Layer management
    LODLayer* addLayer(const std::string& name);
    LODLayer* getLayer(const std::string& name);
    const LODLayer* getLayer(const std::string& name) const;
    void removeLayer(const std::string& name);
    bool hasLayer(const std::string& name) const;

    // Get all layers (ordered from bottom to top)
    const std::vector<LODLayer>& getLayers() const { return layers_; }
    std::vector<LODLayer>& getLayers() { return layers_; }

    // Set overall LOD blend factor [0 = full detail, 1 = full simplified]
    // This can be used to drive layer weights through the stagger function
    void setLODBlendFactor(float factor);
    float getLODBlendFactor() const { return lodBlendFactor_; }

    // Configure staggered blending for a layer
    // startFactor: LOD factor at which this layer starts fading (0-1)
    // endFactor: LOD factor at which this layer is fully faded (0-1)
    // Example: leaves (0.0, 0.6), branches (0.4, 0.9), trunk (0.7, 1.0)
    void setLayerStagger(const std::string& layerName, float startFactor, float endFactor);

    // Compute the final blended pose from all enabled layers
    // basePose: The rest/default pose to start from
    // outPose: Output pose with all layers applied
    void computeFinalPose(const HierarchyPose& basePose, HierarchyPose& outPose) const;

    // Convenience: compute final pose and return it
    HierarchyPose computeFinalPose(const HierarchyPose& basePose) const;

    // Update layer weights based on current LOD blend factor and stagger settings
    void updateLayerWeights();

    // Get effective weight for a specific node (considering layer weights and masks)
    float getEffectiveNodeWeight(size_t nodeIndex, const std::string& layerName) const;

    // Preset configurations for common LOD blending patterns
    // Creates layers with appropriate masks and stagger settings

    // Tree LOD: leaves fade first, then outer branches, then trunk
    // Requires a skeleton with branch levels
    void configureTreeLOD(const std::vector<int>& nodeLevels, int maxLevel);

    // Character LOD: extremities fade first (hands/feet), then limbs, then torso
    // Requires a skeleton with depth information
    void configureCharacterLOD(const std::vector<int>& nodeDepths, int maxDepth);

    // Simple linear: all nodes fade together (default behavior)
    void configureLinearLOD(size_t nodeCount);

private:
    std::vector<LODLayer> layers_;
    std::unordered_map<std::string, size_t> layerIndices_;
    size_t nodeCount_{0};
    float lodBlendFactor_{0.0f};

    // Stagger configuration per layer
    struct StaggerConfig {
        float startFactor{0.0f};
        float endFactor{1.0f};
    };
    std::unordered_map<std::string, StaggerConfig> staggerConfigs_;

    // Calculate layer weight from LOD factor and stagger config
    float calculateStaggeredWeight(float lodFactor, const StaggerConfig& config) const;
};
