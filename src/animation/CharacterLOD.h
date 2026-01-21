#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <bitset>
#include <cstdint>
#include <string>

#include "SkinnedMesh.h"

// Number of LOD levels for skinned characters
constexpr uint32_t CHARACTER_LOD_LEVELS = 4;

// Maximum bones supported for LOD mask
constexpr uint32_t MAX_LOD_BONES = 128;

// Bone categories for LOD culling
enum class BoneCategory : uint8_t {
    Core,        // Always active: hips, spine, head (LOD 0-3)
    Limb,        // Arms, legs (LOD 0-3)
    Extremity,   // Hands, feet (LOD 0-2)
    Finger,      // Individual fingers (LOD 0-1)
    Face,        // Facial bones (LOD 0 only)
    Secondary    // Twist bones, helpers (LOD 0 only)
};

// Get minimum LOD level that includes this bone category
inline uint32_t getMinLODForCategory(BoneCategory cat) {
    switch (cat) {
        case BoneCategory::Core:      return 3;  // Active at all LODs
        case BoneCategory::Limb:      return 3;  // Active at all LODs
        case BoneCategory::Extremity: return 2;  // Active at LOD 0-2
        case BoneCategory::Finger:    return 1;  // Active at LOD 0-1
        case BoneCategory::Face:      return 0;  // Active at LOD 0 only
        case BoneCategory::Secondary: return 0;  // Active at LOD 0 only
        default: return 3;
    }
}

// Bone LOD configuration - which bones are active at each LOD level
struct BoneLODMask {
    std::bitset<MAX_LOD_BONES> activeBones;
    uint32_t activeBoneCount = 0;

    bool isBoneActive(uint32_t boneIndex) const {
        return boneIndex < MAX_LOD_BONES && activeBones.test(boneIndex);
    }

    void setAllActive(uint32_t totalBones) {
        activeBones.reset();
        for (uint32_t i = 0; i < totalBones && i < MAX_LOD_BONES; ++i) {
            activeBones.set(i);
        }
        activeBoneCount = totalBones;
    }
};

// LOD configuration for skinned characters
// Follows AAA game patterns for crowd rendering
struct CharacterLODConfig {
    // Screen-space coverage thresholds for LOD transitions
    // Based on character bounding sphere projected to screen pixels
    // LOD0 when screenSize > thresholds[0], LOD1 when > thresholds[1], etc.
    std::array<float, CHARACTER_LOD_LEVELS - 1> screenSizeThresholds = {
        200.0f,  // LOD0 -> LOD1: character covers 200+ pixels
        100.0f,  // LOD1 -> LOD2: character covers 100+ pixels
        50.0f    // LOD2 -> LOD3: character covers 50+ pixels
    };

    // Distance-based fallback thresholds (used if screen-space not available)
    std::array<float, CHARACTER_LOD_LEVELS - 1> distanceThresholds = {
        10.0f,   // LOD0 -> LOD1: beyond 10m
        25.0f,   // LOD1 -> LOD2: beyond 25m
        50.0f    // LOD2 -> LOD3: beyond 50m
    };

    // Animation update frequency reduction per LOD level
    // 1 = every frame, 2 = every 2 frames, etc.
    std::array<uint32_t, CHARACTER_LOD_LEVELS> animationUpdateInterval = {
        1,   // LOD0: full rate (60Hz at 60fps)
        1,   // LOD1: full rate (still close enough to notice)
        2,   // LOD2: half rate (30Hz at 60fps)
        4    // LOD3: quarter rate (15Hz at 60fps)
    };

    // Hysteresis to prevent LOD popping
    float hysteresisRatio = 0.1f;  // 10% threshold buffer

    // Enable LOD transitions (dithered cross-fade)
    bool enableTransitions = true;
    float transitionDuration = 0.2f;  // Seconds for LOD cross-fade
};

// Per-character LOD state
struct CharacterLODState {
    uint32_t currentLOD = 0;           // Current active LOD level
    uint32_t targetLOD = 0;            // Target LOD (may differ during transition)
    float transitionProgress = 1.0f;   // 0-1, 1 = fully at current LOD
    float lastDistance = 0.0f;         // Distance to camera (for debugging)
    float lastScreenSize = 0.0f;       // Screen size in pixels (for debugging)
    uint32_t framesSinceAnimUpdate = 0; // Frames since last animation update
    bool needsAnimationUpdate = true;   // Whether to update animation this frame
};

// Mesh data for a single LOD level
struct CharacterLODMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t triangleCount = 0;

    // Triangle reduction factor compared to LOD0 (for debugging)
    float reductionFactor = 1.0f;
};

// GPU-uploaded mesh for a single LOD level
struct CharacterLODMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    uint32_t triangleCount = 0;

    bool isValid() const { return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE; }
};

// Screen parameters for LOD calculations
struct CharacterScreenParams {
    float screenHeight = 1080.0f;
    float tanHalfFOV = 1.0f;  // tan(fov/2)

    CharacterScreenParams() = default;
    CharacterScreenParams(float height, float tanFov) : screenHeight(height), tanHalfFOV(tanFov) {}
};

// Calculate screen-space size of a bounding sphere
inline float calculateScreenSize(float boundingSphereRadius, float distance,
                                  const CharacterScreenParams& screen) {
    if (distance <= 0.0f) return screen.screenHeight;  // At camera = max size

    // Project sphere radius to screen pixels
    // screenSize = (radius / distance) * (screenHeight / 2) / tan(fov/2)
    return (boundingSphereRadius / distance) * (screen.screenHeight * 0.5f) / screen.tanHalfFOV;
}

// Calculate LOD level from distance using config thresholds
inline uint32_t calculateLODFromDistance(float distance, const CharacterLODConfig& config,
                                          float hysteresisDirection = 0.0f) {
    float hysteresis = hysteresisDirection * config.hysteresisRatio;

    for (uint32_t lod = 0; lod < CHARACTER_LOD_LEVELS - 1; ++lod) {
        float threshold = config.distanceThresholds[lod] * (1.0f + hysteresis);
        if (distance < threshold) {
            return lod;
        }
    }
    return CHARACTER_LOD_LEVELS - 1;
}

// Calculate LOD level from screen size using config thresholds
inline uint32_t calculateLODFromScreenSize(float screenSize, const CharacterLODConfig& config,
                                            float hysteresisDirection = 0.0f) {
    float hysteresis = hysteresisDirection * config.hysteresisRatio;

    for (uint32_t lod = 0; lod < CHARACTER_LOD_LEVELS - 1; ++lod) {
        float threshold = config.screenSizeThresholds[lod] * (1.0f - hysteresis);
        if (screenSize >= threshold) {
            return lod;
        }
    }
    return CHARACTER_LOD_LEVELS - 1;
}

// Categorize a bone by its name (common humanoid naming conventions)
inline BoneCategory categorizeBone(const std::string& boneName) {
    // Convert to lowercase for matching
    std::string lower = boneName;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Finger bones (most specific, check first)
    if (lower.find("thumb") != std::string::npos ||
        lower.find("index") != std::string::npos ||
        lower.find("middle") != std::string::npos ||
        lower.find("ring") != std::string::npos ||
        lower.find("pinky") != std::string::npos ||
        lower.find("finger") != std::string::npos) {
        return BoneCategory::Finger;
    }

    // Toe bones
    if (lower.find("toe") != std::string::npos) {
        return BoneCategory::Finger;  // Same LOD as fingers
    }

    // Face/head detail bones
    if (lower.find("eye") != std::string::npos ||
        lower.find("jaw") != std::string::npos ||
        lower.find("brow") != std::string::npos ||
        lower.find("lip") != std::string::npos ||
        lower.find("tongue") != std::string::npos ||
        lower.find("teeth") != std::string::npos ||
        lower.find("ear") != std::string::npos ||
        lower.find("nose") != std::string::npos ||
        lower.find("cheek") != std::string::npos) {
        return BoneCategory::Face;
    }

    // Secondary/twist bones
    if (lower.find("twist") != std::string::npos ||
        lower.find("roll") != std::string::npos ||
        lower.find("helper") != std::string::npos ||
        lower.find("auxiliary") != std::string::npos) {
        return BoneCategory::Secondary;
    }

    // Extremities (hands, feet)
    if (lower.find("hand") != std::string::npos ||
        lower.find("foot") != std::string::npos ||
        lower.find("wrist") != std::string::npos ||
        lower.find("ankle") != std::string::npos) {
        return BoneCategory::Extremity;
    }

    // Core bones
    if (lower.find("hip") != std::string::npos ||
        lower.find("pelvis") != std::string::npos ||
        lower.find("spine") != std::string::npos ||
        lower.find("chest") != std::string::npos ||
        lower.find("neck") != std::string::npos ||
        lower.find("head") != std::string::npos ||
        lower.find("root") != std::string::npos) {
        return BoneCategory::Core;
    }

    // Limbs (arms, legs)
    if (lower.find("shoulder") != std::string::npos ||
        lower.find("arm") != std::string::npos ||
        lower.find("elbow") != std::string::npos ||
        lower.find("forearm") != std::string::npos ||
        lower.find("clavicle") != std::string::npos ||
        lower.find("leg") != std::string::npos ||
        lower.find("thigh") != std::string::npos ||
        lower.find("knee") != std::string::npos ||
        lower.find("shin") != std::string::npos ||
        lower.find("calf") != std::string::npos ||
        lower.find("upleg") != std::string::npos) {
        return BoneCategory::Limb;
    }

    // Default to limb (safer to keep visible)
    return BoneCategory::Limb;
}
