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

    // Wind animation
    float windStrength = 0.2;
    float windFreq = 2.5;
    float windPhase = bladeHash * 6.28318;
    float windOffset = sin(push.time * windFreq + windPhase + basePos.x * 0.3 + basePos.z * 0.2) * windStrength;

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5, height * 0.5, 0.0);  // Mid control
    vec3 p2 = vec3(windOffset + tilt, height, 0.0);  // Tip

    // Blade geometry: 2 quads + 1 tip triangle = 15 vertices
    // Quad 0: vertices 0-5 (t=0.0 to t=0.5)
    // Quad 1: vertices 6-11 (t=0.5 to t=1.0)
    // Tip: vertices 12-14 (triangle at t=1.0)

    // Vertex index within blade
    uint vi = gl_VertexIndex;

    // Determine which segment and which vertex in that segment
    float t;
    float xOffset;
    float baseWidth = 0.02;

    if (vi < 12) {
        // Quads (2 triangles each = 6 vertices per quad)
        uint quadIndex = vi / 6;      // 0 or 1
        uint vertInQuad = vi % 6;     // 0-5

        // Height levels for this quad
        float t0 = float(quadIndex) * 0.5;       // 0.0 or 0.5
        float t1 = float(quadIndex + 1) * 0.5;   // 0.5 or 1.0

        // Width at each height level
        float w0 = baseWidth * (1.0 - t0 * 0.9);
        float w1 = baseWidth * (1.0 - t1 * 0.9);

        // Two triangles per quad:
        // Triangle 1 (verts 0,1,2): bottom-left, bottom-right, top-right
        // Triangle 2 (verts 3,4,5): bottom-left, top-right, top-left

        if (vertInQuad == 0) {
            t = t0; xOffset = -w0;  // bottom-left
        } else if (vertInQuad == 1) {
            t = t0; xOffset = w0;   // bottom-right
        } else if (vertInQuad == 2) {
            t = t1; xOffset = w1;   // top-right
        } else if (vertInQuad == 3) {
            t = t0; xOffset = -w0;  // bottom-left
        } else if (vertInQuad == 4) {
            t = t1; xOffset = w1;   // top-right
        } else {
            t = t1; xOffset = -w1;  // top-left
        }
    } else {
        // Tip triangle (vertices 12, 13, 14)
        uint tipVert = vi - 12;
        float w = baseWidth * 0.1;  // Very narrow at tip base

        if (tipVert == 0) {
            t = 1.0; xOffset = -w;   // tip-left
        } else if (tipVert == 1) {
            t = 1.0; xOffset = w;    // tip-right
        } else {
            t = 1.0; xOffset = 0.0;  // tip center (actual tip point)
        }
    }

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
}
