#version 450

const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Wind uniform buffer (must match grass.vert for shadow consistency)
layout(binding = 2) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;  // Which cascade we're rendering
} push;

// Perlin noise implementation (same as grass.vert for consistent shadows)
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
        mix(grad(perm[AA], x, y),
            grad(perm[BA], x - 1.0, y), u),
        mix(grad(perm[AB], x, y - 1.0),
            grad(perm[BB], x - 1.0, y - 1.0), u),
        v
    );

    return (res + 1.0) * 0.5;
}

// Sample wind using scrolling Perlin noise
// Must match grass.vert for consistent shadows
float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windSpeed = wind.windDirectionAndStrength.w;
    float windTime = wind.windParams.w;
    float gustFreq = wind.windParams.x;
    float gustAmp = wind.windParams.y;

    // Scroll position in wind direction
    vec2 scrolledPos = worldPos - windDir * windTime * windSpeed * 0.4;

    // Three octaves: ~10m, ~5m, ~2.5m wavelengths
    float baseFreq = 0.1;
    float n1 = perlinNoise(scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = perlinNoise(scrolledPos.x * baseFreq * 2.0, scrolledPos.y * baseFreq * 2.0);
    float n3 = perlinNoise(scrolledPos.x * baseFreq * 4.0, scrolledPos.y * baseFreq * 4.0);

    // Weighted sum dominated by first octave
    float noise = n1 * 0.7 + n2 * 0.2 + n3 * 0.1;

    // Add time-varying gust
    float gust = (sin(windTime * gustFreq * 6.28318) * 0.5 + 0.5) * gustAmp;

    return (noise + gust) * windStrength;
}

layout(location = 0) out float fragHeight;
layout(location = 1) out float fragHash;

// Quadratic Bezier evaluation
vec3 bezier(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
}

void main() {
    // Get instance data using gl_InstanceIndex
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 basePos = inst.positionAndFacing.xyz;
    float facing = inst.positionAndFacing.w;
    float height = inst.heightHashTilt.x;
    float bladeHash = inst.heightHashTilt.y;
    float tilt = inst.heightHashTilt.z;

    // Wind animation using noise-based wind system (must match grass.vert for consistent shadows)
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windTime = wind.windParams.w;

    // Sample wind strength at this blade's position
    float windSample = sampleWind(vec2(basePos.x, basePos.z));

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(windTime * 2.5 + windPhase) * 0.3;

    // Wind offset (same calculation as grass.vert)
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;
    float windEffect = (windSample + phaseOffset) * 0.25;
    float windOffset = windEffect * cos(relativeWindAngle);

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5, height * 0.5, 0.0);  // Mid control
    vec3 p2 = vec3(windOffset + tilt, height, 0.0);  // Tip

    // Triangle strip blade geometry: 15 vertices = 7 segments (8 height levels)
    uint vi = gl_VertexIndex;
    const uint NUM_SEGMENTS = 7;
    const float baseWidth = 0.02;

    uint segmentIndex = vi / 2;
    bool isRightSide = (vi % 2) == 1;

    float t = float(segmentIndex) / float(NUM_SEGMENTS);
    float widthAtT = baseWidth * (1.0 - t * 0.9);

    if (vi == 14) {
        widthAtT = 0.0;
        t = 1.0;
    }

    float xOffset = isRightSide ? widthAtT : -widthAtT;

    // Get position on bezier curve
    vec3 curvePos = bezier(p0, p1, p2, t);
    vec3 localPos = curvePos + vec3(xOffset, 0.0, 0.0);

    // Rotate by facing angle around Y axis
    float cs = cos(facing);
    float sn = sin(facing);
    vec3 rotatedPos;
    rotatedPos.x = localPos.x * cs - localPos.z * sn;
    rotatedPos.y = localPos.y;
    rotatedPos.z = localPos.x * sn + localPos.z * cs;

    // Final world position
    vec3 worldPos = basePos + rotatedPos;

    // Transform to light space for shadow map using cascade-specific matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    // Pass height and hash for dithering in fragment shader
    fragHeight = t;
    fragHash = bladeHash;
}
