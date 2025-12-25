#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "tree_impostor_instance.glsl"
#include "octahedral_mapping.glsl"

// Per-vertex data for impostor billboards
layout(location = 0) in vec3 inPosition;   // Billboard quad vertex position
layout(location = 1) in vec2 inTexCoord;   // Billboard quad UV

// Visible impostor instances from compute shader (SSBO)
layout(std430, binding = BINDING_TREE_IMPOSTOR_INSTANCES) readonly buffer InstanceBuffer {
    ImpostorInstance instances[];
};

// Simplified push constants
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // xyz = camera world position (for billboard facing)
    vec4 lodParams;         // x = useOctahedral (0 or 1), y = brightness, z = normal strength, w = debug elevation
    int cascadeIndex;       // Which shadow cascade we're rendering
} push;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragArchetypeIndex;

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
    float hSize = inst.sizeAndOffset.x;
    float vSize = inst.sizeAndOffset.y;
    float baseOffset = inst.sizeAndOffset.z;

    // Check if using octahedral mapping (Phase 6)
    bool useOctahedral = push.lodParams.x > 0.5;

    // For shadows, orient billboard to face the sun (not the camera)
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 toSun = sunDir;
    vec2 toSunHorizontal = normalize(toSun.xz);

    // Compute sun elevation angle
    float sunElevation = degrees(asin(clamp(sunDir.y, -1.0, 1.0)));

    vec2 atlasUV;

    if (useOctahedral) {
        // === OCTAHEDRAL MAPPING (Phase 6) ===
        // Convert sun direction to impostor space (apply tree rotation)
        vec3 impostorDir = viewToImpostorSpace(sunDir, rotation);

        // Compute octahedral UV (center of the rendered cell for this view)
        vec2 centerUV = octahedralEncode(impostorDir);

        // Each view was rendered to a 128x128 region in the 2048x2048 atlas
        const float CELL_SCALE = 128.0 / 2048.0;

        // Map billboard UV to cell UV offset
        vec2 uvOffset = inTexCoord - vec2(0.5);

        // Account for aspect ratio: billboard has dimensions 2*hSize x 2*vSize
        // but the atlas cell is square (128x128)
        float aspectRatio = hSize / max(vSize, 0.001);
        if (aspectRatio < 1.0) {
            uvOffset.x *= aspectRatio;
        } else {
            uvOffset.y /= aspectRatio;
        }

        atlasUV = centerUV + uvOffset * CELL_SCALE;
        atlasUV = clamp(atlasUV, vec2(0.0), vec2(1.0));
        fragTexCoord = atlasUV;
    } else {
        // === LEGACY 17-VIEW MAPPING ===
        float hAngle = atan(toSunHorizontal.x, toSunHorizontal.y);
        hAngle = degrees(hAngle);
        if (hAngle < 0.0) hAngle += 360.0;

        // Apply tree rotation offset
        hAngle = mod(hAngle + degrees(rotation) + 360.0, 360.0);

        // Select horizontal cell index (0-7)
        int hIndex = int(mod(round(hAngle / ANGLE_STEP), float(HORIZONTAL_ANGLES)));

        // Select vertical level based on sun elevation
        int cellIndex;
        if (sunElevation > 67.5) {
            cellIndex = 8;  // Top-down view
        } else if (sunElevation > 22.5) {
            cellIndex = CELLS_PER_ROW + hIndex;  // Elevated view
        } else {
            cellIndex = hIndex;  // Horizon view
        }

        // Compute atlas UV offset for this cell
        int cellX = cellIndex % CELLS_PER_ROW;
        int cellY = cellIndex / CELLS_PER_ROW;

        vec2 cellUV = inTexCoord;

        // For top-down view, rotate UV by horizontal angle
        if (sunElevation > 67.5) {
            float rotAngle = radians(-hAngle);
            vec2 centered = cellUV - 0.5;
            cellUV = vec2(
                centered.x * cos(rotAngle) - centered.y * sin(rotAngle),
                centered.x * sin(rotAngle) + centered.y * cos(rotAngle)
            ) + 0.5;
        }

        atlasUV = (vec2(cellX, cellY) + cellUV) / vec2(float(CELLS_PER_ROW), 2.0);
        fragTexCoord = atlasUV;
    }

    // Billboard orientation: face toward sun to cast full shadow
    vec3 forward = normalize(vec3(toSun.x, 0.0, toSun.z));
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, forward);

    // For octahedral mode, use smooth tilt; for legacy, use discrete tilt
    if (useOctahedral) {
        float tiltAmount = smoothstep(15.0, 60.0, sunElevation);
        float tiltAngle = tiltAmount * radians(45.0);
        vec3 tiltedUp = cos(tiltAngle) * up - sin(tiltAngle) * forward;
        vec3 tiltedForward = sin(tiltAngle) * up + cos(tiltAngle) * forward;
        forward = tiltedForward;
        up = tiltedUp;
        right = cross(up, forward);
    } else if (sunElevation > 22.5) {
        float tiltAngle = radians(min(sunElevation - 22.5, 45.0));
        vec3 tiltedUp = cos(tiltAngle) * up - sin(tiltAngle) * forward;
        vec3 tiltedForward = sin(tiltAngle) * up + cos(tiltAngle) * forward;
        forward = tiltedForward;
        up = tiltedUp;
        right = cross(up, forward);
    }

    // Position billboard vertex
    vec3 localPos = right * inPosition.x * hSize * 2.0 +
                    up * inPosition.y * vSize * 2.0;

    // Position billboard with base at tree's base position
    vec3 worldPos = treePos + vec3(0.0, baseOffset, 0.0) + localPos;

    // Transform by cascade light matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    fragArchetypeIndex = archetypeIndex;
}
