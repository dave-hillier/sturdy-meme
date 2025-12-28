#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

#include "CullCommon.h"

// Octahedral impostor atlas configuration
// Uses hemi-octahedral mapping for continuous view coverage
// Grid is NxN where each cell is a captured view direction
struct OctahedralAtlasConfig {
    static constexpr int GRID_SIZE = 8;               // 8x8 = 64 views (good balance of quality vs memory)
    static constexpr int CELL_SIZE = 256;             // Pixels per cell
    static constexpr int ATLAS_WIDTH = GRID_SIZE * CELL_SIZE;   // 2048 pixels
    static constexpr int ATLAS_HEIGHT = GRID_SIZE * CELL_SIZE;  // 2048 pixels (square)
    static constexpr int TOTAL_CELLS = GRID_SIZE * GRID_SIZE;   // 64 views
};

// A single tree archetype's impostor data
struct TreeImpostorArchetype {
    std::string name;           // e.g., "oak_large", "pine_medium"
    std::string treeType;       // e.g., "oak", "pine"
    float boundingSphereRadius; // For billboard sizing (half of max dimension)
    float centerHeight;         // Height of tree center above base (for billboard offset)
    float treeHeight;           // Actual tree height (maxBounds.y - minBounds.y)
    float baseOffset;           // Offset from mesh origin to tree base (minBounds.y)

    // Atlas textures (owned by TreeImpostorAtlas)
    VkImageView albedoAlphaView = VK_NULL_HANDLE;
    VkImageView normalDepthAOView = VK_NULL_HANDLE;

    // Index into the atlas arrays
    uint32_t atlasIndex = 0;
};

// LOD settings with hysteresis support
struct TreeLODSettings {
    // Distance thresholds (used when useScreenSpaceError = false)
    float fullDetailDistance = TreeLODConstants::FULL_DETAIL_DISTANCE;
    float impostorDistance = 50000.0f;     // Impostors visible up to this distance (very far)

    // Hysteresis (prevents flickering at LOD boundaries)
    float hysteresis = TreeLODConstants::HYSTERESIS;

    // Blending characteristics
    float blendRange = TreeLODConstants::BLEND_RANGE;
    float blendExponent = 1.0f;            // Blend curve (1.0 = linear)

    // Screen-space error LOD
    // Screen error is HIGH when close (object large on screen), LOW when far (object small)
    // Logic: close (high error) = full geometry, far (low error) = impostor/cull
    bool useScreenSpaceError = true;       // Use screen-space error instead of distance
    float errorThresholdFull = TreeLODConstants::ERROR_THRESHOLD_FULL;
    float errorThresholdImpostor = TreeLODConstants::ERROR_THRESHOLD_IMPOSTOR;
    float errorThresholdCull = TreeLODConstants::ERROR_THRESHOLD_CULL;

    // Reduced Detail LOD (LOD1) - intermediate between full geometry and impostor
    // When enabled, trees at medium distance use simplified geometry with fewer, larger leaves
    bool enableReducedDetailLOD = false;   // Enable LOD1 (reduced geometry)
    float errorThresholdReduced = TreeLODConstants::ERROR_THRESHOLD_REDUCED;  // Screen error for LOD1
    float reducedDetailDistance = TreeLODConstants::REDUCED_DETAIL_DISTANCE;  // Distance for LOD1 (non-SSE mode)
    float reducedDetailLeafScale = TreeLODConstants::REDUCED_LEAF_SCALE;      // Leaf size multiplier (default 2x)
    float reducedDetailLeafDensity = TreeLODConstants::REDUCED_LEAF_DENSITY;  // Fraction of leaves (default 50%)

    // Impostor settings
    bool enableImpostors = true;
    float impostorBrightness = 1.0f;       // Brightness adjustment for impostors
    float normalStrength = 1.0f;           // How much normals affect lighting
    bool enableFrameBlending = true;       // Blend between 3 nearest frames for smooth transitions

    // Seasonal effects (global for all impostors)
    float autumnHueShift = 0.0f;           // 0 = summer green, 1 = full autumn colors

    // Shadow cascade settings
    // Controls which cascades render full geometry vs impostors only
    struct ShadowSettings {
        // Cascade >= geometryCascadeCutoff uses impostors only (no branches/leaves)
        // Default: cascades 0-2 get geometry, cascade 3 gets impostors only
        uint32_t geometryCascadeCutoff = 3;

        // Cascade >= leafCascadeCutoff skips leaf shadows entirely
        // Default: cascade 3 has no leaf shadows (impostor shadows only)
        uint32_t leafCascadeCutoff = 3;

        // Whether to use cascade-aware shadow LOD
        bool enableCascadeLOD = true;
    } shadow;
};
