#version 450

#extension GL_GOOGLE_include_directive : require

#include "ubo_common.glsl"
#include "lighting_common.glsl"

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float alphaTest;     // Alpha discard threshold for leaves
    int isLeaf;          // 0 = bark, 1 = leaf
} pc;

// Texture samplers
layout(set = 0, binding = 1) uniform sampler2D barkColorTex;
layout(set = 0, binding = 2) uniform sampler2D barkNormalTex;
layout(set = 0, binding = 3) uniform sampler2D barkAOTex;
layout(set = 0, binding = 4) uniform sampler2D barkRoughnessTex;
layout(set = 0, binding = 5) uniform sampler2D leafTex;

// Fragment inputs
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo;
    float alpha = 1.0;
    float ao = 1.0;
    float roughness = pc.roughness;
    vec3 N;

    if (pc.isLeaf == 1) {
        // Leaf rendering - sample leaf texture
        vec4 leafColor = texture(leafTex, fragTexCoord);

        // Alpha test - discard transparent pixels
        if (leafColor.a < pc.alphaTest) {
            discard;
        }

        // Apply tint from vertex color
        albedo = leafColor.rgb * fragColor.rgb;
        alpha = leafColor.a;

        // Use geometric normal for leaves (no normal map)
        N = normalize(fragNormal);

        // Make leaves two-sided
        if (!gl_FrontFacing) {
            N = -N;
        }
    } else {
        // Bark rendering - sample all bark textures
        vec4 barkColor = texture(barkColorTex, fragTexCoord);
        vec3 barkNormalMap = texture(barkNormalTex, fragTexCoord).rgb;
        float barkAO = texture(barkAOTex, fragTexCoord).r;
        float barkRoughness = texture(barkRoughnessTex, fragTexCoord).r;

        // Apply tint from vertex color
        albedo = barkColor.rgb * fragColor.rgb;
        ao = barkAO;
        roughness = barkRoughness;

        // Convert normal map from [0,1] to [-1,1]
        vec3 normalMapValue = barkNormalMap * 2.0 - 1.0;

        // Build TBN matrix for normal mapping
        vec3 T = normalize(fragTangent);
        vec3 Ng = normalize(fragNormal);
        vec3 B = normalize(fragBitangent);
        mat3 TBN = mat3(T, B, Ng);

        // Transform normal from tangent space to world space
        N = normalize(TBN * normalMapValue);
    }

    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Sun lighting
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float sunIntensity = ubo.sunDirection.w;

    // Diffuse + specular lighting
    float NoL = max(dot(N, sunL), 0.0);
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float NoV = max(dot(N, V), 0.001);

    // Diffuse
    vec3 diffuse = albedo / PI;

    // Simple specular (GGX approximation)
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NoH * NoH) * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);
    float specular = D * 0.04 * (1.0 - pc.metallic);

    vec3 sunContrib = (diffuse + specular) * ubo.sunColor.rgb * sunIntensity * NoL;

    // Moon lighting (softer fill)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    float moonNoL = max(dot(N, moonL), 0.0);
    vec3 moonContrib = diffuse * ubo.moonColor.rgb * moonIntensity * moonNoL * 0.3;

    // Ambient lighting with AO
    vec3 ambient = ubo.ambientColor.rgb * albedo * 0.3 * ao;

    // Simple shadow approximation - darken underside
    float hemisphereLight = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    ambient *= mix(0.5, 1.0, hemisphereLight);

    vec3 finalColor = ambient + sunContrib + moonContrib;

    // NO FOG - billboard captures should be clean for later compositing
    // Atmospheric effects will be applied when the billboard is rendered in the scene

    outColor = vec4(finalColor, alpha);
}
