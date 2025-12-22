#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Per-instance data for impostor billboards
layout(location = 0) in vec3 inPosition;   // Billboard quad vertex position
layout(location = 1) in vec2 inTexCoord;   // Billboard quad UV

// Instance data (passed via instance attributes or SSBO)
layout(location = 2) in vec3 instancePos;      // World position of tree
layout(location = 3) in float instanceScale;   // Tree scale
layout(location = 4) in float instanceRotation; // Y-axis rotation
layout(location = 5) in uint instanceArchetype; // Archetype index for atlas lookup
layout(location = 6) in float instanceBlendFactor; // LOD blend factor (0=full geo, 1=impostor)

layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position
    vec4 lodParams;         // x = blend factor (0 = full geo, 1 = impostor), y = brightness, z = normal strength
    vec4 atlasParams;       // x = hSize (horizontal half-size), y = vSize (vertical half-size), z = baseOffset
} push;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out float fragBlendFactor;
layout(location = 3) flat out int fragCellIndex;
layout(location = 4) out mat3 fragImpostorToWorld;  // Rotation from impostor space to world

// Atlas layout constants
const int CELLS_PER_ROW = 9;
const int HORIZONTAL_ANGLES = 8;
const float ANGLE_STEP = 360.0 / float(HORIZONTAL_ANGLES);  // 45 degrees

void main() {
    // Get tree instance data
    vec3 treePos = instancePos;
    float scale = instanceScale;
    float rotation = instanceRotation;

    // Compute view direction FROM tree TO camera (horizontal only for angle selection)
    // This must match the capture convention where azimuth=0 means camera on +Z axis
    vec3 toCamera = push.cameraPos.xyz - treePos;
    vec2 toCameraHorizontal = normalize(toCamera.xz);

    // Compute horizontal angle (0-360 degrees)
    // atan(x, z) gives angle where +Z is 0°, +X is 90°
    // Negate so that moving counterclockwise around tree decreases cell index
    float hAngle = -atan(toCameraHorizontal.x, toCameraHorizontal.y);
    hAngle = degrees(hAngle);
    if (hAngle < 0.0) hAngle += 360.0;

    // Apply tree rotation offset
    hAngle = mod(hAngle - degrees(rotation) + 360.0, 360.0);

    // Select horizontal cell index (0-7)
    int hIndex = int(mod(round(hAngle / ANGLE_STEP), float(HORIZONTAL_ANGLES)));

    // Compute elevation angle (how much camera is above tree)
    float dist = length(toCamera);
    float elevation = degrees(asin(clamp(toCamera.y / dist, -1.0, 1.0)));

    // Debug elevation override (lodParams.w >= -90 means override is enabled)
    if (push.lodParams.w >= -90.0) {
        elevation = push.lodParams.w;
    }

    // Select vertical level
    // Level 0: horizon (-22.5 to 22.5 degrees)
    // Level 1: elevated (22.5 to 67.5 degrees)
    // Level 2: top-down (> 67.5 degrees) - uses single cell
    int vIndex;
    int cellIndex;

    if (elevation > 67.5) {
        // Top-down view: use cell 8 of row 0
        cellIndex = 8;
        vIndex = 0;
    } else if (elevation > 22.5) {
        // Elevated view: row 1
        cellIndex = CELLS_PER_ROW + hIndex;
        vIndex = 1;
    } else {
        // Horizon view: row 0
        cellIndex = hIndex;
        vIndex = 0;
    }

    fragCellIndex = cellIndex;

    // Compute atlas UV offset for this cell
    int cellX = cellIndex % CELLS_PER_ROW;
    int cellY = cellIndex / CELLS_PER_ROW;

    // Transform quad UV to atlas UV (no rotation needed - billboard orientation handles it)
    vec2 cellUV = inTexCoord;
    vec2 atlasUV = (vec2(cellX, cellY) + cellUV) / vec2(float(CELLS_PER_ROW), 2.0);
    fragTexCoord = atlasUV;

    // Billboard orientation depends on view angle
    vec3 forward, up, right;
    float hSize = push.atlasParams.x * scale;
    float vSize = push.atlasParams.y * scale;
    float baseOffset = push.atlasParams.z * scale;
    vec3 localPos;
    vec3 billboardCenter;

    if (elevation > 67.5) {
        // Top-down view: billboard lies flat, fixed orientation based on tree rotation only
        forward = vec3(0.0, 1.0, 0.0);  // Billboard faces up
        // Fixed orientation based on tree rotation (doesn't change with camera angle)
        right = vec3(cos(rotation), 0.0, -sin(rotation));
        up = vec3(sin(rotation), 0.0, cos(rotation));

        // For top-down, use same size in both directions (it's a square from above)
        float maxSize = max(hSize, vSize);
        // Billboard is centered on the tree at canopy height
        // inPosition.x is -0.5 to 0.5, inPosition.y is 0 to 1
        // Remap Y to also be -0.5 to 0.5 for centered quad
        localPos = right * inPosition.x * maxSize * 2.0 +
                   up * (inPosition.y - 0.5) * maxSize * 2.0;
        // Center the billboard at tree center height (half the tree height)
        billboardCenter = treePos + vec3(0.0, vSize, 0.0);
    } else {
        // Normal billboard: face camera but stay upright
        // Forward points toward camera (opposite of toCamera horizontal direction)
        forward = -normalize(vec3(toCamera.x, 0.0, toCamera.z));
        up = vec3(0.0, 1.0, 0.0);
        right = cross(up, forward);

        // For elevated views (row 1, captured at 45 degrees), tilt billboard 45 degrees
        // Tilt top of billboard away from camera so face tilts up toward camera
        if (elevation > 22.5) {
            float tiltAngle = radians(45.0);  // Fixed 45 degrees to match capture angle
            vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
            vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
            forward = tiltedForward;
            up = tiltedUp;
            right = cross(up, forward);
        }

        // inPosition.x is -0.5 to 0.5, inPosition.y is 0 to 1
        // Billboard width = 2*hSize, height = 2*vSize
        // Base of billboard (y=0) should be at tree base
        localPos = right * inPosition.x * hSize * 2.0 +
                   up * inPosition.y * vSize * 2.0;
        billboardCenter = treePos + vec3(0.0, baseOffset, 0.0);
    }

    // Build impostor-to-world rotation matrix for normal transformation
    fragImpostorToWorld = mat3(right, up, forward);

    vec3 worldPos = billboardCenter + localPos;
    fragWorldPos = worldPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    fragBlendFactor = instanceBlendFactor;
}
