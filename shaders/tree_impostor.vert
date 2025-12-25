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

// Simplified push constants - instance data comes from SSBO
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position, w = autumnHueShift
    vec4 lodParams;         // x = useOctahedral (0 or 1), y = brightness, z = normal strength, w = debug elevation
} push;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out float fragBlendFactor;
layout(location = 3) flat out int fragCellIndex;
layout(location = 4) out mat3 fragImpostorToWorld;  // Rotation from impostor space to world
layout(location = 7) flat out uint fragArchetypeIndex;

// Atlas layout constants
const int CELLS_PER_ROW = 9;
const int HORIZONTAL_ANGLES = 8;
const float ANGLE_STEP = 360.0 / float(HORIZONTAL_ANGLES);  // 45 degrees

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

    // Check if using octahedral mapping (Phase 6)
    bool useOctahedral = push.lodParams.x > 0.5;

    // Compute elevation angle (how much camera is above tree)
    float elevation = degrees(asin(clamp(viewDir.y, -1.0, 1.0)));

    // Debug elevation override (lodParams.w >= -90 means override is enabled)
    if (push.lodParams.w >= -90.0) {
        elevation = push.lodParams.w;
        // Recalculate viewDir for debug elevation
        float debugElevRad = radians(elevation);
        vec2 horizDir = normalize(toCamera.xz);
        viewDir = vec3(horizDir.x * cos(debugElevRad), sin(debugElevRad), horizDir.y * cos(debugElevRad));
    }

    vec2 atlasUV;
    int cellIndex = 0;

    if (useOctahedral) {
        // === OCTAHEDRAL MAPPING (Phase 6) ===
        // Convert view direction to impostor space (apply tree rotation)
        vec3 impostorDir = viewToImpostorSpace(viewDir, rotation);

        // Compute octahedral UV (center of the rendered cell for this view)
        vec2 centerUV = octahedralEncode(impostorDir);

        // Each view was rendered to a 64x64 region in the 512x512 atlas
        // The cell scale maps billboard UV [0,1] to the 64-pixel cell extent
        const float CELL_SCALE = 64.0 / 512.0;  // 0.125

        // The billboard quad has:
        // - inPosition.x in [-0.5, 0.5], scaled by hSize*2 for world position
        // - inPosition.y in [0, 1], scaled by vSize*2 for world position
        // - inTexCoord.x in [0, 1], inTexCoord.y in [1, 0] (y inverted)

        // Map billboard UV to cell UV offset
        // Center of billboard (inTexCoord = 0.5, 0.5) should sample center of cell
        vec2 uvOffset = inTexCoord - vec2(0.5);

        // Account for aspect ratio: the billboard has dimensions 2*hSize x 2*vSize
        // but the atlas cell is square (64x64). We need to scale the offset so that
        // the billboard's extent maps to the correct portion of the cell.
        // If the tree is taller than wide (vSize > hSize), the horizontal extent
        // uses less of the cell width.
        float aspectRatio = hSize / max(vSize, 0.001);
        if (aspectRatio < 1.0) {
            // Tree is taller than wide - horizontal content is centered
            uvOffset.x *= aspectRatio;
        } else {
            // Tree is wider than tall - vertical content is centered
            uvOffset.y /= aspectRatio;
        }

        atlasUV = centerUV + uvOffset * CELL_SCALE;

        // Clamp to valid atlas range
        atlasUV = clamp(atlasUV, vec2(0.0), vec2(1.0));

        fragTexCoord = atlasUV;
        fragCellIndex = -1;  // Not using cell index for octahedral
    } else {
        // === LEGACY 17-VIEW MAPPING ===
        vec2 toCameraHorizontal = normalize(toCamera.xz);

        // Compute horizontal angle (0-360 degrees)
        float hAngle = atan(toCameraHorizontal.x, toCameraHorizontal.y);
        hAngle = degrees(hAngle);
        if (hAngle < 0.0) hAngle += 360.0;

        // Apply tree rotation offset
        hAngle = mod(hAngle + degrees(rotation) + 360.0, 360.0);

        // Select horizontal cell index (0-7)
        int hIndex = int(mod(round(hAngle / ANGLE_STEP), float(HORIZONTAL_ANGLES)));

        // Select vertical level
        if (elevation > 67.5) {
            cellIndex = 8;  // Top-down view
        } else if (elevation > 22.5) {
            cellIndex = CELLS_PER_ROW + hIndex;  // Elevated view
        } else {
            cellIndex = hIndex;  // Horizon view
        }

        fragCellIndex = cellIndex;

        // Compute atlas UV offset for this cell
        int cellX = cellIndex % CELLS_PER_ROW;
        int cellY = cellIndex / CELLS_PER_ROW;

        vec2 cellUV = inTexCoord;
        atlasUV = (vec2(cellX, cellY) + cellUV) / vec2(float(CELLS_PER_ROW), 2.0);
        fragTexCoord = atlasUV;
    }

    // Billboard orientation depends on view angle
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

        // For octahedral mode, use smooth tilt based on elevation
        // For legacy mode, use discrete tilt for elevated views
        if (useOctahedral) {
            // Smooth tilt interpolation based on elevation
            float tiltAmount = smoothstep(15.0, 60.0, elevation);
            float tiltAngle = tiltAmount * radians(45.0);

            vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
            vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
            forward = tiltedForward;
            up = tiltedUp;
            right = cross(up, forward);
        } else if (elevation > 22.5) {
            // Legacy discrete 45 degree tilt
            float tiltAngle = radians(45.0);
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

    // Build impostor-to-world rotation matrix for normal transformation
    fragImpostorToWorld = mat3(right, up, forward);

    vec3 worldPos = billboardCenter + localPos;
    fragWorldPos = worldPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    fragBlendFactor = blendFactor;
    fragArchetypeIndex = archetypeIndex;
}
