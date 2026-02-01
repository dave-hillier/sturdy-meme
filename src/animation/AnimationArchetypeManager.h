#pragma once

#include "Animation.h"
#include "CharacterLOD.h"
#include "loaders/GLTFLoader.h"
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class AnimatedCharacter;
struct SkinnedMesh;
struct Mesh;

// =============================================================================
// AnimationArchetype - Shared animation data for a character type
// =============================================================================
// Contains skeleton, animation clips, and LOD configuration shared by all
// NPCs of the same type. Reduces memory from O(n NPCs) to O(character types).

struct AnimationArchetype {
    // Identification
    std::string name;
    uint32_t id = 0;

    // Shared skeleton (bone hierarchy, inverse bind matrices)
    Skeleton skeleton;
    std::vector<glm::mat4> bindPoseLocalTransforms;

    // Shared animation clips
    std::vector<AnimationClip> animations;

    // Bone LOD configuration
    std::array<BoneLODMask, CHARACTER_LOD_LEVELS> boneLODMasks;
    std::vector<BoneCategory> boneCategories;

    // Animation lookup by name (populated on load)
    std::unordered_map<std::string, size_t> animationNameToIndex;

    // Check validity
    [[nodiscard]] bool isValid() const {
        return !skeleton.joints.empty() && !animations.empty();
    }

    // Get bone count
    [[nodiscard]] uint32_t getBoneCount() const {
        return static_cast<uint32_t>(skeleton.joints.size());
    }

    // Find animation by name
    [[nodiscard]] const AnimationClip* findAnimation(const std::string& animName) const {
        auto it = animationNameToIndex.find(animName);
        if (it != animationNameToIndex.end() && it->second < animations.size()) {
            return &animations[it->second];
        }
        return nullptr;
    }

    // Get animation by index
    [[nodiscard]] const AnimationClip* getAnimation(size_t index) const {
        return index < animations.size() ? &animations[index] : nullptr;
    }

    // Find animation index by name
    [[nodiscard]] size_t findAnimationIndex(const std::string& animName) const {
        auto it = animationNameToIndex.find(animName);
        return (it != animationNameToIndex.end()) ? it->second : SIZE_MAX;
    }

    // Get bone LOD mask for a given LOD level
    [[nodiscard]] const BoneLODMask& getBoneLODMask(uint32_t lodLevel) const {
        if (lodLevel >= CHARACTER_LOD_LEVELS) lodLevel = CHARACTER_LOD_LEVELS - 1;
        return boneLODMasks[lodLevel];
    }

    // Build name-to-index lookup
    void buildAnimationLookup() {
        animationNameToIndex.clear();
        for (size_t i = 0; i < animations.size(); ++i) {
            animationNameToIndex[animations[i].name] = i;
        }
    }
};

// =============================================================================
// Animation Sampling Functions
// =============================================================================
// Standalone functions to sample animation from an archetype without
// owning an AnimatedCharacter instance.

// Sample animation at a given time and compute bone matrices
// archetype: shared animation data
// clipIndex: which animation clip to sample
// time: current playback time (will be wrapped for looping)
// outBoneMatrices: output buffer for computed bone matrices (must be pre-sized)
// lodLevel: LOD level for bone skipping (0 = full detail)
void sampleArchetypeAnimation(
    const AnimationArchetype& archetype,
    size_t clipIndex,
    float time,
    std::vector<glm::mat4>& outBoneMatrices,
    uint32_t lodLevel = 0);

// Sample with blending between two clips (for transitions)
// blendFactor: 0.0 = clipA, 1.0 = clipB
void sampleArchetypeAnimationBlended(
    const AnimationArchetype& archetype,
    size_t clipIndexA,
    float timeA,
    size_t clipIndexB,
    float timeB,
    float blendFactor,
    std::vector<glm::mat4>& outBoneMatrices,
    uint32_t lodLevel = 0);

// Advance animation time with looping
// Returns the new time value
float advanceAnimationTime(
    const AnimationClip& clip,
    float currentTime,
    float deltaTime,
    float playbackSpeed = 1.0f,
    bool looping = true);

// =============================================================================
// AnimationArchetypeManager - Manages shared animation archetypes
// =============================================================================
// Central manager for all animation archetypes. NPCs reference archetypes
// by ID instead of owning AnimatedCharacter instances.

class AnimationArchetypeManager {
public:
    AnimationArchetypeManager() = default;
    ~AnimationArchetypeManager() = default;

    // Non-copyable
    AnimationArchetypeManager(const AnimationArchetypeManager&) = delete;
    AnimationArchetypeManager& operator=(const AnimationArchetypeManager&) = delete;

    // Create archetype from an existing AnimatedCharacter
    // Extracts skeleton, animations, and LOD data
    // The AnimatedCharacter can be destroyed after this call
    uint32_t createFromCharacter(const std::string& name, const AnimatedCharacter& character);

    // Create archetype directly from data
    uint32_t createArchetype(AnimationArchetype archetype);

    // Get archetype by ID
    [[nodiscard]] const AnimationArchetype* getArchetype(uint32_t id) const;
    [[nodiscard]] AnimationArchetype* getArchetype(uint32_t id);

    // Get archetype by name
    [[nodiscard]] const AnimationArchetype* findArchetype(const std::string& name) const;
    [[nodiscard]] uint32_t findArchetypeId(const std::string& name) const;

    // Get all archetype IDs
    [[nodiscard]] std::vector<uint32_t> getAllArchetypeIds() const;

    // Statistics
    [[nodiscard]] size_t getArchetypeCount() const { return archetypes_.size(); }
    [[nodiscard]] size_t getTotalBoneCount() const;
    [[nodiscard]] size_t getTotalAnimationCount() const;

    // Clear all archetypes
    void clear();

    // Invalid archetype ID constant
    static constexpr uint32_t INVALID_ARCHETYPE_ID = UINT32_MAX;

private:
    std::vector<std::unique_ptr<AnimationArchetype>> archetypes_;
    std::unordered_map<std::string, uint32_t> nameToId_;
    uint32_t nextId_ = 0;
};

// =============================================================================
// Per-NPC Animation Instance State
// =============================================================================
// Minimal state needed per-NPC when using shared archetypes.
// This replaces the full AnimatedCharacter instance.

struct NPCAnimationInstance {
    // Reference to shared archetype
    uint32_t archetypeId = AnimationArchetypeManager::INVALID_ARCHETYPE_ID;

    // Current animation state
    size_t currentClipIndex = 0;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool looping = true;

    // Transition/blend state (for smooth clip changes)
    size_t previousClipIndex = 0;
    float previousTime = 0.0f;
    float blendWeight = 1.0f;  // 1.0 = fully on current clip
    float blendDuration = 0.0f;
    float blendElapsed = 0.0f;
    bool isBlending = false;

    // LOD level (affects bone update frequency and detail)
    uint32_t lodLevel = 0;

    // Cached bone matrices (computed during update)
    std::vector<glm::mat4> boneMatrices;
    uint32_t lastUpdateFrame = 0;

    // Check if archetype is set
    [[nodiscard]] bool hasArchetype() const {
        return archetypeId != AnimationArchetypeManager::INVALID_ARCHETYPE_ID;
    }

    // Start blending to a new animation
    void startBlend(size_t newClipIndex, float duration) {
        if (newClipIndex == currentClipIndex && !isBlending) return;

        previousClipIndex = currentClipIndex;
        previousTime = currentTime;
        currentClipIndex = newClipIndex;
        currentTime = 0.0f;
        blendWeight = 0.0f;
        blendDuration = duration;
        blendElapsed = 0.0f;
        isBlending = true;
    }

    // Update blend state
    void updateBlend(float deltaTime) {
        if (!isBlending) return;

        blendElapsed += deltaTime;
        if (blendElapsed >= blendDuration) {
            blendWeight = 1.0f;
            isBlending = false;
        } else {
            blendWeight = blendElapsed / blendDuration;
        }
    }

    // Resize bone matrix buffer to match archetype
    void resizeBoneMatrices(uint32_t boneCount) {
        if (boneMatrices.size() != boneCount) {
            boneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
    }
};

// Update animation instance with time advancement and bone matrix computation
void updateAnimationInstance(
    NPCAnimationInstance& instance,
    const AnimationArchetype& archetype,
    float deltaTime,
    uint32_t currentFrame);
