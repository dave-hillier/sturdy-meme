#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    float timeOfDay;
} ubo;

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = unused
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

layout(push_constant) uniform PushConstants {
    float time;
} push;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragHeight;
layout(location = 3) out float fragClumpId;

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

    // Wind animation
    float windStrength = 0.2;
    float windFreq = 2.5;
    float windPhase = bladeHash * 6.28318;
    float windOffset = sin(push.time * windFreq + windPhase + basePos.x * 0.3 + basePos.z * 0.2) * windStrength;

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5, height * 0.5, 0.0);  // Mid control
    vec3 p2 = vec3(windOffset + tilt, height, 0.0);  // Tip

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
    vec3 rotatedPos;
    rotatedPos.x = localPos.x * cs - localPos.z * sn;
    rotatedPos.y = localPos.y;
    rotatedPos.z = localPos.x * sn + localPos.z * cs;

    // Final world position
    vec3 worldPos = basePos + rotatedPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Calculate normal (perpendicular to blade surface)
    vec3 tangent = normalize(bezierDerivative(p0, p1, p2, t));
    vec3 bladeRight = vec3(cs, 0.0, sn);
    vec3 normal = normalize(cross(tangent, bladeRight));

    // Color gradient: darker at base, lighter at tip
    vec3 baseColor = vec3(0.08, 0.22, 0.04);
    vec3 tipColor = vec3(0.35, 0.65, 0.18);
    fragColor = mix(baseColor, tipColor, t);

    fragNormal = normal;
    fragHeight = t;
    fragClumpId = clumpId;
}
