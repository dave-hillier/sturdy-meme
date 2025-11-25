#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    float timeOfDay;
    float shadowMapSize;
} ubo;

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

layout(push_constant) uniform PushConstants {
    float time;
} push;

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

    // Simplified wind animation (same phase as main shader for consistent shadows)
    float windStrength = 0.2;
    float windFreq = 2.5;
    float windPhase = bladeHash * 6.28318;
    float windOffset = sin(push.time * windFreq + windPhase + basePos.x * 0.3 + basePos.z * 0.2) * windStrength;

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

    // Transform to light space for shadow map
    gl_Position = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);

    // Pass height and hash for dithering in fragment shader
    fragHeight = t;
    fragHash = bladeHash;
}
