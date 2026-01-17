#include "LODLayerController.h"
#include <algorithm>
#include <cmath>

void LODLayerController::initialize(size_t nodeCount) {
    nodeCount_ = nodeCount;
    layers_.clear();
    layerIndices_.clear();
    staggerConfigs_.clear();
    lodBlendFactor_ = 0.0f;
}

LODLayer* LODLayerController::addLayer(const std::string& name) {
    if (hasLayer(name)) {
        return getLayer(name);
    }

    layers_.emplace_back(name, nodeCount_);
    layerIndices_[name] = layers_.size() - 1;

    // Default stagger: full range
    staggerConfigs_[name] = StaggerConfig{0.0f, 1.0f};

    return &layers_.back();
}

LODLayer* LODLayerController::getLayer(const std::string& name) {
    auto it = layerIndices_.find(name);
    if (it != layerIndices_.end()) {
        return &layers_[it->second];
    }
    return nullptr;
}

const LODLayer* LODLayerController::getLayer(const std::string& name) const {
    auto it = layerIndices_.find(name);
    if (it != layerIndices_.end()) {
        return &layers_[it->second];
    }
    return nullptr;
}

void LODLayerController::removeLayer(const std::string& name) {
    auto it = layerIndices_.find(name);
    if (it == layerIndices_.end()) {
        return;
    }

    size_t index = it->second;
    layers_.erase(layers_.begin() + static_cast<long>(index));
    layerIndices_.erase(it);
    staggerConfigs_.erase(name);

    // Update indices for layers after the removed one
    for (auto& pair : layerIndices_) {
        if (pair.second > index) {
            pair.second--;
        }
    }
}

bool LODLayerController::hasLayer(const std::string& name) const {
    return layerIndices_.find(name) != layerIndices_.end();
}

void LODLayerController::setLODBlendFactor(float factor) {
    lodBlendFactor_ = std::clamp(factor, 0.0f, 1.0f);
    updateLayerWeights();
}

void LODLayerController::setLayerStagger(const std::string& layerName, float startFactor, float endFactor) {
    staggerConfigs_[layerName] = StaggerConfig{
        std::clamp(startFactor, 0.0f, 1.0f),
        std::clamp(endFactor, 0.0f, 1.0f)
    };
}

float LODLayerController::calculateStaggeredWeight(float lodFactor, const StaggerConfig& config) const {
    // Before start: full weight (1.0)
    // Between start and end: linear fade
    // After end: zero weight (0.0)

    if (lodFactor <= config.startFactor) {
        return 1.0f;
    }
    if (lodFactor >= config.endFactor) {
        return 0.0f;
    }

    float range = config.endFactor - config.startFactor;
    if (range < 0.001f) {
        return 0.0f;
    }

    float t = (lodFactor - config.startFactor) / range;
    return 1.0f - t;
}

void LODLayerController::updateLayerWeights() {
    for (auto& layer : layers_) {
        auto it = staggerConfigs_.find(layer.name);
        if (it != staggerConfigs_.end()) {
            layer.weight = calculateStaggeredWeight(lodBlendFactor_, it->second);
        }
    }
}

void LODLayerController::computeFinalPose(const HierarchyPose& basePose, HierarchyPose& outPose) const {
    if (basePose.empty()) {
        outPose.clear();
        return;
    }

    // Start with base pose
    outPose = basePose;

    // Apply each enabled layer in order
    for (const auto& layer : layers_) {
        if (!layer.enabled || layer.weight <= 0.0f || layer.pose.empty()) {
            continue;
        }

        // Ensure pose sizes match
        size_t count = std::min({outPose.size(), layer.pose.size(), layer.nodeMask.size()});

        for (size_t i = 0; i < count; ++i) {
            float nodeWeight = layer.weight * layer.nodeMask.getWeight(i);
            if (nodeWeight <= 0.0f) {
                continue;
            }

            if (layer.blendMode == BlendMode::Override) {
                // Override: blend from current toward layer pose
                outPose[i] = PoseBlend::blend(outPose[i], layer.pose[i], nodeWeight);
            } else {
                // Additive: add layer pose delta on top
                outPose[i] = PoseBlend::additive(outPose[i], layer.pose[i], nodeWeight);
            }
        }
    }
}

HierarchyPose LODLayerController::computeFinalPose(const HierarchyPose& basePose) const {
    HierarchyPose result;
    computeFinalPose(basePose, result);
    return result;
}

float LODLayerController::getEffectiveNodeWeight(size_t nodeIndex, const std::string& layerName) const {
    const LODLayer* layer = getLayer(layerName);
    if (!layer || !layer->enabled) {
        return 0.0f;
    }
    return layer->weight * layer->nodeMask.getWeight(nodeIndex);
}

void LODLayerController::configureTreeLOD(const std::vector<int>& nodeLevels, int maxLevel) {
    initialize(nodeLevels.size());

    // Create layers for different tree parts
    // Leaves/outer branches fade first, trunk fades last

    // Layer 1: Outer branches (levels 2+) - fade first
    LODLayer* outerLayer = addLayer("outer_branches");
    outerLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeLevels, 2, maxLevel);
    setLayerStagger("outer_branches", 0.0f, 0.6f);

    // Layer 2: Primary branches (level 1) - fade mid
    LODLayer* primaryLayer = addLayer("primary_branches");
    primaryLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeLevels, 1, 1);
    setLayerStagger("primary_branches", 0.3f, 0.8f);

    // Layer 3: Trunk (level 0) - fade last
    LODLayer* trunkLayer = addLayer("trunk");
    trunkLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeLevels, 0, 0);
    setLayerStagger("trunk", 0.6f, 1.0f);
}

void LODLayerController::configureCharacterLOD(const std::vector<int>& nodeDepths, int maxDepth) {
    initialize(nodeDepths.size());

    // Create layers based on depth from root
    // Extremities (high depth) fade first, torso (low depth) fades last

    // Layer 1: Extremities (hands, feet, fingers) - fade first
    LODLayer* extremitiesLayer = addLayer("extremities");
    int extremityMinDepth = maxDepth - 2;
    extremitiesLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeDepths, extremityMinDepth, maxDepth);
    setLayerStagger("extremities", 0.0f, 0.5f);

    // Layer 2: Limbs (arms, legs) - fade mid
    LODLayer* limbsLayer = addLayer("limbs");
    int limbMinDepth = maxDepth / 2;
    limbsLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeDepths, limbMinDepth, extremityMinDepth - 1);
    setLayerStagger("limbs", 0.3f, 0.7f);

    // Layer 3: Core (spine, hips) - fade last
    LODLayer* coreLayer = addLayer("core");
    coreLayer->nodeMask = NodeMask::fromDepthRange(nodeCount_, nodeDepths, 0, limbMinDepth - 1);
    setLayerStagger("core", 0.5f, 1.0f);
}

void LODLayerController::configureLinearLOD(size_t nodeCount) {
    initialize(nodeCount);

    // Single layer that fades uniformly
    LODLayer* mainLayer = addLayer("main");
    mainLayer->nodeMask = NodeMask(nodeCount, 1.0f);
    setLayerStagger("main", 0.0f, 1.0f);
}
