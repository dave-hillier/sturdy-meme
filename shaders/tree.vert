#version 450

#extension GL_GOOGLE_include_directive : require

#include "ubo_common.glsl"
#include "tree_wind.glsl"

// Push constants for tree rendering
layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float alphaTest;
    int isLeaf;
} pc;

// Standard vertex inputs (same as Mesh::Vertex)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Wind animation inputs (TreeVertex extensions)
layout(location = 7) in vec3 inBranchOrigin;  // Branch origin point for rotation
layout(location = 8) in vec4 inWindParams;    // x=level, y=phase, z=flexibility, w=length

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

// Safe normalize that returns fallback for zero-length or NaN vectors
bool usedFallback = false;
vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len = length(v);
    if (len > 0.0001 && !isnan(len) && !isinf(len)) {
        return v / len;
    }
    usedFallback = true;
    return fallback;
}

void main() {
    // Guard against NaN vertex positions
    vec3 pos = inPosition;
    if (any(isnan(pos)) || any(isinf(pos))) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragTangent = vec3(1.0, 0.0, 0.0);
        fragBitangent = vec3(0.0, 0.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragWorldPos = vec3(0.0);
        fragColor = vec4(1.0);
        return;
    }

    // Extract wind parameters
    float branchLevel = inWindParams.x;
    float phaseOffset = inWindParams.y;
    float flexibility = inWindParams.z;
    float branchLength = inWindParams.w;

    // Get wind settings from UBO
    vec2 windDir = normalize(ubo.windDirectionAndSpeed.xy + vec2(0.0001)); // Avoid zero
    float windSpeed = ubo.windDirectionAndSpeed.z;
    float windTime = ubo.windDirectionAndSpeed.w;

    // Calculate wind strength (base strength from speed, could be enhanced)
    float windStrength = clamp(windSpeed * 0.5, 0.0, 2.0);

    // Transform tree world position for wind sampling
    vec3 treeWorldPos = (pc.model * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    // Apply wind animation in model space
    // Based on Ghost of Tsushima: rotate vertices around branch origin
    vec3 animatedPos = pos;

    // Only apply wind if we have valid wind data (flexibility > 0)
    if (flexibility > 0.001 && windStrength > 0.001) {
        // Check if this is a leaf (isLeaf flag)
        if (pc.isLeaf != 0) {
            // Leaves get rapid flutter animation
            animatedPos = applyLeafWind(
                pos,
                inBranchOrigin,
                phaseOffset,
                windDir,
                windStrength,
                windTime
            );
        } else {
            // Branches get the full tree wind treatment
            animatedPos = applyTreeWind(
                pos,
                inBranchOrigin,
                branchLevel,
                flexibility,
                phaseOffset,
                branchLength,
                windDir,
                windStrength,
                windSpeed,
                windTime,
                treeWorldPos
            );
        }
    }

    // Transform to world space
    vec4 worldPos = pc.model * vec4(animatedPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    // Guard against invalid clip coordinates
    if (any(isnan(gl_Position)) || any(isinf(gl_Position)) || abs(gl_Position.w) < 0.0001) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragTangent = vec3(1.0, 0.0, 0.0);
        fragBitangent = vec3(0.0, 0.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragWorldPos = vec3(0.0);
        fragColor = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    // Transform normal to world space
    mat3 modelMat3 = mat3(pc.model);
    mat3 normalMatrix = transpose(inverse(modelMat3));
    fragNormal = safeNormalize(normalMatrix * inNormal, vec3(0.0, 1.0, 0.0));

    // Tangent uses regular model matrix
    fragTangent = safeNormalize(modelMat3 * inTangent.xyz, vec3(1.0, 0.0, 0.0));

    // Calculate bitangent
    vec3 bitangentRaw = cross(fragNormal, fragTangent) * inTangent.w;
    fragBitangent = safeNormalize(bitangentRaw, vec3(0.0, 0.0, 1.0));

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;

    // Debug: tint magenta if fallback was used
    if (usedFallback) {
        fragColor = vec4(1.0, 0.0, 1.0, 1.0);
    } else {
        fragColor = inColor;
    }
}
