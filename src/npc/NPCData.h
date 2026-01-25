#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// NPC LOD levels inspired by Assassin's Creed crowd systems
// Controls update frequency and animation quality
enum class NPCLODLevel : uint8_t {
    Virtual = 0,  // >50m: No rendering, minimal updates (every 10 seconds)
    Bulk = 1,     // 25-50m: Simplified animation, reduced updates (every 1 second)
    Real = 2      // <25m: Full animation every frame
};

// Animation playback state per-NPC
// Minimal state needed to continue animation from any point
struct AnimationPlaybackState {
    size_t clipIndex = 0;          // Index into template's animation clips
    float currentTime = 0.0f;      // Current playback position in seconds
    float playbackSpeed = 1.0f;    // Speed multiplier
    float blendWeight = 1.0f;      // Blend weight for transitions
    bool looping = true;           // Whether to loop at end
};

// Structure-of-Arrays for NPC data
// Designed for cache-efficient access patterns during LOD/culling
struct NPCData {
    // Identity - which character template to use
    std::vector<uint32_t> templateIndices;

    // Spatial data (hot data for culling/LOD calculations)
    std::vector<glm::vec3> positions;
    std::vector<float> yawDegrees;  // Facing direction

    // LOD state (updated each frame based on camera distance)
    std::vector<NPCLODLevel> lodLevels;
    std::vector<uint32_t> framesSinceUpdate;  // For LOD-based update scheduling

    // Animation state (per-NPC playback, references template clips)
    std::vector<AnimationPlaybackState> animStates;

    // Cached bone matrices (reused when animation update is skipped)
    std::vector<std::vector<glm::mat4>> cachedBoneMatrices;

    // Renderable indices into SceneBuilder's sceneObjects
    std::vector<size_t> renderableIndices;

    // Get number of NPCs
    size_t count() const { return positions.size(); }

    // Reserve capacity for n NPCs
    void reserve(size_t n) {
        templateIndices.reserve(n);
        positions.reserve(n);
        yawDegrees.reserve(n);
        lodLevels.reserve(n);
        framesSinceUpdate.reserve(n);
        animStates.reserve(n);
        cachedBoneMatrices.reserve(n);
        renderableIndices.reserve(n);
    }

    // Add a new NPC with default state
    size_t addNPC(uint32_t templateIndex, const glm::vec3& position, float yaw) {
        size_t index = positions.size();
        templateIndices.push_back(templateIndex);
        positions.push_back(position);
        yawDegrees.push_back(yaw);
        lodLevels.push_back(NPCLODLevel::Real);  // Start at highest quality
        framesSinceUpdate.push_back(0);
        animStates.push_back(AnimationPlaybackState{});
        cachedBoneMatrices.push_back({});
        renderableIndices.push_back(0);  // Set by caller after adding renderable
        return index;
    }

    // Clear all NPC data
    void clear() {
        templateIndices.clear();
        positions.clear();
        yawDegrees.clear();
        lodLevels.clear();
        framesSinceUpdate.clear();
        animStates.clear();
        cachedBoneMatrices.clear();
        renderableIndices.clear();
    }
};
