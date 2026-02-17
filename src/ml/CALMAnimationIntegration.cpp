#include "CALMAnimationIntegration.h"
#include "GLTFLoader.h"
#include "CharacterController.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>

namespace ml {

// --- CALMArchetypeManager ---

uint32_t CALMArchetypeManager::createArchetype(const std::string& name,
                                                uint32_t animArchetypeId,
                                                CALMLowLevelController llc,
                                                CALMLatentSpace latentSpace,
                                                CALMCharacterConfig config) {
    auto archetype = std::make_unique<CALMArchetype>();
    archetype->id = nextArchetypeId_;
    archetype->name = name;
    archetype->animArchetypeId = animArchetypeId;
    archetype->llc = std::move(llc);
    archetype->latentSpace = std::move(latentSpace);
    archetype->config = config;

    archetypeNameMap_[name] = nextArchetypeId_;
    archetypes_.push_back(std::move(archetype));

    SDL_Log("CALMArchetypeManager: created archetype '%s' (id=%u, actionDim=%d, obsDim=%d)",
            name.c_str(), nextArchetypeId_, config.actionDim, config.observationDim);

    return nextArchetypeId_++;
}

const CALMArchetype* CALMArchetypeManager::getArchetype(uint32_t id) const {
    for (const auto& a : archetypes_) {
        if (a->id == id) return a.get();
    }
    return nullptr;
}

const CALMArchetype* CALMArchetypeManager::findArchetype(const std::string& name) const {
    auto it = archetypeNameMap_.find(name);
    if (it == archetypeNameMap_.end()) return nullptr;
    return getArchetype(it->second);
}

// --- Instance management ---

size_t CALMArchetypeManager::createInstance(uint32_t archetypeId) {
    CALMNPCInstance instance;
    instance.archetypeId = archetypeId;
    size_t idx = instances_.size();
    instances_.push_back(std::move(instance));
    return idx;
}

void CALMArchetypeManager::initInstance(size_t instanceIdx, Skeleton& skeleton) {
    if (instanceIdx >= instances_.size()) return;

    auto& instance = instances_[instanceIdx];
    const CALMArchetype* archetype = getArchetype(instance.archetypeId);
    if (!archetype) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CALMArchetypeManager: invalid archetype %u for instance %zu",
                     instance.archetypeId, instanceIdx);
        return;
    }

    // Initialize the per-NPC controller with shared archetype data.
    // The LLC and latent space are copied (controllers are lightweight),
    // but their weight data (tensors) use copy-on-write semantics.
    instance.controller.init(archetype->config,
                             archetype->llc,
                             archetype->latentSpace);

    // Pre-allocate cached pose and bone matrices
    size_t boneCount = skeleton.joints.size();
    instance.cachedPose.resize(boneCount);
    instance.cachedBoneMatrices.resize(boneCount, glm::mat4(1.0f));
    instance.initialized = true;
}

CALMNPCInstance* CALMArchetypeManager::getInstance(size_t index) {
    return index < instances_.size() ? &instances_[index] : nullptr;
}

const CALMNPCInstance* CALMArchetypeManager::getInstance(size_t index) const {
    return index < instances_.size() ? &instances_[index] : nullptr;
}

// --- Per-frame update ---

void CALMArchetypeManager::updateAll(float deltaTime,
                                      std::vector<Skeleton>& skeletons,
                                      const std::vector<CharacterController>& physics,
                                      uint32_t currentFrame,
                                      const CharacterLODConfig& lodConfig) {
    assert(skeletons.size() == instances_.size());
    assert(physics.size() == instances_.size());

    for (size_t i = 0; i < instances_.size(); ++i) {
        auto& inst = instances_[i];
        if (!inst.initialized) continue;

        if (shouldUpdateInstance(i, currentFrame, lodConfig)) {
            updateInstance(i, deltaTime, skeletons[i], physics[i]);
            computeBoneMatrices(i, skeletons[i]);
            inst.lastUpdateFrame = currentFrame;
            inst.framesSinceUpdate = 0;
        } else {
            ++inst.framesSinceUpdate;
        }
    }
}

void CALMArchetypeManager::updateInstance(size_t instanceIdx,
                                           float deltaTime,
                                           Skeleton& skeleton,
                                           const CharacterController& physics) {
    if (instanceIdx >= instances_.size()) return;

    auto& inst = instances_[instanceIdx];
    if (!inst.initialized) return;

    inst.controller.update(deltaTime, skeleton, physics, inst.cachedPose);
}

// --- LOD control ---

void CALMArchetypeManager::setInstanceLOD(size_t instanceIdx, uint32_t lodLevel) {
    if (instanceIdx < instances_.size()) {
        instances_[instanceIdx].lodLevel = lodLevel;
    }
}

bool CALMArchetypeManager::shouldUpdateInstance(size_t instanceIdx, uint32_t currentFrame,
                                                 const CharacterLODConfig& lodConfig) const {
    if (instanceIdx >= instances_.size()) return false;

    const auto& inst = instances_[instanceIdx];
    uint32_t lod = inst.lodLevel;
    if (lod >= CHARACTER_LOD_LEVELS) lod = CHARACTER_LOD_LEVELS - 1;

    uint32_t interval = lodConfig.animationUpdateInterval[lod];
    if (interval <= 1) return true;

    return inst.framesSinceUpdate >= interval;
}

// --- Bone matrix computation ---

void CALMArchetypeManager::computeBoneMatrices(size_t instanceIdx,
                                                const Skeleton& skeleton) {
    if (instanceIdx >= instances_.size()) return;

    auto& inst = instances_[instanceIdx];
    computeBoneMatricesFromPose(inst.cachedPose, skeleton, inst.cachedBoneMatrices);
}

const std::vector<glm::mat4>& CALMArchetypeManager::getBoneMatrices(size_t instanceIdx) const {
    static const std::vector<glm::mat4> empty;
    if (instanceIdx >= instances_.size()) return empty;
    return instances_[instanceIdx].cachedBoneMatrices;
}

void CALMArchetypeManager::clearInstances() {
    instances_.clear();
}

// --- Utility ---

void computeBoneMatricesFromPose(const SkeletonPose& pose,
                                  const Skeleton& skeleton,
                                  std::vector<glm::mat4>& outMatrices) {
    size_t numJoints = skeleton.joints.size();
    outMatrices.resize(numJoints, glm::mat4(1.0f));

    if (pose.size() != numJoints) return;

    // Compute world-space transforms by traversing the hierarchy
    std::vector<glm::mat4> worldTransforms(numJoints, glm::mat4(1.0f));

    for (size_t i = 0; i < numJoints; ++i) {
        // Build local transform from BonePose
        const BonePose& bp = pose[i];
        glm::mat4 local = glm::translate(glm::mat4(1.0f), bp.translation)
                         * glm::mat4_cast(bp.rotation)
                         * glm::scale(glm::mat4(1.0f), bp.scale);

        int32_t parentIdx = skeleton.joints[i].parentIndex;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < numJoints) {
            worldTransforms[i] = worldTransforms[parentIdx] * local;
        } else {
            worldTransforms[i] = local;
        }

        // Final bone matrix = world transform * inverse bind matrix
        outMatrices[i] = worldTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

} // namespace ml
