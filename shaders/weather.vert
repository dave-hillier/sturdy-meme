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
    float julianDay;
    float antiAliasStrength;   // Strength for cloud aliasing suppression
} ubo;

// Weather particle structure (must match CPU and compute shader)
struct WeatherParticle {
    vec3 position;
    float lifetime;
    vec3 velocity;
    float size;
    float rotation;
    float hash;
    uint type;      // 0 = rain, 1 = snow
    uint flags;
};

layout(std430, binding = 1) readonly buffer ParticleBuffer {
    WeatherParticle particles[];
};

layout(push_constant) uniform PushConstants {
    float time;
    float deltaTime;
    int cascadeIndex;
    int padding;
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragAlpha;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) flat out uint fragType;

void main() {
    // gl_InstanceIndex = which particle
    // gl_VertexIndex = which vertex of the quad (0-3)
    WeatherParticle p = particles[gl_InstanceIndex];

    // Check if particle is active (bit 0) and visible (bit 1)
    if ((p.flags & 3u) != 3u) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);  // Behind camera, culled
        fragAlpha = 0.0;
        return;
    }

    vec3 worldPos = p.position;
    vec3 velocity = p.velocity;

    // Calculate camera vectors
    vec3 cameraPos = ubo.cameraPosition.xyz;
    vec3 viewDir = normalize(worldPos - cameraPos);

    // Quad vertex positions (triangle strip: 0,1,2,3 -> BL, BR, TL, TR)
    int vertexId = gl_VertexIndex;
    vec2 quadOffset;

    if (vertexId == 0) quadOffset = vec2(-0.5, -0.5);      // Bottom-left
    else if (vertexId == 1) quadOffset = vec2(0.5, -0.5); // Bottom-right
    else if (vertexId == 2) quadOffset = vec2(-0.5, 0.5); // Top-left
    else quadOffset = vec2(0.5, 0.5);                     // Top-right

    // Different geometry for rain vs snow
    vec3 offset;

    if (p.type == 0u) {
        // RAIN: Velocity-aligned stretched billboard
        float speed = length(velocity);
        vec3 velDir = speed > 0.01 ? normalize(velocity) : vec3(0.0, -1.0, 0.0);

        // Rain streak length based on velocity (20-100mm)
        float streakLength = p.size * 40.0 + speed * 0.005;
        float streakWidth = p.size;

        // Create billboard aligned to velocity direction
        // Right vector is perpendicular to velocity and view direction
        vec3 right = normalize(cross(velDir, viewDir));
        if (length(right) < 0.01) {
            right = normalize(cross(velDir, vec3(1.0, 0.0, 0.0)));
        }

        // Offset in local space, stretched along velocity
        offset = right * quadOffset.x * streakWidth +
                 velDir * quadOffset.y * streakLength;

        // Alpha based on velocity (faster = more visible due to motion blur effect)
        fragAlpha = 0.3 + speed * 0.02;
        fragAlpha = clamp(fragAlpha, 0.2, 0.7);

    } else {
        // SNOW: Camera-facing billboard with tumble
        // Camera right and up vectors
        vec3 camRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
        vec3 camUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);

        // Apply rotation for tumbling effect
        float c = cos(p.rotation);
        float s = sin(p.rotation);
        vec2 rotatedOffset = vec2(
            quadOffset.x * c - quadOffset.y * s,
            quadOffset.x * s + quadOffset.y * c
        );

        offset = (camRight * rotatedOffset.x + camUp * rotatedOffset.y) * p.size;

        // Snow alpha - softer, more opaque
        fragAlpha = 0.5 + p.hash * 0.2;
    }

    // Final world position
    vec3 finalPos = worldPos + offset;
    fragWorldPos = finalPos;
    fragType = p.type;

    // UV coordinates
    fragUV = quadOffset + 0.5;

    // Distance-based fade
    float dist = length(finalPos - cameraPos);
    float distanceFade = 1.0 - smoothstep(80.0, 100.0, dist);
    fragAlpha *= distanceFade;

    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * vec4(finalPos, 1.0);
}
