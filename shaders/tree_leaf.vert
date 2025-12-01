#version 450

const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

// Leaf instance data - matches compute shader output
struct LeafInstance {
    vec4 position;      // xyz = world position, w = size
    vec4 facing;        // xy = facing direction, z = windPhase (clump-based), w = flutter
    vec4 color;         // rgb = color tint, a = alpha
    uvec4 metadata;     // x = tree index, y = flags, z = clumpId bits, w = unused
};

layout(std430, binding = 1) readonly buffer LeafBuffer {
    LeafInstance leaves[];
};

// Wind uniform buffer - shared with grass and branches
layout(binding = 3) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;
    float padding[2];
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragColor;
layout(location = 4) out float fragDepth;

// ============================================================================
// Perlin noise (matching branch shader for consistent wind)
// ============================================================================
const int perm[512] = int[512](
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
    // Repeat
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

float fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

float perlinNoise(float x, float y) {
    int X = int(floor(x)) & 255;
    int Y = int(floor(y)) & 255;
    x -= floor(x);
    y -= floor(y);
    float u = fade(x);
    float v = fade(y);
    int A = perm[X] + Y;
    int AA = perm[A];
    int AB = perm[A + 1];
    int B = perm[X + 1] + Y;
    int BA = perm[B];
    int BB = perm[B + 1];
    float res = mix(
        mix(grad(perm[AA], x, y), grad(perm[BA], x - 1.0, y), u),
        mix(grad(perm[AB], x, y - 1.0), grad(perm[BB], x - 1.0, y - 1.0), u),
        v
    );
    return (res + 1.0) * 0.5;
}

// Sample wind at position - matches branch shader for hierarchical consistency
float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windSpeed = wind.windDirectionAndStrength.w;
    float windTime = wind.windParams.w;

    vec2 scrolledPos = worldPos - windDir * windTime * windSpeed * 0.4;
    float baseFreq = 0.1;
    float n1 = perlinNoise(scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = perlinNoise(scrolledPos.x * baseFreq * 2.0, scrolledPos.y * baseFreq * 2.0);
    float noise = n1 * 0.7 + n2 * 0.3;

    return noise * windStrength;
}

// ============================================================================
// Main vertex shader
// ============================================================================
void main() {
    uint leafIndex = gl_InstanceIndex;
    uint vertIndex = gl_VertexIndex;

    LeafInstance leaf = leaves[leafIndex];

    vec3 leafPos = leaf.position.xyz;
    float leafSize = leaf.position.w;
    vec2 facingDir = leaf.facing.xy;
    float windPhase = leaf.facing.z;      // Clump-based phase offset
    float flutter = leaf.facing.w;
    vec3 leafColor = leaf.color.rgb;

    // Get camera direction for billboarding
    vec3 toCamera = normalize(ubo.cameraPosition.xyz - leafPos);

    // Create billboard axes with facing-based tilt
    vec3 up = vec3(0.0, 1.0, 0.0);
    up = normalize(up + vec3(facingDir.x, 0.0, facingDir.y) * 0.3);

    vec3 right = normalize(cross(up, toCamera));
    vec3 forward = cross(right, up);

    // Quad vertex positions (CCW winding, triangle list)
    vec2 quadPos;
    vec2 uv;

    if (vertIndex == 0u) {
        quadPos = vec2(-0.5, -0.5);
        uv = vec2(0.0, 1.0);
    } else if (vertIndex == 1u) {
        quadPos = vec2(0.5, -0.5);
        uv = vec2(1.0, 1.0);
    } else if (vertIndex == 2u) {
        quadPos = vec2(0.5, 0.5);
        uv = vec2(1.0, 0.0);
    } else if (vertIndex == 3u) {
        quadPos = vec2(-0.5, -0.5);
        uv = vec2(0.0, 1.0);
    } else if (vertIndex == 4u) {
        quadPos = vec2(0.5, 0.5);
        uv = vec2(1.0, 0.0);
    } else {  // vertIndex == 5
        quadPos = vec2(-0.5, 0.5);
        uv = vec2(0.0, 0.0);
    }

    // Apply leaf size
    vec3 offset = right * quadPos.x * leafSize + up * quadPos.y * leafSize;
    vec3 worldPos = leafPos + offset;

    // ========================================================================
    // Hierarchical wind animation (Milestone 4)
    // Wind affects leaves more than branches, with clump synchronization
    // ========================================================================
    vec2 windDir2D = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windTime = wind.windParams.w;

    // Sample base wind at leaf position (same as branches)
    float windSample = sampleWind(leafPos.xz);

    // Primary sway - synced with branch movement
    // Leaves move more than branches at the same position
    float primarySway = windSample * 0.4;

    // Secondary flutter - high frequency oscillation
    // Uses clump-based phase so nearby leaves move together
    float flutterFreq = 6.0 + flutter * 4.0;  // 6-10 Hz flutter
    float flutterPhase1 = push.time * flutterFreq + windPhase;
    float flutterPhase2 = push.time * flutterFreq * 1.3 + windPhase * 0.7;

    // Multi-frequency flutter for natural look
    float flutterAmount = sin(flutterPhase1) * 0.6 + sin(flutterPhase2) * 0.4;
    flutterAmount *= flutter * windSample;

    // Tertiary micro-movement - very high frequency shimmer
    float shimmerPhase = push.time * 15.0 + windPhase * 3.0;
    float shimmer = sin(shimmerPhase) * 0.15 * windStrength;

    // Combine wind components
    vec3 windOffset = vec3(windDir2D.x, 0.0, windDir2D.y) * primarySway;

    // Flutter in perpendicular direction with slight vertical
    vec3 perpDir = normalize(vec3(-windDir2D.y, 0.25, windDir2D.x));
    windOffset += perpDir * flutterAmount * 0.25;

    // Add shimmer
    windOffset += perpDir * shimmer * 0.1;

    // Slight vertical bob synced with horizontal sway
    float verticalBob = sin(push.time * 3.0 + windPhase) * windSample * 0.05;
    windOffset.y += verticalBob;

    worldPos += windOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Output
    fragWorldPos = worldPos;
    fragNormal = toCamera;
    fragUV = uv;
    fragColor = leafColor;
    fragDepth = -(ubo.view * vec4(worldPos, 1.0)).z;
}
