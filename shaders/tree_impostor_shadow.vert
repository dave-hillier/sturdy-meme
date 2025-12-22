#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Per-vertex data for impostor billboards
layout(location = 0) in vec3 inPosition;   // Billboard quad vertex position
layout(location = 1) in vec2 inTexCoord;   // Billboard quad UV

// Instance data
layout(location = 2) in vec3 instancePos;      // World position of tree
layout(location = 3) in float instanceScale;   // Tree scale
layout(location = 4) in float instanceRotation; // Y-axis rotation
layout(location = 5) in uint instanceArchetype; // Archetype index for atlas lookup

layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position (for billboard facing)
    vec4 lodParams;         // x = blend factor, y = brightness, z = normal strength
    vec4 atlasParams;       // x = unused, y = orthoSize (billboard half-size), z = baseOffset (tree base Y)
    int cascadeIndex;       // Which shadow cascade we're rendering
} push;

layout(location = 0) out vec2 fragTexCoord;

// Atlas layout constants
const int CELLS_PER_ROW = 9;
const int HORIZONTAL_ANGLES = 8;
const float ANGLE_STEP = 360.0 / float(HORIZONTAL_ANGLES);  // 45 degrees

void main() {
    // Get tree instance data
    vec3 treePos = instancePos;
    float scale = instanceScale;
    float rotation = instanceRotation;

    // Compute view direction to tree (from camera for billboard facing)
    vec3 toTree = treePos - push.cameraPos.xyz;
    vec2 toTreeHorizontal = normalize(toTree.xz);

    // Compute horizontal angle (0-360 degrees, clockwise from +Z)
    float hAngle = atan(toTreeHorizontal.x, toTreeHorizontal.y);
    hAngle = degrees(hAngle);
    if (hAngle < 0.0) hAngle += 360.0;

    // Apply tree rotation offset
    hAngle = mod(hAngle - degrees(rotation) + 360.0, 360.0);

    // Select horizontal cell index (0-7)
    int hIndex = int(mod(round(hAngle / ANGLE_STEP), float(HORIZONTAL_ANGLES)));

    // Compute elevation angle
    float dist = length(toTree);
    float elevation = degrees(asin(clamp(-toTree.y / dist, -1.0, 1.0)));

    // Select vertical level
    int cellIndex;
    if (elevation > 67.5) {
        cellIndex = 8;  // Top-down view
    } else if (elevation > 22.5) {
        cellIndex = CELLS_PER_ROW + hIndex;  // Elevated view
    } else {
        cellIndex = hIndex;  // Horizon view
    }

    // Compute atlas UV offset for this cell
    int cellX = cellIndex % CELLS_PER_ROW;
    int cellY = cellIndex / CELLS_PER_ROW;

    // Transform quad UV to atlas UV
    vec2 cellUV = inTexCoord;

    // For top-down view, rotate UV by horizontal angle
    if (elevation > 67.5) {
        float rotAngle = radians(-hAngle + degrees(rotation));
        vec2 centered = cellUV - 0.5;
        cellUV = vec2(
            centered.x * cos(rotAngle) - centered.y * sin(rotAngle),
            centered.x * sin(rotAngle) + centered.y * cos(rotAngle)
        ) + 0.5;
    }

    vec2 atlasUV = (vec2(cellX, cellY) + cellUV) / vec2(float(CELLS_PER_ROW), 2.0);
    fragTexCoord = atlasUV;

    // Billboard orientation: face camera but stay upright
    vec3 forward = normalize(vec3(toTree.x, 0.0, toTree.z));
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, forward);

    // For elevated views, tilt the billboard
    if (elevation > 22.5) {
        float tiltAngle = radians(min(elevation - 22.5, 45.0));
        vec3 tiltedUp = cos(tiltAngle) * up - sin(tiltAngle) * forward;
        vec3 tiltedForward = sin(tiltAngle) * up + cos(tiltAngle) * forward;
        forward = tiltedForward;
        up = tiltedUp;
        right = cross(up, forward);
    }

    // Position billboard vertex
    // Billboard is sized to 2*orthoSize, base at tree base position
    float orthoSize = push.atlasParams.y * scale;
    float baseOffset = push.atlasParams.z * scale;

    vec3 localPos = right * inPosition.x * orthoSize * 2.0 +
                    up * inPosition.y * orthoSize * 2.0;

    // Position billboard with base at tree's base position
    vec3 worldPos = treePos + vec3(0.0, baseOffset, 0.0) + localPos;

    // Transform by cascade light matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);
}
