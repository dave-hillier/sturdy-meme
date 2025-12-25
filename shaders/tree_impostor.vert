#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "tree_impostor_instance.glsl"
#include "octahedral_mapping.glsl"

// Per-vertex data for billboard quad
layout(location = 0) in vec3 inPosition;   // Billboard quad vertex position
layout(location = 1) in vec2 inTexCoord;   // Billboard quad UV

// Visible impostor instances from compute shader (SSBO)
layout(std430, binding = BINDING_TREE_IMPOSTOR_INSTANCES) readonly buffer InstanceBuffer {
    ImpostorInstance instances[];
};

// Push constants
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position, w = autumnHueShift
    vec4 lodParams;         // x = useOctahedral, y = brightness, z = normal strength, w = debug elevation
} push;

// Outputs for legacy mode
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out float fragBlendFactor;
layout(location = 3) flat out int fragCellIndex;
layout(location = 4) out mat3 fragImpostorToWorld;
layout(location = 7) flat out uint fragArchetypeIndex;

// Outputs for octahedral mode (frame blending)
layout(location = 8) out vec2 fragOctaUV;           // Continuous octahedral UV
layout(location = 9) out vec3 fragViewDir;          // View direction for frame lookup
layout(location = 10) flat out int fragUseOctahedral;  // Mode flag
layout(location = 11) out vec2 fragLocalUV;         // Billboard local UV for frame sampling

// Legacy atlas layout constants
const int CELLS_PER_ROW = 9;
const int HORIZONTAL_ANGLES = 8;
const float ANGLE_STEP = 360.0 / float(HORIZONTAL_ANGLES);

void main() {
    // Get instance data from SSBO
    ImpostorInstance inst = instances[gl_InstanceIndex];

    vec3 treePos = inst.positionAndScale.xyz;
    float scale = inst.positionAndScale.w;
    float rotation = inst.rotationAndArchetype.x;
    uint archetypeIndex = uint(inst.rotationAndArchetype.y);
    float blendFactor = inst.rotationAndArchetype.z;
    float hSize = inst.sizeAndOffset.x;
    float vSize = inst.sizeAndOffset.y;
    float baseOffset = inst.sizeAndOffset.z;

    // Compute view direction FROM tree TO camera
    vec3 toCamera = push.cameraPos.xyz - treePos;
    float dist = length(toCamera);
    vec3 viewDir = toCamera / dist;

    // Apply tree rotation to view direction (rotate view around Y axis)
    float cosRot = cos(-rotation);
    float sinRot = sin(-rotation);
    vec3 rotatedViewDir = vec3(
        viewDir.x * cosRot - viewDir.z * sinRot,
        viewDir.y,
        viewDir.x * sinRot + viewDir.z * cosRot
    );

    // Check octahedral mode
    bool useOctahedral = push.lodParams.x > 0.5;
    fragUseOctahedral = useOctahedral ? 1 : 0;

    // Compute elevation angle
    float elevation = degrees(asin(clamp(viewDir.y, -1.0, 1.0)));

    // Debug elevation override
    if (push.lodParams.w >= -90.0) {
        elevation = push.lodParams.w;
        // Reconstruct view direction from debug elevation
        float debugElevRad = radians(elevation);
        viewDir.y = sin(debugElevRad);
        float horizScale = cos(debugElevRad);
        viewDir.x *= horizScale / max(length(vec2(viewDir.x, viewDir.z)), 0.001);
        viewDir.z *= horizScale / max(length(vec2(viewDir.x, viewDir.z)), 0.001);
    }

    // Billboard orientation
    vec3 forward, up, right;
    vec3 localPos;
    vec3 billboardCenter;

    if (elevation > 67.5) {
        // Top-down view: billboard lies flat
        forward = vec3(0.0, 1.0, 0.0);
        right = vec3(cos(rotation), 0.0, -sin(rotation));
        up = vec3(sin(rotation), 0.0, cos(rotation));

        float maxSize = max(hSize, vSize);
        localPos = right * inPosition.x * maxSize * 2.0 +
                   up * (inPosition.y - 0.5) * maxSize * 2.0;
        billboardCenter = treePos + vec3(0.0, vSize, 0.0);
    } else {
        // Normal billboard: face camera but stay upright
        forward = -normalize(vec3(toCamera.x, 0.0, toCamera.z));
        up = vec3(0.0, 1.0, 0.0);
        right = cross(up, forward);

        // Tilt for elevated views
        if (elevation > 22.5 && !useOctahedral) {
            // Legacy mode: tilt at fixed 45 degrees for elevated row
            float tiltAngle = radians(45.0);
            vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
            vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
            forward = tiltedForward;
            up = tiltedUp;
            right = cross(up, forward);
        } else if (useOctahedral && elevation > 5.0) {
            // Octahedral mode: tilt to match capture angle more smoothly
            float tiltAngle = radians(clamp(elevation, 0.0, 80.0));
            vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
            vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
            forward = tiltedForward;
            up = tiltedUp;
            right = cross(up, forward);
        }

        localPos = right * inPosition.x * hSize * 2.0 +
                   up * inPosition.y * vSize * 2.0;
        billboardCenter = treePos + vec3(0.0, baseOffset, 0.0);
    }

    // Build impostor-to-world rotation matrix
    fragImpostorToWorld = mat3(right, up, forward);

    vec3 worldPos = billboardCenter + localPos;
    fragWorldPos = worldPos;
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    fragBlendFactor = blendFactor;
    fragArchetypeIndex = archetypeIndex;
    fragViewDir = rotatedViewDir;

    // Pass local UV for frame blending
    fragLocalUV = inTexCoord;

    if (useOctahedral) {
        // Octahedral mode: compute which cell we're viewing from
        vec2 octaUV = hemiOctaEncode(rotatedViewDir);
        fragOctaUV = octaUV;

        // Find the cell in the grid
        float gridSize = float(OCTA_GRID_SIZE);
        ivec2 cell = ivec2(floor(octaUV * gridSize));
        cell = clamp(cell, ivec2(0), ivec2(OCTA_GRID_SIZE - 1));

        // Map billboard's local UV (inTexCoord) to the cell position in atlas
        // Each cell spans 1/gridSize of the atlas
        vec2 atlasUV = (vec2(cell) + inTexCoord) / gridSize;
        fragTexCoord = atlasUV;

        fragCellIndex = cell.y * OCTA_GRID_SIZE + cell.x;
    } else {
        // Legacy mode: discrete cell selection
        vec2 toCameraHorizontal = normalize(toCamera.xz);
        float hAngle = atan(toCameraHorizontal.x, toCameraHorizontal.y);
        hAngle = degrees(hAngle);
        if (hAngle < 0.0) hAngle += 360.0;
        hAngle = mod(hAngle + degrees(rotation) + 360.0, 360.0);

        int hIndex = int(mod(round(hAngle / ANGLE_STEP), float(HORIZONTAL_ANGLES)));

        int vIndex;
        int cellIndex;

        if (elevation > 67.5) {
            cellIndex = 8;
            vIndex = 0;
        } else if (elevation > 22.5) {
            cellIndex = CELLS_PER_ROW + hIndex;
            vIndex = 1;
        } else {
            cellIndex = hIndex;
            vIndex = 0;
        }

        fragCellIndex = cellIndex;

        int cellX = cellIndex % CELLS_PER_ROW;
        int cellY = cellIndex / CELLS_PER_ROW;
        vec2 cellUV = inTexCoord;
        vec2 atlasUV = (vec2(cellX, cellY) + cellUV) / vec2(float(CELLS_PER_ROW), 2.0);
        fragTexCoord = atlasUV;
        fragOctaUV = vec2(0.0);
    }
}
