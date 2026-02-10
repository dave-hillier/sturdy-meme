#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "scene_instance_common.glsl"
#include "noise_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused in non-skinned path)
layout(location = 6) in vec4 inColor;

// Wind uniform buffer for vegetation animation
layout(binding = BINDING_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragColor;
// Pass material index to fragment shader for bindless material lookup
layout(location = 5) flat out uint fragMaterialIndex;

void main() {
    // Get instance data from buffer
    SceneInstance inst = sceneInstances[gl_InstanceIndex];
    mat4 model = inst.model;

    vec4 worldPos = model * vec4(inPosition, 1.0);

    // Wind animation using skeletal approach:
    // inColor.rgb = pivot point (branch/leaf attachment in model space)
    // inColor.a = branch level (0-1), where 0 = trunk, 1 = leaves
    // Non-vegetation objects have default color (1,1,1,1), so we detect that
    float branchLevel = inColor.a;
    vec3 pivotLocal = inColor.rgb;

    // Detect vegetation: NOT the default white (1,1,1,1) color
    bool isDefaultColor = (inColor.r > 0.99 && inColor.g > 0.99 && inColor.b > 0.99 && inColor.a > 0.99);
    bool isVegetation = !isDefaultColor;

    if (isVegetation) {
        float windStrength = wind.windDirectionAndStrength.z;
        float noiseScale = wind.windParams.z;
        float windTime = wind.windParams.w;
        float gustFreq = wind.windParams.x;
        vec2 windDir = wind.windDirectionAndStrength.xy;

        // Transform pivot to world space
        vec3 pivotWorld = (model * vec4(pivotLocal, 1.0)).xyz;

        // Sample wind noise based on tree base (XZ of world position)
        vec3 treeBase = worldPos.xyz;
        treeBase.y = 0.0;
        float windOffset = 6.283185 * simplex3(treeBase / noiseScale);

        // Multi-frequency wind oscillation for natural motion
        float windOsc =
            0.5 * sin(windTime * gustFreq + windOffset) +
            0.3 * sin(2.0 * windTime * gustFreq + 1.3 * windOffset) +
            0.2 * sin(5.0 * windTime * gustFreq + 1.5 * windOffset);

        // Bend angle scales with branch level (higher = more sway)
        float distFromPivot = length(worldPos.xyz - pivotWorld);
        float bendAngle = branchLevel * windStrength * windOsc * 0.08 * (1.0 + distFromPivot * 0.05);

        // Rotation axis perpendicular to wind direction in XZ plane
        vec3 rotAxis = normalize(vec3(-windDir.y, 0.0, windDir.x));

        // Position relative to this branch's pivot point
        vec3 relPos = worldPos.xyz - pivotWorld;

        // Rodrigues' rotation formula
        float c = cos(bendAngle);
        float s = sin(bendAngle);
        vec3 rotated = relPos * c + cross(rotAxis, relPos) * s + rotAxis * dot(rotAxis, relPos) * (1.0 - c);

        worldPos.xyz = pivotWorld + rotated;
    }

    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(model) * inNormal;
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    fragTangent = vec4(mat3(model) * inTangent.xyz, inTangent.w);
    fragColor = isVegetation ? vec4(1.0, 1.0, 1.0, 1.0) : inColor;
    fragMaterialIndex = inst.materialIndex;
}
