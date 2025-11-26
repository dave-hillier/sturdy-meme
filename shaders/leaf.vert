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
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;                       // Julian day for sidereal rotation
} ubo;

// Leaf particle states
const uint STATE_INACTIVE = 0;
const uint STATE_FALLING = 1;
const uint STATE_GROUNDED = 2;
const uint STATE_DISTURBED = 3;

// Flag bits
const uint FLAG_ACTIVE = 1u;
const uint FLAG_VISIBLE = 2u;

// Leaf particle structure (must match CPU and compute shader)
struct LeafParticle {
    vec3 position;
    uint state;
    vec3 velocity;
    float groundTime;
    vec4 orientation;       // quaternion
    vec3 angularVelocity;
    float size;
    float hash;
    uint leafType;
    uint flags;
    float padding;
};

layout(std430, binding = 1) readonly buffer ParticleBuffer {
    LeafParticle particles[];
};

// Wind uniforms
layout(binding = 2) uniform WindUniforms {
    vec4 windDirectionAndStrength;
    vec4 windParams;
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    float deltaTime;
    int padding[2];
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) flat out uint fragLeafType;
layout(location = 4) flat out uint fragState;
layout(location = 5) out float fragDistToCamera;

// Rotate vector by quaternion
vec3 rotateByQuat(vec3 v, vec4 q) {
    vec3 qvec = q.xyz;
    float qw = q.w;
    return v + 2.0 * cross(qvec, cross(qvec, v) + qw * v);
}

void main() {
    // gl_InstanceIndex = which particle
    // gl_VertexIndex = which vertex of the quad (0-3)
    LeafParticle p = particles[gl_InstanceIndex];

    // Check if particle is active and visible
    if ((p.flags & (FLAG_ACTIVE | FLAG_VISIBLE)) != (FLAG_ACTIVE | FLAG_VISIBLE)) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);  // Behind camera, culled
        fragNormal = vec3(0.0, 1.0, 0.0);
        return;
    }

    // Quad vertex positions (triangle strip: 0,1,2,3 -> BL, BR, TL, TR)
    int vertexId = gl_VertexIndex;
    vec2 quadOffset;

    if (vertexId == 0) quadOffset = vec2(-0.5, -0.5);      // Bottom-left
    else if (vertexId == 1) quadOffset = vec2(0.5, -0.5); // Bottom-right
    else if (vertexId == 2) quadOffset = vec2(-0.5, 0.5); // Top-left
    else quadOffset = vec2(0.5, 0.5);                     // Top-right

    // Scale by leaf size
    vec3 localPos = vec3(quadOffset.x, 0.0, quadOffset.y) * p.size;

    // Local normal (before rotation, leaf lies flat in XZ plane)
    vec3 localNormal = vec3(0.0, 1.0, 0.0);

    // Rotate by orientation quaternion
    vec3 rotatedPos = rotateByQuat(localPos, p.orientation);
    vec3 rotatedNormal = rotateByQuat(localNormal, p.orientation);

    // World position
    vec3 worldPos = p.position + rotatedPos;

    // Output data
    fragWorldPos = worldPos;
    fragNormal = normalize(rotatedNormal);
    fragUV = quadOffset + 0.5;  // 0-1 UV range
    fragLeafType = p.leafType;
    fragState = p.state;

    // Distance to camera for fading
    vec3 cameraPos = ubo.cameraPosition.xyz;
    fragDistToCamera = length(worldPos - cameraPos);

    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
