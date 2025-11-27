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
    vec4 moonColor;                       // rgb = moon color
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;           // Julian day for sidereal rotation
    float antiAliasStrength;   // Strength for cloud aliasing suppression
} ubo;

layout(location = 0) out vec3 rayDir;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.9999, 1.0);

    // Reconstruct world-space ray direction from screen position
    //
    // Use inverse of projection to go from clip space to view space,
    // then inverse of view rotation to go to world space.

    // For perspective projection:
    //   proj[0][0] = 1 / (aspect * tan(fov/2))
    //   proj[1][1] = 1 / tan(fov/2)  (negated for Vulkan Y-flip)
    //
    // To unproject clip (x,y) to view space direction:
    //   view_x = clip_x / proj[0][0]
    //   view_y = clip_y / proj[1][1]
    //   view_z = -1 (looking down -Z)

    vec3 viewSpaceDir = vec3(
        pos.x / ubo.proj[0][0],
        pos.y / ubo.proj[1][1],  // proj[1][1] is negative, so this flips Y correctly
        -1.0
    );

    // Transform to world space using inverse view rotation
    mat3 invViewRotation = transpose(mat3(ubo.view));
    rayDir = invViewRotation * viewSpaceDir;
}
