/*
 * virtual_texture.glsl - Virtual texture sampling functions
 *
 * Implements runtime virtual texture lookup with:
 * - Page table (indirection texture) lookup
 * - Physical cache sampling
 * - GPU feedback for tile requests
 * - Mip level calculation and fallback
 */

#ifndef VIRTUAL_TEXTURE_GLSL
#define VIRTUAL_TEXTURE_GLSL

#include "bindings.glsl"

// Maximum feedback entries per frame
#define VT_MAX_FEEDBACK_ENTRIES 4096

// VT Parameters UBO (matches VTParamsUBO in VirtualTextureTypes.h)
layout(std140, binding = BINDING_VT_PARAMS_UBO) uniform VTParamsUBO {
    vec4 virtualTextureSizeAndInverse;     // xy = size, zw = 1/size
    vec4 physicalCacheSizeAndInverse;      // xy = size, zw = 1/size
    vec4 tileSizeAndBorder;                // x = tile size, y = border, z = tile with border, w = unused
    uint maxMipLevel;
    // Note: Use individual uints instead of uint[3] array because std140
    // layout gives arrays a 16-byte stride per element, creating a size
    // mismatch with the C++ struct where uints are packed contiguously.
    uint vtPadding0;
    uint vtPadding1;
    uint vtPadding2;
} vtParams;

// Page table (indirection) - texture array with one layer per mip level
layout(binding = BINDING_VT_PAGE_TABLE) uniform usampler2DArray vtPageTable;

// Physical cache texture
layout(binding = BINDING_VT_PHYSICAL_CACHE) uniform sampler2D vtCache;

// Feedback buffer for tile requests (entry storage)
layout(std430, binding = BINDING_VT_FEEDBACK) buffer VTFeedback {
    uint feedbackEntries[VT_MAX_FEEDBACK_ENTRIES];
};

// Feedback counter (separate buffer for atomic operations - matches VirtualTextureFeedback design)
layout(std430, binding = BINDING_VT_FEEDBACK_COUNTER) buffer VTFeedbackCounter {
    uint vtFeedbackCounter;
};

// Pack tile ID for feedback (matches TileId::pack() in C++)
uint vtPackTileId(uvec2 tileCoord, uint mipLevel) {
    return (mipLevel << 20u) | ((tileCoord.y & 0x3FFu) << 10u) | (tileCoord.x & 0x3FFu);
}

// Calculate mip level from screen-space derivatives
float vtCalculateMipLevel(vec2 virtualUV) {
    // Calculate derivatives in virtual texture space
    vec2 dx = dFdx(virtualUV * vtParams.virtualTextureSizeAndInverse.x);
    vec2 dy = dFdy(virtualUV * vtParams.virtualTextureSizeAndInverse.x);

    // Use the larger derivative to determine mip level
    float maxDerivSq = max(dot(dx, dx), dot(dy, dy));

    // log2(sqrt(x)) = 0.5 * log2(x)
    return 0.5 * log2(max(maxDerivSq, 1.0));
}

// Write tile request to feedback buffer
void vtWriteFeedback(uvec2 tileCoord, uint mipLevel) {
    uint packed = vtPackTileId(tileCoord, mipLevel);

    // Atomically increment counter and get index
    uint idx = atomicAdd(vtFeedbackCounter, 1u);

    // Only write if we haven't exceeded buffer capacity
    if (idx < VT_MAX_FEEDBACK_ENTRIES) {
        feedbackEntries[idx] = packed;
    }
}

// Sample virtual texture with automatic mip selection
vec4 sampleVirtualTexture(vec2 virtualUV, float mipLevel) {
    // Clamp to valid mip range
    uint mip = clamp(uint(mipLevel + 0.5), 0u, vtParams.maxMipLevel);

    // Calculate number of tiles at this mip level
    float tileSize = vtParams.tileSizeAndBorder.x;
    float virtualSize = vtParams.virtualTextureSizeAndInverse.x;
    float tilesAtMip = virtualSize / (tileSize * float(1u << mip));

    // Calculate tile coordinates
    vec2 tileCoordF = virtualUV * tilesAtMip;
    ivec2 tileCoord = ivec2(floor(tileCoordF));

    // Clamp tile coordinates to valid range
    int maxTileCoord = int(tilesAtMip) - 1;
    tileCoord = clamp(tileCoord, ivec2(0), ivec2(maxTileCoord));

    // Calculate UV within the tile [0, 1]
    vec2 inTileUV = fract(tileCoordF);

    // Fetch page table entry from texture array
    // Each mip level is stored as a separate layer in the texture array
    // Entry format: R = cacheX, G = cacheY, B = unused, A = valid
    uvec4 pageEntry = texelFetch(vtPageTable, ivec3(tileCoord.x, tileCoord.y, int(mip)), 0);

    // Check if tile is loaded (valid flag in alpha channel)
    if (pageEntry.a == 0u) {
        // Tile not loaded - write feedback request
        vtWriteFeedback(uvec2(tileCoord), mip);

        // Try to use coarser mip level as fallback
        if (mip < vtParams.maxMipLevel) {
            return sampleVirtualTexture(virtualUV, float(mip + 1u));
        }

        // Ultimate fallback - return placeholder color (pink/magenta for debugging)
        return vec4(1.0, 0.0, 1.0, 1.0);
    }

    // Transform to physical cache coordinates
    vec2 cachePos = vec2(pageEntry.xy);  // Cache tile position (in tiles)
    float border = vtParams.tileSizeAndBorder.y;
    float tileWithBorder = vtParams.tileSizeAndBorder.z;
    float cacheSize = vtParams.physicalCacheSizeAndInverse.x;

    // Calculate physical UV
    // Each tile in cache includes border pixels for filtering
    // The actual tile content is (tileSize - 2*border) in the center
    float usableTileSize = tileSize;  // We use the full tile but account for border in UV
    vec2 physicalUV = (cachePos * tileWithBorder + border + inTileUV * usableTileSize) / cacheSize;

    return texture(vtCache, physicalUV);
}

// Convenience function with automatic mip calculation
vec4 sampleVirtualTextureAuto(vec2 virtualUV) {
    float mipLevel = vtCalculateMipLevel(virtualUV);
    return sampleVirtualTexture(virtualUV, mipLevel);
}

// Sample with bias (useful for anisotropic filtering approximation)
vec4 sampleVirtualTextureBias(vec2 virtualUV, float bias) {
    float mipLevel = vtCalculateMipLevel(virtualUV) + bias;
    return sampleVirtualTexture(virtualUV, mipLevel);
}

#endif // VIRTUAL_TEXTURE_GLSL
