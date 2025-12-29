#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "tree_impostor_instance.glsl"
#include "octahedral_mapping.glsl"

// Per-vertex data for impostor billboards
layout(location = 0) in vec3 inPosition;   // Billboard quad vertex position
layout(location = 1) in vec2 inTexCoord;   // Billboard quad UV

// Visible impostor instances from compute shader (SSBO)
layout(std430, binding = BINDING_TREE_IMPOSTOR_SHADOW_INSTANCES) readonly buffer InstanceBuffer {
    ImpostorInstance instances[];
};

// Push constants - must match layout from TreeLODSystem
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position (unused for shadow)
    vec4 lodParams;         // unused for shadow but needed for layout consistency
    int cascadeIndex;       // Which shadow cascade we're rendering
} push;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragArchetypeIndex;

void main() {
    // Get instance data from SSBO
    ImpostorInstance inst = instances[gl_InstanceIndex];

    vec3 treePos = inst.positionAndScale.xyz;
    float scale = inst.positionAndScale.w;
    float rotation = inst.rotationAndArchetype.x;
    uint archetypeIndex = uint(inst.rotationAndArchetype.y);
    float hSize = inst.sizeAndOffset.x;
    float vSize = inst.sizeAndOffset.y;
    float baseOffset = inst.sizeAndOffset.z;

    // For shadows, orient billboard to face the sun (not the camera)
    // This gives a full shadow profile instead of a thin edge shadow
    vec3 sunDir = normalize(ubo.toSunDirection.xyz);  // Points toward sun

    // Apply tree rotation to view direction (rotate view around Y axis)
    float cosRot = cos(-rotation);
    float sinRot = sin(-rotation);
    vec3 rotatedSunDir = vec3(
        sunDir.x * cosRot - sunDir.z * sinRot,
        sunDir.y,
        sunDir.x * sinRot + sunDir.z * cosRot
    );

    // Compute sun elevation angle
    float sunElevation = degrees(asin(clamp(sunDir.y, -1.0, 1.0)));

    // Octahedral atlas UV lookup - compute which cell based on sun direction
    vec2 octaUV = hemiOctaEncode(rotatedSunDir);
    float gridSize = float(OCTA_GRID_SIZE);
    ivec2 cell = ivec2(floor(octaUV * gridSize));
    cell = clamp(cell, ivec2(0), ivec2(OCTA_GRID_SIZE - 1));

    // Map billboard's local UV to the cell position in atlas
    vec2 atlasUV = (vec2(cell) + inTexCoord) / gridSize;
    fragTexCoord = atlasUV;

    // Billboard orientation: face toward sun to cast full shadow
    // forward points FROM sun TO tree (so billboard front faces the sun)
    vec3 forward = -normalize(vec3(sunDir.x, 0.0, sunDir.z));
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, forward);

    // Handle top-down view (sun nearly overhead)
    if (sunElevation > 67.5) {
        forward = vec3(0.0, 1.0, 0.0);
        right = vec3(cos(rotation), 0.0, -sin(rotation));
        up = vec3(sin(rotation), 0.0, cos(rotation));
    } else if (sunElevation > 5.0) {
        // Tilt billboard to match capture angle for elevated sun
        float tiltAngle = radians(clamp(sunElevation, 0.0, 80.0));
        vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
        vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
        forward = tiltedForward;
        up = tiltedUp;
        right = cross(up, forward);
    }

    // Position billboard vertex - center Y around origin so baseOffset positions correctly.
    // The atlas captures with a square ortho projection using max(hSize, vSize),
    // so we must use the same size for both dimensions to match the texture content.
    float billboardSize = max(hSize, vSize);
    vec3 localPos = right * inPosition.x * billboardSize * 2.0 +
                    up * (inPosition.y - 0.5) * billboardSize * 2.0;

    // Position billboard centered at tree's center height
    vec3 worldPos = treePos + vec3(0.0, baseOffset, 0.0) + localPos;

    // Transform by cascade light matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    fragArchetypeIndex = archetypeIndex;
}
