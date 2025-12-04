#version 450

#extension GL_GOOGLE_include_directive : require

#include "ubo_common.glsl"
#include "lighting_common.glsl"

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float padding[2];
} pc;

// Fragment inputs
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragColor;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Use vertex color as albedo (set by generator based on branch level)
    vec3 albedo = fragColor.rgb;

    // Sun lighting
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float sunIntensity = ubo.sunDirection.w;

    // Simple diffuse + specular lighting
    float NoL = max(dot(N, sunL), 0.0);
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float NoV = max(dot(N, V), 0.001);

    // Diffuse
    vec3 diffuse = albedo / PI;

    // Simple specular (GGX approximation)
    float roughness = pc.roughness;
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

    // Ambient lighting
    vec3 ambient = ubo.ambientColor.rgb * albedo * 0.3;

    // Simple shadow approximation - darken underside
    float hemisphereLight = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    ambient *= mix(0.5, 1.0, hemisphereLight);

    vec3 finalColor = ambient + sunContrib + moonContrib;

    // Apply simple fog based on distance
    float distance = length(fragWorldPos - ubo.cameraPosition.xyz);
    float fogFactor = 1.0 - exp(-distance * 0.002);
    vec3 fogColor = ubo.ambientColor.rgb * 0.8;
    finalColor = mix(finalColor, fogColor, fogFactor * 0.5);

    outColor = vec4(finalColor, fragColor.a);
}
