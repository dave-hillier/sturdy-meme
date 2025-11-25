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
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = unused
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Wind uniform buffer
layout(binding = 3) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
} push;

// Perlin noise implementation for wind variation
// Uses the same permutation table as CPU for consistency
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

// Sample wind at a world position with scrolling noise
float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windSpeed = wind.windDirectionAndStrength.w;
    float noiseScale = wind.windParams.z;
    float windTime = wind.windParams.w;
    float gustFreq = wind.windParams.x;
    float gustAmp = wind.windParams.y;

    // Scroll position in wind direction
    vec2 scrolledPos = worldPos - windDir * windTime * windSpeed;

    // Multi-octave noise for natural variation
    float noise = 0.0;
    float amplitude = 1.0;
    float frequency = noiseScale;
    float maxAmp = 0.0;

    for (int i = 0; i < 2; i++) {
        noise += perlinNoise(scrolledPos.x * frequency, scrolledPos.y * frequency) * amplitude;
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    noise /= maxAmp;

    // Add time-varying gust
    float gust = (sin(windTime * gustFreq * 6.28318) * 0.5 + 0.5) * gustAmp;

    return (noise + gust) * windStrength;
}

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragHeight;
layout(location = 3) out float fragClumpId;
layout(location = 4) out vec3 fragWorldPos;

// Quadratic Bezier evaluation
vec3 bezier(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
}

// Quadratic Bezier derivative
vec3 bezierDerivative(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return 2.0 * u * (p1 - p0) + 2.0 * t * (p2 - p1);
}

void main() {
    // With indirect draw:
    // - gl_InstanceIndex = which blade (0 to instanceCount-1)
    // - gl_VertexIndex = which vertex within this blade (0 to vertexCount-1)

    // Get instance data using gl_InstanceIndex
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 basePos = inst.positionAndFacing.xyz;
    float facing = inst.positionAndFacing.w;
    float height = inst.heightHashTilt.x;
    float bladeHash = inst.heightHashTilt.y;
    float tilt = inst.heightHashTilt.z;
    float clumpId = inst.heightHashTilt.w;

    // Wind animation using noise-based wind system
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windTime = wind.windParams.w;

    // Sample wind strength at this blade's position
    float windSample = sampleWind(vec2(basePos.x, basePos.z));

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(windTime * 2.5 + windPhase) * 0.3;

    // Wind offset in wind direction (converted to local X space after rotation)
    // The blade faces in the 'facing' direction, so we need to project wind onto blade space
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;

    // Wind effect is stronger when wind is perpendicular to blade facing
    // This creates more natural bending
    float windEffect = (windSample + phaseOffset) * 0.4;
    float windOffset = windEffect * cos(relativeWindAngle);

    // Blade folding for short grass
    // Shorter blades fold over more (like real lawn grass bending under weight)
    // Height range is ~0.3-0.7, normalize to 0-1 for folding calculation
    float normalizedHeight = clamp((height - 0.3) / 0.4, 0.0, 1.0);

    // Fold amount: short grass (0) folds a lot, tall grass (1) stays upright
    // foldAmount ranges from 0.6 (short) to 0.1 (tall)
    float foldAmount = mix(0.6, 0.1, normalizedHeight);

    // Add per-blade variation to fold direction using hash
    float foldDirection = (bladeHash - 0.5) * 2.0;  // -1 to 1
    float foldX = foldDirection * foldAmount * height;

    // Short grass also droops more - tip ends up lower relative to height
    float droopFactor = mix(0.3, 0.0, normalizedHeight);  // 30% droop for shortest
    float effectiveHeight = height * (1.0 - droopFactor);

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5 + foldX * 0.5, height * 0.6, 0.0);  // Mid control - higher for fold
    vec3 p2 = vec3(windOffset + tilt + foldX, effectiveHeight, 0.0);  // Tip - with fold and droop

    // Triangle strip blade geometry: 15 vertices = 7 segments (8 height levels)
    // Even vertices (0,2,4,6,8,10,12,14) are left side
    // Odd vertices (1,3,5,7,9,11,13) are right side
    // Vertex 14 is the tip point (width = 0)
    uint vi = gl_VertexIndex;

    // Number of segments and height levels
    const uint NUM_SEGMENTS = 7;
    const float baseWidth = 0.02;

    // Calculate which height level this vertex is at
    uint segmentIndex = vi / 2;  // 0-7
    bool isRightSide = (vi % 2) == 1;

    // Calculate t (position along blade, 0 = base, 1 = tip)
    float t = float(segmentIndex) / float(NUM_SEGMENTS);

    // Width tapers from base to tip (90% taper)
    float widthAtT = baseWidth * (1.0 - t * 0.9);

    // For the last vertex (tip), width is 0
    if (vi == 14) {
        widthAtT = 0.0;
        t = 1.0;
    }

    // Offset left or right from center
    float xOffset = isRightSide ? widthAtT : -widthAtT;

    // Get position on bezier curve and offset by width
    vec3 curvePos = bezier(p0, p1, p2, t);
    vec3 localPos = curvePos + vec3(xOffset, 0.0, 0.0);

    // Rotate by facing angle around Y axis
    float cs = cos(facing);
    float sn = sin(facing);

    // Blade's right vector (perpendicular to facing direction in XZ plane)
    vec3 bladeRight = vec3(cs, 0.0, sn);

    // View-facing thickening: widen blade when viewed edge-on
    // Calculate view direction to blade base
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - basePos);

    // How much we're viewing edge-on (0 = face-on, 1 = edge-on)
    float edgeFactor = abs(dot(viewDir, bladeRight));

    // Thicken by up to 3x when viewed edge-on
    float thickenAmount = 1.0 + edgeFactor * 2.0;
    vec3 thickenedPos = vec3(localPos.x * thickenAmount, localPos.y, localPos.z);

    vec3 rotatedPos;
    rotatedPos.x = thickenedPos.x * cs - thickenedPos.z * sn;
    rotatedPos.y = thickenedPos.y;
    rotatedPos.z = thickenedPos.x * sn + thickenedPos.z * cs;

    // Final world position
    vec3 worldPos = basePos + rotatedPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Calculate normal (perpendicular to blade surface)
    vec3 tangent = normalize(bezierDerivative(p0, p1, p2, t));
    vec3 normal = normalize(cross(tangent, bladeRight));

    // Color gradient: darker at base, lighter at tip
    vec3 baseColor = vec3(0.08, 0.22, 0.04);
    vec3 tipColor = vec3(0.35, 0.65, 0.18);
    fragColor = mix(baseColor, tipColor, t);

    fragNormal = normal;
    fragHeight = t;
    fragClumpId = clumpId;
    fragWorldPos = worldPos;
}
