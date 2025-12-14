#include "AnimationLayerController.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

void AnimationLayerController::initialize(const Skeleton& skeleton) {
    bindPose.resize(skeleton.joints.size());
    bindPosePreRotations.resize(skeleton.joints.size());

    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        bindPose[i] = BonePose::fromMatrix(skeleton.joints[i].localTransform,
                                           skeleton.joints[i].preRotation);
        bindPosePreRotations[i] = skeleton.joints[i].preRotation;
    }

    initialized = true;
}

AnimationLayer* AnimationLayerController::addLayer(const std::string& name) {
    // Check if layer already exists
    if (layerNameToIndex.find(name) != layerNameToIndex.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "AnimationLayerController: Layer '%s' already exists", name.c_str());
        return layers[layerNameToIndex[name]].get();
    }

    auto layer = std::make_unique<AnimationLayer>(name);

    // Initialize bone mask to skeleton size if we're initialized
    if (initialized) {
        layer->getMask().resize(bindPose.size(), 1.0f);
    }

    layers.push_back(std::move(layer));
    layerNameToIndex[name] = layers.size() - 1;

    return layers.back().get();
}

void AnimationLayerController::removeLayer(const std::string& name) {
    int idx = findLayerIndex(name);
    if (idx < 0) {
        return;
    }

    layers.erase(layers.begin() + idx);

    // Rebuild name-to-index map
    layerNameToIndex.clear();
    for (size_t i = 0; i < layers.size(); ++i) {
        layerNameToIndex[layers[i]->getName()] = i;
    }
}

AnimationLayer* AnimationLayerController::getLayer(const std::string& name) {
    auto it = layerNameToIndex.find(name);
    if (it != layerNameToIndex.end()) {
        return layers[it->second].get();
    }
    return nullptr;
}

const AnimationLayer* AnimationLayerController::getLayer(const std::string& name) const {
    auto it = layerNameToIndex.find(name);
    if (it != layerNameToIndex.end()) {
        return layers[it->second].get();
    }
    return nullptr;
}

AnimationLayer* AnimationLayerController::getLayer(size_t index) {
    if (index < layers.size()) {
        return layers[index].get();
    }
    return nullptr;
}

const AnimationLayer* AnimationLayerController::getLayer(size_t index) const {
    if (index < layers.size()) {
        return layers[index].get();
    }
    return nullptr;
}

int AnimationLayerController::findLayerIndex(const std::string& name) const {
    auto it = layerNameToIndex.find(name);
    if (it != layerNameToIndex.end()) {
        return static_cast<int>(it->second);
    }
    return -1;
}

void AnimationLayerController::setLayerOrder(size_t layerIndex, size_t newPosition) {
    if (layerIndex >= layers.size() || newPosition >= layers.size()) {
        return;
    }

    if (layerIndex == newPosition) {
        return;
    }

    auto layer = std::move(layers[layerIndex]);
    layers.erase(layers.begin() + layerIndex);
    layers.insert(layers.begin() + newPosition, std::move(layer));

    // Rebuild name-to-index map
    layerNameToIndex.clear();
    for (size_t i = 0; i < layers.size(); ++i) {
        layerNameToIndex[layers[i]->getName()] = i;
    }
}

void AnimationLayerController::moveLayerUp(const std::string& name) {
    int idx = findLayerIndex(name);
    if (idx > 0) {
        setLayerOrder(static_cast<size_t>(idx), static_cast<size_t>(idx - 1));
    }
}

void AnimationLayerController::moveLayerDown(const std::string& name) {
    int idx = findLayerIndex(name);
    if (idx >= 0 && static_cast<size_t>(idx) < layers.size() - 1) {
        setLayerOrder(static_cast<size_t>(idx), static_cast<size_t>(idx + 1));
    }
}

void AnimationLayerController::update(float deltaTime) {
    for (auto& layer : layers) {
        layer->update(deltaTime);
    }
}

void AnimationLayerController::applyLayer(const AnimationLayer& layer, SkeletonPose& accumPose) const {
    if (!layer.getEnabled() || layer.getWeight() <= 0.0f) {
        return;
    }

    // Sample the layer's pose
    SkeletonPose layerPose;
    // Create a temporary skeleton structure for sampling
    Skeleton tempSkeleton;
    tempSkeleton.joints.resize(bindPose.size());
    for (size_t i = 0; i < bindPose.size(); ++i) {
        tempSkeleton.joints[i].localTransform = bindPose[i].toMatrix(bindPosePreRotations[i]);
        tempSkeleton.joints[i].preRotation = bindPosePreRotations[i];
    }
    layer.samplePose(tempSkeleton, layerPose);

    // Get layer weights
    float globalWeight = layer.getWeight();
    const BoneMask& mask = layer.getMask();

    // Compute effective per-bone weights
    std::vector<float> effectiveWeights(accumPose.size());
    for (size_t i = 0; i < accumPose.size(); ++i) {
        effectiveWeights[i] = globalWeight * mask.getWeight(i);
    }

    // Apply based on blend mode
    if (layer.getBlendMode() == BlendMode::Override) {
        // Override: blend between accumulated pose and layer pose
        AnimationBlend::blendMasked(accumPose, layerPose, effectiveWeights, accumPose);
    } else {
        // Additive: add layer pose delta on top of accumulated pose
        AnimationBlend::additiveMasked(accumPose, layerPose, effectiveWeights, accumPose);
    }
}

void AnimationLayerController::computeFinalPose(SkeletonPose& outPose) const {
    if (!initialized) {
        return;
    }

    // Start with bind pose
    outPose = bindPose;

    // Apply each layer in order
    for (const auto& layer : layers) {
        applyLayer(*layer, outPose);
    }
}

void AnimationLayerController::applyToSkeleton(Skeleton& skeleton) const {
    if (!initialized) {
        return;
    }

    SkeletonPose finalPose;
    computeFinalPose(finalPose);

    // Apply pose to skeleton
    size_t count = std::min(finalPose.size(), skeleton.joints.size());
    for (size_t i = 0; i < count; ++i) {
        // Reconstruct matrix with preRotation: T * Rpre * R * S
        skeleton.joints[i].localTransform = finalPose[i].toMatrix(skeleton.joints[i].preRotation);
    }
}

void AnimationLayerController::setBaseAnimation(const AnimationClip* clip, bool looping) {
    // Ensure we have at least one layer
    if (layers.empty()) {
        addLayer("base");
    }

    layers[0]->setAnimation(clip, looping);
}

std::vector<std::string> AnimationLayerController::getLayerNames() const {
    std::vector<std::string> names;
    names.reserve(layers.size());
    for (const auto& layer : layers) {
        names.push_back(layer->getName());
    }
    return names;
}
