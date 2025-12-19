// Tree Wind Animation
// Based on Ghost of Tsushima's tree wind system (GDC 2021)
//
// Key concepts from the talk:
// - 3-level skeleton: trunk (0), branch (1), sub-branch (2+)
// - Each vertex stores branch origin point for rotation
// - Wind rotates branches around their origin
// - Sinusoidal sway with position-hashed phase
// - Phase variation prevents adjacent trees/branches moving in lockstep

#ifndef TREE_WIND_GLSL
#define TREE_WIND_GLSL

// Perlin noise permutation table (same as grass.vert for consistency)
const int windPerm[512] = int[512](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    // Repeat for wrap-around
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
);

float windFade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float windGrad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

float treePerlinNoise(float x, float y) {
    int X = int(floor(x)) & 255;
    int Y = int(floor(y)) & 255;

    x -= floor(x);
    y -= floor(y);

    float u = windFade(x);
    float v = windFade(y);

    int A = windPerm[X] + Y;
    int AA = windPerm[A];
    int AB = windPerm[A + 1];
    int B = windPerm[X + 1] + Y;
    int BA = windPerm[B];
    int BB = windPerm[B + 1];

    float res = mix(
        mix(windGrad(windPerm[AA], x, y),
            windGrad(windPerm[BA], x - 1.0, y), u),
        mix(windGrad(windPerm[AB], x, y - 1.0),
            windGrad(windPerm[BB], x - 1.0, y - 1.0), u),
        v
    );

    return (res + 1.0) * 0.5;
}

// Hash function for position-based phase variation
// This makes adjacent trees/branches have different phases
float treeWindHash(vec3 pos) {
    vec3 p = fract(pos * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// Sample multi-octave wind noise at a world position
// Returns wind strength multiplier (0-1 range, centered around 0.5)
float sampleTreeWind(vec2 worldPos, vec2 windDir, float windSpeed, float time) {
    // Scroll position in wind direction
    vec2 scrolledPos = worldPos - windDir * time * windSpeed * 0.4;

    // Three octaves: ~10m, ~5m, ~2.5m wavelengths
    float baseFreq = 0.1;
    float n1 = treePerlinNoise(scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = treePerlinNoise(scrolledPos.x * baseFreq * 2.0, scrolledPos.y * baseFreq * 2.0);
    float n3 = treePerlinNoise(scrolledPos.x * baseFreq * 4.0, scrolledPos.y * baseFreq * 4.0);

    // Weighted sum (like GoT: 0.7 + 0.2 + 0.1)
    return n1 * 0.7 + n2 * 0.2 + n3 * 0.1;
}

// Rotate a point around an origin by an axis-angle rotation
// This is the core of branch sway - rotating vertices around branch origin
vec3 rotateAroundAxis(vec3 point, vec3 origin, vec3 axis, float angle) {
    vec3 offset = point - origin;

    // Rodrigues' rotation formula
    float c = cos(angle);
    float s = sin(angle);
    float oneMinusC = 1.0 - c;

    vec3 rotated = offset * c +
                   cross(axis, offset) * s +
                   axis * dot(axis, offset) * oneMinusC;

    return origin + rotated;
}

// Apply wind sway to a tree vertex
// Based on GoT technique: rotate each branch around its origin away from wind
//
// Parameters:
//   position:     Vertex position in model space
//   branchOrigin: Origin point of the branch this vertex belongs to
//   branchLevel:  0 = trunk, 1 = branch, 2+ = sub-branch
//   flexibility:  0 = rigid (at branch base), 1 = fully flexible (at tip)
//   phaseOffset:  Per-vertex phase for motion variation
//   branchLength: Length of this branch (for scaling motion)
//   windDir:      Normalized wind direction (XZ plane)
//   windStrength: Wind intensity (0 = calm, 1+ = strong)
//   time:         Current time for animation
//   treeWorldPos: World position of tree root (for noise sampling)
//
// Returns: Wind-displaced position in model space
vec3 applyTreeWind(
    vec3 position,
    vec3 branchOrigin,
    float branchLevel,
    float flexibility,
    float phaseOffset,
    float branchLength,
    vec2 windDir,
    float windStrength,
    float windSpeed,
    float time,
    vec3 treeWorldPos
) {
    // Skip if no wind or vertex is at branch base (rigid)
    if (windStrength < 0.001 || flexibility < 0.001) {
        return position;
    }

    // Sample wind noise at tree position for spatial variation
    float windNoise = sampleTreeWind(treeWorldPos.xz, windDir, windSpeed, time);

    // Branch level affects sway characteristics:
    // - Trunk (level 0): Large, slow movements
    // - Branch (level 1): Medium movements, faster
    // - Sub-branch (level 2+): Small, rapid flutter
    float levelMultiplier = 1.0;
    float swayFrequency = 1.5;
    float swayAmplitude = 0.025;

    if (branchLevel < 0.5) {
        // Trunk: slow, subtle sway
        levelMultiplier = 0.3;
        swayFrequency = 0.8;
        swayAmplitude = 0.01;
    } else if (branchLevel < 1.5) {
        // Primary branches: moderate sway
        levelMultiplier = 0.7;
        swayFrequency = 1.5;
        swayAmplitude = 0.02;
    } else {
        // Sub-branches and leaves: faster flutter
        levelMultiplier = 1.0;
        swayFrequency = 2.5 + branchLevel * 0.5;
        swayAmplitude = 0.03;
    }

    // Sinusoidal sway with position-hashed phase
    // This is the key technique from GoT for natural-looking motion
    float sway = sin(time * swayFrequency + phaseOffset) * swayAmplitude;

    // Total wind effect combines:
    // 1. Constant bias away from wind direction
    // 2. Oscillating sway
    // 3. Spatial noise variation
    float totalWindEffect = (windNoise - 0.5 + sway) * windStrength * levelMultiplier;

    // Scale effect by flexibility (distance from branch base)
    totalWindEffect *= flexibility;

    // Scale by branch length (longer branches sway more at tips)
    totalWindEffect *= sqrt(branchLength);

    // Clamp to prevent extreme deformation
    totalWindEffect = clamp(totalWindEffect, -0.15, 0.15);

    // Calculate rotation axis: perpendicular to both wind direction and up
    vec3 windDir3D = normalize(vec3(windDir.x, 0.0, windDir.y));
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 rotationAxis = normalize(cross(up, windDir3D));

    // Add small perpendicular sway for more natural motion
    float perpSway = sin(time * swayFrequency * 1.3 + phaseOffset + 1.57) * swayAmplitude * 0.2;
    float perpEffect = perpSway * windStrength * levelMultiplier * flexibility;

    // Apply main rotation (bending in wind direction)
    vec3 result = rotateAroundAxis(position, branchOrigin, rotationAxis, totalWindEffect);

    // Apply perpendicular sway (small side-to-side motion)
    vec3 perpAxis = windDir3D;
    result = rotateAroundAxis(result, branchOrigin, perpAxis, perpEffect);

    return result;
}

// Simplified version for leaves - faster, more flutter
vec3 applyLeafWind(
    vec3 position,
    vec3 attachPoint,
    float phaseOffset,
    vec2 windDir,
    float windStrength,
    float time
) {
    if (windStrength < 0.001) {
        return position;
    }

    // Leaves have rapid flutter
    float flutter = sin(time * 5.0 + phaseOffset) * 0.03 +
                    sin(time * 7.3 + phaseOffset * 1.5) * 0.015;

    float totalEffect = flutter * windStrength;

    vec3 windDir3D = normalize(vec3(windDir.x, 0.0, windDir.y));
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 rotationAxis = normalize(cross(up, windDir3D));

    // Add some twist around leaf normal
    float twist = sin(time * 3.0 + phaseOffset * 2.0) * 0.05 * windStrength;

    vec3 result = rotateAroundAxis(position, attachPoint, rotationAxis, totalEffect);

    // Twist around wind direction
    result = rotateAroundAxis(result, attachPoint, windDir3D, twist);

    return result;
}

#endif // TREE_WIND_GLSL
