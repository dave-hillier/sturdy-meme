#version 450

#extension GL_GOOGLE_include_directive : require

#include "ubo_common.glsl"

// Push constants for tree rendering
layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float alphaTest;
    int isLeaf;
} pc;

// Vertex inputs (same as Mesh::Vertex)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

// Safe normalize that returns fallback for zero-length or NaN vectors
// Also sets a debug flag when fallback is used
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
    // Guard against NaN vertex positions - discard by placing at clip space infinity
    vec3 pos = inPosition;
    if (any(isnan(pos)) || any(isinf(pos))) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // Behind near plane, will be clipped
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragTangent = vec3(1.0, 0.0, 0.0);
        fragBitangent = vec3(0.0, 0.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragWorldPos = vec3(0.0);
        fragColor = vec4(1.0);
        return;
    }

    vec4 worldPos = pc.model * vec4(pos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    // Guard against invalid clip coordinates (NaN, inf, or extreme W)
    if (any(isnan(gl_Position)) || any(isinf(gl_Position)) || abs(gl_Position.w) < 0.0001) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // Behind near plane
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragTangent = vec3(1.0, 0.0, 0.0);
        fragBitangent = vec3(0.0, 0.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragWorldPos = vec3(0.0);
        fragColor = vec4(1.0, 0.0, 1.0, 1.0); // Magenta debug
        return;
    }

    // Transform normal to world space using inverse-transpose for correct
    // handling of non-uniform scaling. For uniform scaling this is equivalent
    // to mat3(model), but handles general cases correctly.
    mat3 modelMat3 = mat3(pc.model);
    mat3 normalMatrix = transpose(inverse(modelMat3));
    fragNormal = safeNormalize(normalMatrix * inNormal, vec3(0.0, 1.0, 0.0));

    // Tangent uses regular model matrix (direction in surface plane)
    fragTangent = safeNormalize(modelMat3 * inTangent.xyz, vec3(1.0, 0.0, 0.0));

    // Calculate bitangent (cross product with tangent handedness)
    // Re-orthogonalize after transformation to handle numerical precision
    vec3 bitangentRaw = cross(fragNormal, fragTangent) * inTangent.w;
    fragBitangent = safeNormalize(bitangentRaw, vec3(0.0, 0.0, 1.0));

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;

    // Debug: if any fallback was used, tint the vertex magenta
    if (usedFallback) {
        fragColor = vec4(1.0, 0.0, 1.0, 1.0); // Magenta = fallback used
    } else {
        fragColor = inColor;
    }
}
