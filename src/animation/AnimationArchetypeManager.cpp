#include "AnimationArchetypeManager.h"
#include "AnimatedCharacter.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// =============================================================================
// Animation Sampling Functions
// =============================================================================

// Helper: Sample animation channels into local transforms
// This is similar to AnimationClip::sample but works with a vector of transforms
// instead of modifying the skeleton directly
static void sampleClipToLocalTransforms(
    const AnimationClip& clip,
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& bindPoseLocalTransforms,
    float time,
    std::vector<glm::mat4>& outLocalTransforms)
{
    // Start with bind pose
    outLocalTransforms = bindPoseLocalTransforms;

    for (const auto& channel : clip.channels) {
        if (channel.jointIndex < 0 ||
            channel.jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
            continue;
        }

        const Joint& joint = skeleton.joints[channel.jointIndex];
        glm::mat4& localTransform = outLocalTransforms[channel.jointIndex];

        // Decompose bind pose transform to get default values
        glm::vec3 translation = glm::vec3(localTransform[3]);

        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(localTransform[0]));
        scale.y = glm::length(glm::vec3(localTransform[1]));
        scale.z = glm::length(glm::vec3(localTransform[2]));

        glm::mat3 rotMat(
            glm::vec3(localTransform[0]) / scale.x,
            glm::vec3(localTransform[1]) / scale.y,
            glm::vec3(localTransform[2]) / scale.z
        );
        glm::quat rotation = glm::quat_cast(rotMat);

        // Override with animated values where available
        if (channel.hasTranslation()) {
            translation = channel.translation.sample(time);
        }
        if (channel.hasRotation()) {
            rotation = channel.rotation.sample(time);
        }
        if (channel.hasScale()) {
            scale = channel.scale.sample(time);
        }

        // Strip root motion: zero out horizontal translation for root bone
        if (channel.jointIndex == clip.rootBoneIndex) {
            translation.x = 0.0f;
            translation.z = 0.0f;
        }

        // Build local transform matrix: T * Rpre * R * S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 Rpre = glm::mat4_cast(joint.preRotation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

        localTransform = T * Rpre * R * S;
    }
}

// Helper: Compute global transforms from local transforms
static void computeGlobalTransforms(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& localTransforms,
    std::vector<glm::mat4>& outGlobalTransforms)
{
    size_t numJoints = skeleton.joints.size();
    outGlobalTransforms.resize(numJoints);

    for (size_t i = 0; i < numJoints; ++i) {
        int32_t parentIdx = skeleton.joints[i].parentIndex;
        if (parentIdx < 0 || parentIdx >= static_cast<int32_t>(numJoints)) {
            // Root joint
            outGlobalTransforms[i] = localTransforms[i];
        } else {
            // Child joint: parent's global * local
            outGlobalTransforms[i] = outGlobalTransforms[parentIdx] * localTransforms[i];
        }
    }
}

// Helper: Compute bone matrices from global transforms
static void computeBoneMatricesFromGlobal(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& globalTransforms,
    const BoneLODMask* lodMask,
    std::vector<glm::mat4>& outBoneMatrices)
{
    size_t numJoints = skeleton.joints.size();
    outBoneMatrices.resize(numJoints);

    // First pass: compute active bones
    for (size_t i = 0; i < numJoints; ++i) {
        bool isActive = !lodMask || i >= MAX_LOD_BONES ||
                        lodMask->isBoneActive(static_cast<uint32_t>(i));
        if (isActive) {
            outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
        }
    }

    // Second pass: inactive bones copy parent's matrix
    if (lodMask) {
        for (size_t i = 0; i < numJoints; ++i) {
            if (i < MAX_LOD_BONES && !lodMask->isBoneActive(static_cast<uint32_t>(i))) {
                int32_t parentIdx = skeleton.joints[i].parentIndex;
                if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(numJoints)) {
                    outBoneMatrices[i] = outBoneMatrices[parentIdx];
                } else {
                    outBoneMatrices[i] = glm::mat4(1.0f);
                }
            }
        }
    }
}

void sampleArchetypeAnimation(
    const AnimationArchetype& archetype,
    size_t clipIndex,
    float time,
    std::vector<glm::mat4>& outBoneMatrices,
    uint32_t lodLevel)
{
    if (clipIndex >= archetype.animations.size()) {
        return;
    }

    const AnimationClip& clip = archetype.animations[clipIndex];

    // Wrap time for looping
    if (clip.duration > 0.0f) {
        time = std::fmod(time, clip.duration);
        if (time < 0.0f) time += clip.duration;
    }

    // Sample animation into local transforms
    std::vector<glm::mat4> localTransforms;
    sampleClipToLocalTransforms(clip, archetype.skeleton,
                                 archetype.bindPoseLocalTransforms, time, localTransforms);

    // Compute global transforms
    std::vector<glm::mat4> globalTransforms;
    computeGlobalTransforms(archetype.skeleton, localTransforms, globalTransforms);

    // Compute bone matrices with LOD
    const BoneLODMask* lodMask = (lodLevel > 0 && lodLevel < CHARACTER_LOD_LEVELS)
                                   ? &archetype.boneLODMasks[lodLevel]
                                   : nullptr;
    computeBoneMatricesFromGlobal(archetype.skeleton, globalTransforms, lodMask, outBoneMatrices);
}

void sampleArchetypeAnimationBlended(
    const AnimationArchetype& archetype,
    size_t clipIndexA,
    float timeA,
    size_t clipIndexB,
    float timeB,
    float blendFactor,
    std::vector<glm::mat4>& outBoneMatrices,
    uint32_t lodLevel)
{
    // Handle edge cases
    if (blendFactor <= 0.0f || clipIndexA >= archetype.animations.size()) {
        sampleArchetypeAnimation(archetype, clipIndexA, timeA, outBoneMatrices, lodLevel);
        return;
    }
    if (blendFactor >= 1.0f || clipIndexB >= archetype.animations.size()) {
        sampleArchetypeAnimation(archetype, clipIndexB, timeB, outBoneMatrices, lodLevel);
        return;
    }

    const AnimationClip& clipA = archetype.animations[clipIndexA];
    const AnimationClip& clipB = archetype.animations[clipIndexB];

    // Wrap times
    float wrappedTimeA = clipA.duration > 0.0f ? std::fmod(timeA, clipA.duration) : 0.0f;
    float wrappedTimeB = clipB.duration > 0.0f ? std::fmod(timeB, clipB.duration) : 0.0f;
    if (wrappedTimeA < 0.0f) wrappedTimeA += clipA.duration;
    if (wrappedTimeB < 0.0f) wrappedTimeB += clipB.duration;

    // Sample both clips
    std::vector<glm::mat4> localTransformsA, localTransformsB;
    sampleClipToLocalTransforms(clipA, archetype.skeleton,
                                 archetype.bindPoseLocalTransforms, wrappedTimeA, localTransformsA);
    sampleClipToLocalTransforms(clipB, archetype.skeleton,
                                 archetype.bindPoseLocalTransforms, wrappedTimeB, localTransformsB);

    // Blend local transforms
    size_t numJoints = archetype.skeleton.joints.size();
    std::vector<glm::mat4> blendedLocalTransforms(numJoints);

    for (size_t i = 0; i < numJoints; ++i) {
        // Decompose both transforms
        glm::vec3 transA = glm::vec3(localTransformsA[i][3]);
        glm::vec3 transB = glm::vec3(localTransformsB[i][3]);

        glm::vec3 scaleA(
            glm::length(glm::vec3(localTransformsA[i][0])),
            glm::length(glm::vec3(localTransformsA[i][1])),
            glm::length(glm::vec3(localTransformsA[i][2]))
        );
        glm::vec3 scaleB(
            glm::length(glm::vec3(localTransformsB[i][0])),
            glm::length(glm::vec3(localTransformsB[i][1])),
            glm::length(glm::vec3(localTransformsB[i][2]))
        );

        glm::mat3 rotMatA(
            glm::vec3(localTransformsA[i][0]) / scaleA.x,
            glm::vec3(localTransformsA[i][1]) / scaleA.y,
            glm::vec3(localTransformsA[i][2]) / scaleA.z
        );
        glm::mat3 rotMatB(
            glm::vec3(localTransformsB[i][0]) / scaleB.x,
            glm::vec3(localTransformsB[i][1]) / scaleB.y,
            glm::vec3(localTransformsB[i][2]) / scaleB.z
        );
        glm::quat rotA = glm::quat_cast(rotMatA);
        glm::quat rotB = glm::quat_cast(rotMatB);

        // Blend
        glm::vec3 blendedTrans = glm::mix(transA, transB, blendFactor);
        glm::vec3 blendedScale = glm::mix(scaleA, scaleB, blendFactor);
        glm::quat blendedRot = glm::slerp(rotA, rotB, blendFactor);

        // Recompose
        glm::mat4 T = glm::translate(glm::mat4(1.0f), blendedTrans);
        glm::mat4 R = glm::mat4_cast(blendedRot);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), blendedScale);
        blendedLocalTransforms[i] = T * R * S;
    }

    // Compute global transforms
    std::vector<glm::mat4> globalTransforms;
    computeGlobalTransforms(archetype.skeleton, blendedLocalTransforms, globalTransforms);

    // Compute bone matrices with LOD
    const BoneLODMask* lodMask = (lodLevel > 0 && lodLevel < CHARACTER_LOD_LEVELS)
                                   ? &archetype.boneLODMasks[lodLevel]
                                   : nullptr;
    computeBoneMatricesFromGlobal(archetype.skeleton, globalTransforms, lodMask, outBoneMatrices);
}

float advanceAnimationTime(
    const AnimationClip& clip,
    float currentTime,
    float deltaTime,
    float playbackSpeed,
    bool looping)
{
    float newTime = currentTime + deltaTime * playbackSpeed;

    if (looping) {
        if (clip.duration > 0.0f) {
            newTime = std::fmod(newTime, clip.duration);
            if (newTime < 0.0f) newTime += clip.duration;
        }
    } else {
        if (newTime > clip.duration) {
            newTime = clip.duration;
        } else if (newTime < 0.0f) {
            newTime = 0.0f;
        }
    }

    return newTime;
}

// =============================================================================
// AnimationArchetypeManager Implementation
// =============================================================================

uint32_t AnimationArchetypeManager::createFromCharacter(
    const std::string& name,
    const AnimatedCharacter& character)
{
    auto archetype = std::make_unique<AnimationArchetype>();
    archetype->name = name;
    archetype->id = nextId_;

    // Copy skeleton
    archetype->skeleton = character.getSkeleton();

    // Copy bind pose transforms
    const auto& animations = character.getAnimations();

    // Get bind pose from skeleton joints
    archetype->bindPoseLocalTransforms.resize(archetype->skeleton.joints.size());
    for (size_t i = 0; i < archetype->skeleton.joints.size(); ++i) {
        archetype->bindPoseLocalTransforms[i] = archetype->skeleton.joints[i].localTransform;
    }

    // Copy animations
    archetype->animations = animations;

    // Copy bone LOD data
    for (uint32_t lod = 0; lod < CHARACTER_LOD_LEVELS; ++lod) {
        archetype->boneLODMasks[lod] = character.getBoneLODMask(lod);
    }
    archetype->boneCategories = character.getBoneCategories();

    // Build animation lookup
    archetype->buildAnimationLookup();

    uint32_t id = archetype->id;
    nameToId_[name] = id;
    archetypes_.push_back(std::move(archetype));
    nextId_++;

    SDL_Log("AnimationArchetypeManager: Created archetype '%s' (id=%u) with %zu bones, %zu animations",
            name.c_str(), id,
            archetype->skeleton.joints.size(),
            archetype->animations.size());

    return id;
}

uint32_t AnimationArchetypeManager::createArchetype(AnimationArchetype archetype) {
    archetype.id = nextId_;

    // Build animation lookup if not already built
    if (archetype.animationNameToIndex.empty()) {
        archetype.buildAnimationLookup();
    }

    uint32_t id = archetype.id;
    nameToId_[archetype.name] = id;

    auto ptr = std::make_unique<AnimationArchetype>(std::move(archetype));
    archetypes_.push_back(std::move(ptr));
    nextId_++;

    return id;
}

const AnimationArchetype* AnimationArchetypeManager::getArchetype(uint32_t id) const {
    for (const auto& arch : archetypes_) {
        if (arch->id == id) {
            return arch.get();
        }
    }
    return nullptr;
}

AnimationArchetype* AnimationArchetypeManager::getArchetype(uint32_t id) {
    for (auto& arch : archetypes_) {
        if (arch->id == id) {
            return arch.get();
        }
    }
    return nullptr;
}

const AnimationArchetype* AnimationArchetypeManager::findArchetype(const std::string& name) const {
    auto it = nameToId_.find(name);
    if (it != nameToId_.end()) {
        return getArchetype(it->second);
    }
    return nullptr;
}

uint32_t AnimationArchetypeManager::findArchetypeId(const std::string& name) const {
    auto it = nameToId_.find(name);
    return (it != nameToId_.end()) ? it->second : INVALID_ARCHETYPE_ID;
}

std::vector<uint32_t> AnimationArchetypeManager::getAllArchetypeIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(archetypes_.size());
    for (const auto& arch : archetypes_) {
        ids.push_back(arch->id);
    }
    return ids;
}

size_t AnimationArchetypeManager::getTotalBoneCount() const {
    size_t total = 0;
    for (const auto& arch : archetypes_) {
        total += arch->skeleton.joints.size();
    }
    return total;
}

size_t AnimationArchetypeManager::getTotalAnimationCount() const {
    size_t total = 0;
    for (const auto& arch : archetypes_) {
        total += arch->animations.size();
    }
    return total;
}

void AnimationArchetypeManager::clear() {
    archetypes_.clear();
    nameToId_.clear();
    nextId_ = 0;
}

// =============================================================================
// NPCAnimationInstance Update
// =============================================================================

void updateAnimationInstance(
    NPCAnimationInstance& instance,
    const AnimationArchetype& archetype,
    float deltaTime,
    uint32_t currentFrame)
{
    // Ensure bone matrix buffer is sized correctly
    instance.resizeBoneMatrices(archetype.getBoneCount());

    // Advance animation time
    const AnimationClip* currentClip = archetype.getAnimation(instance.currentClipIndex);
    if (!currentClip) {
        return;
    }

    instance.currentTime = advanceAnimationTime(
        *currentClip,
        instance.currentTime,
        deltaTime,
        instance.playbackSpeed,
        instance.looping
    );

    // Update blend state
    instance.updateBlend(deltaTime);

    // Advance previous clip time if blending
    if (instance.isBlending) {
        const AnimationClip* prevClip = archetype.getAnimation(instance.previousClipIndex);
        if (prevClip) {
            instance.previousTime = advanceAnimationTime(
                *prevClip,
                instance.previousTime,
                deltaTime,
                instance.playbackSpeed,
                true  // Always loop during blend-out
            );
        }
    }

    // Sample animation(s) and compute bone matrices
    if (instance.isBlending) {
        sampleArchetypeAnimationBlended(
            archetype,
            instance.previousClipIndex,
            instance.previousTime,
            instance.currentClipIndex,
            instance.currentTime,
            instance.blendWeight,
            instance.boneMatrices,
            instance.lodLevel
        );
    } else {
        sampleArchetypeAnimation(
            archetype,
            instance.currentClipIndex,
            instance.currentTime,
            instance.boneMatrices,
            instance.lodLevel
        );
    }

    instance.lastUpdateFrame = currentFrame;
}
