// Octahedral impostor mapping utilities
// Based on techniques from:
// - ShaderBits (Ryan Brucks/Epic Games): https://shaderbits.com/blog/octahedral-impostors
// - Octahedral normal encoding: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
//
// For trees we use hemi-octahedral mapping since trees are only viewed from above/sides, not below.
// This gives better resolution for typical viewing angles.

#ifndef OCTAHEDRAL_MAPPING_GLSL
#define OCTAHEDRAL_MAPPING_GLSL

// Configuration for octahedral impostor atlas
// Grid size determines how many frames are captured (GRID_SIZE x GRID_SIZE)
const int OCTA_GRID_SIZE = 8;  // 8x8 = 64 views

// Helper: sign function that returns 1.0 for zero (used in octahedral wrap)
vec2 octaSignNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// Encode a 3D direction to 2D octahedral UV coordinates [0, 1]
// For full octahedron (all directions including below horizon)
vec2 octaEncode(vec3 dir) {
    // L1 normalize: project onto octahedron surface
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));

    // Wrap lower hemisphere
    if (dir.y < 0.0) {
        dir.xz = (1.0 - abs(dir.zx)) * octaSignNotZero(dir.xz);
    }

    // Map from [-1, 1] to [0, 1]
    return dir.xz * 0.5 + 0.5;
}

// Decode 2D octahedral UV coordinates [0, 1] to 3D direction
vec3 octaDecode(vec2 uv) {
    // Map from [0, 1] to [-1, 1]
    uv = uv * 2.0 - 1.0;

    // Reconstruct Z from X and Y using octahedral geometry
    vec3 dir = vec3(uv.x, 1.0 - abs(uv.x) - abs(uv.y), uv.y);

    // Handle wrapped lower hemisphere
    if (dir.y < 0.0) {
        float t = clamp(-dir.y, 0.0, 1.0);
        // Component-wise comparison (GLSL doesn't support >= on vec2)
        dir.x += (dir.x >= 0.0 ? -t : t);
        dir.z += (dir.z >= 0.0 ? -t : t);
    }

    return normalize(dir);
}

// Hemi-octahedral encoding: only upper hemisphere (y >= 0)
// Better resolution for trees since we never view from below
vec2 hemiOctaEncode(vec3 dir) {
    // Ensure direction is in upper hemisphere
    dir.y = max(dir.y, 0.001);

    // L1 normalize
    float sum = abs(dir.x) + abs(dir.y) + abs(dir.z);
    dir /= sum;

    // For upper hemisphere, we can use XZ directly
    // The hemi-octahedron maps to a diamond shape that we stretch to fill [0,1]
    vec2 enc = dir.xz;

    // Transform from diamond [-1,1] to square [0,1]
    // Using hemi-octahedral mapping: rotate 45 degrees and scale
    vec2 result;
    result.x = enc.x + enc.y;
    result.y = enc.y - enc.x;

    // Map to [0, 1]
    return result * 0.5 + 0.5;
}

// Hemi-octahedral decoding: from UV [0, 1] to 3D direction
vec3 hemiOctaDecode(vec2 uv) {
    // Map from [0, 1] to [-1, 1]
    uv = uv * 2.0 - 1.0;

    // Inverse of the diamond rotation
    vec2 enc;
    enc.x = (uv.x - uv.y) * 0.5;
    enc.y = (uv.x + uv.y) * 0.5;

    // Reconstruct Y from X and Z
    float y = 1.0 - abs(enc.x) - abs(enc.y);

    return normalize(vec3(enc.x, max(y, 0.0), enc.y));
}

// Find the grid cell containing the given UV coordinate
ivec2 octaGetGridCell(vec2 uv, int gridSize) {
    return ivec2(floor(uv * float(gridSize)));
}

// Get the UV coordinate of a grid cell center
vec2 octaGetCellCenter(ivec2 cell, int gridSize) {
    return (vec2(cell) + 0.5) / float(gridSize);
}

// Structure for storing frame blending information
struct OctaFrameData {
    vec2 frameUV0;      // UV for frame 0 (current frame)
    vec2 frameUV1;      // UV for frame 1 (neighbor)
    vec2 frameUV2;      // UV for frame 2 (neighbor)
    float weight0;      // Blend weight for frame 0
    float weight1;      // Blend weight for frame 1
    float weight2;      // Blend weight for frame 2
    vec3 frameDir0;     // View direction for frame 0
    vec3 frameDir1;     // View direction for frame 1
    vec3 frameDir2;     // View direction for frame 2
};

// Find the 3 nearest frames and their blend weights for smooth interpolation
// Based on Ryan Brucks' technique from Fortnite impostors
OctaFrameData octaGetFrameBlendData(vec3 viewDir, int gridSize, bool useHemi) {
    OctaFrameData data;

    // Encode view direction to UV
    vec2 uv = useHemi ? hemiOctaEncode(viewDir) : octaEncode(viewDir);

    // Find the grid cell (quad) we're in
    float gridF = float(gridSize);
    vec2 scaledUV = uv * gridF;
    ivec2 cell = ivec2(floor(scaledUV));

    // Clamp to valid range
    cell = clamp(cell, ivec2(0), ivec2(gridSize - 1));

    // Fractional position within the cell [0, 1]
    vec2 frac = fract(scaledUV);

    // Determine which triangle half we're in (upper-left or lower-right diagonal)
    // Upper-left triangle: use current, right, down
    // Lower-right triangle: use current, right, lower-right
    bool upperTriangle = (frac.x + frac.y) < 1.0;

    // Frame indices (cells)
    ivec2 frame0 = cell;                                    // Current (always used)
    ivec2 frame1 = cell + ivec2(1, 0);                      // Right neighbor
    ivec2 frame2 = upperTriangle
        ? cell + ivec2(0, 1)                                // Down neighbor (upper triangle)
        : cell + ivec2(1, 1);                               // Diagonal neighbor (lower triangle)

    // Clamp frames to valid range
    frame0 = clamp(frame0, ivec2(0), ivec2(gridSize - 1));
    frame1 = clamp(frame1, ivec2(0), ivec2(gridSize - 1));
    frame2 = clamp(frame2, ivec2(0), ivec2(gridSize - 1));

    // Compute blend weights using barycentric-like coordinates
    if (upperTriangle) {
        // Upper triangle: vertices at (0,0), (1,0), (0,1)
        data.weight0 = 1.0 - frac.x - frac.y;  // Current frame
        data.weight1 = frac.x;                  // Right frame
        data.weight2 = frac.y;                  // Down frame
    } else {
        // Lower triangle: vertices at (1,1), (0,1), (1,0) - but we use (1,0), (1,1), (0,1)
        // Remap: we're interpolating between right (1,0), diagonal (1,1), and down (0,1)
        // Transform fractional coords: from lower triangle to unit triangle
        vec2 lowerFrac = frac - vec2(0.0);
        float u = 1.0 - lowerFrac.y;
        float v = lowerFrac.x + lowerFrac.y - 1.0;
        float w = lowerFrac.y - lowerFrac.x;

        // The weights for lower triangle
        data.weight0 = max(1.0 - frac.x - frac.y + 1.0, 0.0) - 1.0;  // Negative, won't use
        data.weight1 = 1.0 - frac.y;                                  // Right frame
        data.weight2 = frac.x + frac.y - 1.0;                        // Diagonal frame

        // Recalculate for proper lower triangle blending
        // Lower triangle corners: (1,0)=right, (1,1)=diagonal, (0,1)=down (but we use current as base)
        data.weight0 = 1.0 - frac.x;      // Contribution from current column
        data.weight1 = frac.x - frac.y + frac.y;  // Right weight
        data.weight2 = frac.y;            // Lower row contribution

        // Simplified: for lower triangle, adjust weights
        data.weight0 = (1.0 - frac.x);
        data.weight1 = frac.x * (1.0 - frac.y) / max(frac.x + frac.y - 1.0 + 1.0, 0.001);
        data.weight2 = 1.0 - data.weight0 - data.weight1;
    }

    // Simpler approach: just use smooth bilinear-like weights based on distance to frame centers
    vec2 center0 = (vec2(frame0) + 0.5) / gridF;
    vec2 center1 = (vec2(frame1) + 0.5) / gridF;
    vec2 center2 = (vec2(frame2) + 0.5) / gridF;

    float dist0 = length(uv - center0);
    float dist1 = length(uv - center1);
    float dist2 = length(uv - center2);

    // Inverse distance weighting
    float invDist0 = 1.0 / max(dist0, 0.001);
    float invDist1 = 1.0 / max(dist1, 0.001);
    float invDist2 = 1.0 / max(dist2, 0.001);
    float sumInv = invDist0 + invDist1 + invDist2;

    data.weight0 = invDist0 / sumInv;
    data.weight1 = invDist1 / sumInv;
    data.weight2 = invDist2 / sumInv;

    // Compute UVs within each frame
    // Each frame spans 1/gridSize of the atlas
    float cellSize = 1.0 / gridF;

    // Local UV within each frame (same position relative to each frame center)
    // This is the "virtual projection" concept - we need to render each frame
    // as if looking from that frame's view direction
    vec2 localUV = frac;  // Position within current cell

    data.frameUV0 = (vec2(frame0) + localUV) * cellSize;
    data.frameUV1 = (vec2(frame1) + localUV) * cellSize;
    data.frameUV2 = (vec2(frame2) + localUV) * cellSize;

    // Get the view directions for each frame (for billboard orientation)
    data.frameDir0 = useHemi ? hemiOctaDecode(center0) : octaDecode(center0);
    data.frameDir1 = useHemi ? hemiOctaDecode(center1) : octaDecode(center1);
    data.frameDir2 = useHemi ? hemiOctaDecode(center2) : octaDecode(center2);

    return data;
}

// Simple single-frame octahedral lookup (no blending)
vec2 octaGetSingleFrameUV(vec3 viewDir, int gridSize, bool useHemi) {
    vec2 uv = useHemi ? hemiOctaEncode(viewDir) : octaEncode(viewDir);
    return uv;
}

// Get the frame index (0 to gridSize*gridSize-1) for a given view direction
int octaGetFrameIndex(vec3 viewDir, int gridSize, bool useHemi) {
    vec2 uv = useHemi ? hemiOctaEncode(viewDir) : octaEncode(viewDir);
    ivec2 cell = ivec2(floor(uv * float(gridSize)));
    cell = clamp(cell, ivec2(0), ivec2(gridSize - 1));
    return cell.y * gridSize + cell.x;
}

// Get the view direction for a specific frame index
vec3 octaGetFrameDirection(int frameIndex, int gridSize, bool useHemi) {
    int x = frameIndex % gridSize;
    int y = frameIndex / gridSize;
    vec2 uv = (vec2(x, y) + 0.5) / float(gridSize);
    return useHemi ? hemiOctaDecode(uv) : octaDecode(uv);
}

#endif // OCTAHEDRAL_MAPPING_GLSL
