#pragma once

#include <glm/glm.hpp>
#include <cstdint>

/**
 * Common culling data structures and utilities shared across vegetation culling systems.
 * These structures are designed to match GPU shader layouts (std140/std430).
 */

// Common culling fields that appear in multiple uniform structs
struct CullFrustumData {
    glm::vec4 cameraPosition;       // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];     // 6 frustum planes for culling
};

// Helper to populate frustum data from camera state
inline void populateFrustumData(CullFrustumData& data, const glm::vec3& cameraPos, const glm::vec4* frustumPlanes) {
    data.cameraPosition = glm::vec4(cameraPos, 1.0f);
    for (int i = 0; i < 6; ++i) {
        data.frustumPlanes[i] = frustumPlanes[i];
    }
}

// Helper to extract frustum planes from a view-projection matrix
// Uses Gribb/Hartmann plane extraction method with GLM column-major matrix
inline void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* outPlanes) {
    // GLM is column-major, so transpose to get row access for Gribb/Hartmann extraction
    glm::mat4 m = glm::transpose(viewProj);

    // Left plane: row3 + row0
    outPlanes[0] = m[3] + m[0];
    // Right plane: row3 - row0
    outPlanes[1] = m[3] - m[0];
    // Bottom plane: row3 + row1
    outPlanes[2] = m[3] + m[1];
    // Top plane: row3 - row1
    outPlanes[3] = m[3] - m[1];
    // Near plane: row3 + row2
    outPlanes[4] = m[3] + m[2];
    // Far plane: row3 - row2
    outPlanes[5] = m[3] - m[2];

    // Normalize the planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(outPlanes[i]));
        if (len > 0.0001f) {
            outPlanes[i] /= len;
        }
    }
}

// ============================================================================
// Common LOD Parameters
// ============================================================================
// These constants define consistent LOD behavior across all tree subsystems.
// Screen-space error: HIGH when close (large on screen), LOW when far (small)
// Logic: close (high error) = full detail, far (low error) = impostor/cull

// Screen-space error LOD thresholds (used by TreeLODSystem and ImpostorCullSystem)
namespace TreeLODConstants {
    constexpr float ERROR_THRESHOLD_FULL = 4.0f;        // Above: full geometry (close trees)
    constexpr float ERROR_THRESHOLD_IMPOSTOR = 1.0f;    // Below: full impostor (far trees)
    constexpr float ERROR_THRESHOLD_CULL = 0.25f;       // Below: cull entirely (very far)

    // Distance-based LOD thresholds (used by TreeLeafCulling for leaf instances)
    // These should be consistent with screen-space error when possible
    constexpr float FULL_DETAIL_DISTANCE = 250.0f;      // Full geometry below this
    constexpr float MAX_DRAW_DISTANCE = 500.0f;         // Maximum leaf visibility
    constexpr float LOD_TRANSITION_START = 150.0f;      // Start transitioning LOD
    constexpr float LOD_TRANSITION_END = 250.0f;        // Finish transitioning LOD

    // Hysteresis to prevent LOD flickering at boundaries
    constexpr float HYSTERESIS = 5.0f;
    constexpr float BLEND_RANGE = 10.0f;

    // Impostor sizing margin - adds padding to ensure tree fits in billboard
    // Used during atlas capture and runtime sizing calculations
    constexpr float IMPOSTOR_SIZE_MARGIN = 1.15f;
}

// ============================================================================
// Screen-Space Error Calculation
// ============================================================================
// Computes how many pixels of error a world-space feature would produce at a given distance.
// High error = close/large on screen = needs detail, Low error = far/small = can use LOD
//
// Formula: screenError = worldError * screenHeight / (2 * distance * tan(fov/2))
//
// This same formula is used in:
// - TreeLODSystem::update() for CPU LOD decisions
// - tree_impostor_cull.comp for GPU culling
inline float computeScreenError(float worldError, float distance, float screenHeight, float tanHalfFOV) {
    if (distance <= 0.0f) return 9999.0f;
    return worldError * screenHeight / (2.0f * distance * tanHalfFOV);
}

// Number of leaf types (must match tree_leaf_cull.comp NUM_LEAF_TYPES)
constexpr uint32_t NUM_LEAF_TYPES = 4;

// Leaf type indices (oak=0, ash=1, aspen=2, pine=3)
constexpr uint32_t LEAF_TYPE_OAK = 0;
constexpr uint32_t LEAF_TYPE_ASH = 1;
constexpr uint32_t LEAF_TYPE_ASPEN = 2;
constexpr uint32_t LEAF_TYPE_PINE = 3;
