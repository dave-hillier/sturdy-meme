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

// Branch instance data - matches compute shader output
struct BranchInstance {
    vec4 basePosition;      // xyz = base position, w = base radius
    vec4 tipPosition;       // xyz = tip position, w = tip radius
    vec4 controlPoint1;     // xyz = bezier control point 1, w = hash
    vec4 controlPoint2;     // xyz = bezier control point 2, w = unused
    uvec4 metadata;         // x = parent index, y = depth, z = tree index, w = flags
};

layout(std430, binding = 1) readonly buffer BranchBuffer {
    BranchInstance branches[];
};

// Wind uniform buffer
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
layout(location = 3) out float fragDepth;

// Perlin noise implementation
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

// Cubic bezier evaluation
vec3 evaluateBezier(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
}

// Cubic bezier derivative
vec3 evaluateBezierDerivative(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return 3.0*u*u*(p1-p0) + 6.0*u*t*(p2-p1) + 3.0*t*t*(p3-p2);
}

void main() {
    // Decode vertex index
    // 32 vertices per branch: 8 segments * 4 vertices per ring
    uint branchIndex = gl_InstanceIndex;
    uint vertIndex = gl_VertexIndex;

    // Get branch data
    BranchInstance branch = branches[branchIndex];

    vec3 basePos = branch.basePosition.xyz;
    float baseRadius = branch.basePosition.w;
    vec3 tipPos = branch.tipPosition.xyz;
    float tipRadius = branch.tipPosition.w;
    vec3 ctrl1 = branch.controlPoint1.xyz;
    float branchHash = branch.controlPoint1.w;
    vec3 ctrl2 = branch.controlPoint2.xyz;
    uint depth = branch.metadata.y;

    // Branch segments and rings
    const uint NUM_SEGMENTS = 8;
    const uint VERTS_PER_RING = 4;

    uint segmentIndex = vertIndex / VERTS_PER_RING;
    uint ringIndex = vertIndex % VERTS_PER_RING;

    // Position along branch (0 = base, 1 = tip)
    float t = float(segmentIndex) / float(NUM_SEGMENTS - 1);

    // Get position on bezier curve
    vec3 curvePos = evaluateBezier(basePos, ctrl1, ctrl2, tipPos, t);
    vec3 tangent = normalize(evaluateBezierDerivative(basePos, ctrl1, ctrl2, tipPos, t));

    // Interpolate radius
    float radius = mix(baseRadius, tipRadius, t);

    // Build coordinate frame around tangent
    vec3 up = abs(tangent.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, tangent));
    vec3 forward = normalize(cross(tangent, right));

    // Radial position
    float angle = float(ringIndex) / float(VERTS_PER_RING) * 6.28318;
    vec3 radialDir = right * cos(angle) + forward * sin(angle);

    vec3 localPos = curvePos + radialDir * radius;

    // Apply wind animation
    float windSample = sampleWind(basePos.xz);
    vec2 windDir = wind.windDirectionAndStrength.xy;

    // Stiffness based on branch thickness (thicker = stiffer)
    float stiffness = baseRadius / 0.3;  // Normalize against typical trunk radius
    float sway = windSample * (1.0 - stiffness * 0.5) * 0.15;

    // Movement increases along branch (tip moves more)
    sway *= t * t;

    // Deeper branches sway more
    sway *= 1.0 + float(depth) * 0.3;

    vec3 windOffset = vec3(windDir.x, 0.0, windDir.y) * sway;

    // Add perpendicular oscillation
    float perpPhase = push.time * 2.0 + branchHash * 6.28318;
    vec3 perpDir = vec3(-windDir.y, 0.0, windDir.x);
    windOffset += perpDir * sin(perpPhase) * sway * 0.3;

    vec3 worldPos = localPos + windOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Output
    fragWorldPos = worldPos;
    fragNormal = radialDir;  // Normal points outward from branch center
    fragUV = vec2(angle / 6.28318, t);  // U = around branch, V = along branch
    fragDepth = -(ubo.view * vec4(worldPos, 1.0)).z;
}
