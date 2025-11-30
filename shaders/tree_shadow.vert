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

// Branch instance data
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
layout(binding = 2) uniform WindUniforms {
    vec4 windDirectionAndStrength;
    vec4 windParams;
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;
    float padding[2];
} push;

// Simple perlin-like noise
float hash(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(dot(i, vec2(127.1, 311.7)));
    float b = hash(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7)));
    float c = hash(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7)));
    float d = hash(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7)));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windTime = wind.windParams.w;

    vec2 scrolledPos = worldPos - windDir * windTime * 1.0;
    float n = noise(scrolledPos * 0.1);
    return n * windStrength;
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
    uint branchIndex = gl_InstanceIndex;
    uint vertIndex = gl_VertexIndex;

    BranchInstance branch = branches[branchIndex];

    vec3 basePos = branch.basePosition.xyz;
    float baseRadius = branch.basePosition.w;
    vec3 tipPos = branch.tipPosition.xyz;
    float tipRadius = branch.tipPosition.w;
    vec3 ctrl1 = branch.controlPoint1.xyz;
    float branchHash = branch.controlPoint1.w;
    vec3 ctrl2 = branch.controlPoint2.xyz;
    uint depth = branch.metadata.y;

    const uint NUM_SEGMENTS = 8;
    const uint VERTS_PER_RING = 4;

    uint segmentIndex = vertIndex / VERTS_PER_RING;
    uint ringIndex = vertIndex % VERTS_PER_RING;

    float t = float(segmentIndex) / float(NUM_SEGMENTS - 1);

    vec3 curvePos = evaluateBezier(basePos, ctrl1, ctrl2, tipPos, t);
    vec3 tangent = normalize(evaluateBezierDerivative(basePos, ctrl1, ctrl2, tipPos, t));

    float radius = mix(baseRadius, tipRadius, t);

    vec3 up = abs(tangent.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, tangent));
    vec3 forward = normalize(cross(tangent, right));

    float angle = float(ringIndex) / float(VERTS_PER_RING) * 6.28318;
    vec3 radialDir = right * cos(angle) + forward * sin(angle);

    vec3 localPos = curvePos + radialDir * radius;

    // Apply wind (same as main shader)
    float windSample = sampleWind(basePos.xz);
    vec2 windDir = wind.windDirectionAndStrength.xy;

    float stiffness = baseRadius / 0.3;
    float sway = windSample * (1.0 - stiffness * 0.5) * 0.15;
    sway *= t * t;
    sway *= 1.0 + float(depth) * 0.3;

    vec3 windOffset = vec3(windDir.x, 0.0, windDir.y) * sway;

    float perpPhase = push.time * 2.0 + branchHash * 6.28318;
    vec3 perpDir = vec3(-windDir.y, 0.0, windDir.x);
    windOffset += perpDir * sin(perpPhase) * sway * 0.3;

    vec3 worldPos = localPos + windOffset;

    // Use cascade matrix for shadow pass
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);
}
