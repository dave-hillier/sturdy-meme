#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "shadow_common.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragBlendFactor;
layout(location = 3) flat in int fragCellIndex;
layout(location = 4) in mat3 fragImpostorToWorld;

layout(location = 0) out vec4 outColor;

// Impostor atlas textures
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2D albedoAlphaAtlas;
layout(binding = BINDING_TREE_IMPOSTOR_NORMAL) uniform sampler2D normalDepthAOAtlas;

// Shadow map
layout(binding = BINDING_TREE_IMPOSTOR_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;

layout(push_constant) uniform PushConstants {
    vec4 cameraPos;
    vec4 lodParams;     // x = blend factor, y = brightness, z = normal strength
    vec4 atlasParams;   // x = archetype index, y = bounding radius
} push;

// Octahedral normal decoding
vec3 decodeOctahedral(vec2 e) {
    e = e * 2.0 - 1.0;  // Map from [0, 1] to [-1, 1]
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

void main() {
    // Sample impostor atlas
    vec4 albedoAlpha = texture(albedoAlphaAtlas, fragTexCoord);
    vec4 normalDepthAO = texture(normalDepthAOAtlas, fragTexCoord);

    // Alpha test
    if (albedoAlpha.a < 0.5) {
        discard;
    }

    // Decode normal from octahedral encoding
    vec3 impostorNormal = decodeOctahedral(normalDepthAO.rg);

    // Transform normal from impostor space to world space
    float normalStrength = push.lodParams.z;
    vec3 worldNormal = normalize(fragImpostorToWorld * impostorNormal);
    worldNormal = mix(vec3(0.0, 1.0, 0.0), worldNormal, normalStrength);

    float depth = normalDepthAO.b;
    float ao = normalDepthAO.a;

    // Get albedo and apply brightness adjustment
    vec3 albedo = albedoAlpha.rgb * push.lodParams.y;

    // PBR parameters for tree foliage
    float metallic = 0.0;
    float roughness = 0.7;

    // Calculate lighting
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 lightDir = normalize(-ubo.sunDirection.xyz);

    // Simple PBR-like lighting
    float NdotL = max(dot(worldNormal, lightDir), 0.0);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(worldNormal, halfVec), 0.0);
    float NdotV = max(dot(worldNormal, viewDir), 0.001);

    // Diffuse
    vec3 diffuse = albedo / 3.14159265;

    // Simple specular
    float spec = pow(NdotH, 32.0) * (1.0 - roughness);
    vec3 specular = vec3(spec) * 0.1;

    // Shadow sampling using cascaded shadow maps
    float shadow = calculateCascadedShadow(
        fragWorldPos, worldNormal, lightDir,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Combine lighting
    vec3 sunLight = ubo.sunColor.rgb * ubo.sunColor.a;
    vec3 ambient = ubo.ambientColor.rgb * ubo.ambientColor.a * ao;

    vec3 color = ambient * albedo;
    color += (diffuse + specular) * sunLight * NdotL * shadow;

    outColor = vec4(color, 1.0);
}
