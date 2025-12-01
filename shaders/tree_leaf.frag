#version 450

#extension GL_GOOGLE_include_directive : require

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;

// GPU light structure (must match CPU GPULight struct)
struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), z = shadow map index, w = padding
};

// Light buffer SSBO
layout(std430, binding = 4) readonly buffer LightBuffer {
    uvec4 lightCount;
    GPULight lights[MAX_LIGHTS];
} lightBuffer;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragColor;
layout(location = 4) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Material parameters for tree leaves
const float LEAF_ROUGHNESS = 0.65;  // Leaves are somewhat glossy
const float LEAF_SSS_STRENGTH = 0.45;  // Strong subsurface scattering for backlit leaves
const float LEAF_SPECULAR_STRENGTH = 0.12;  // Subtle specular for waxy leaf surface
const float LEAF_TRANSLUCENCY = 0.6;  // How much light passes through

// Calculate contribution from a single dynamic light for leaves
vec3 calculateDynamicLightLeaf(GPULight light, vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 lightPos = light.positionAndType.xyz;
    uint lightType = uint(light.positionAndType.w);
    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;
    float lightRadius = light.radiusAndInnerCone.x;

    if (lightIntensity <= 0.0) return vec3(0.0);

    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    if (lightRadius > 0.0 && distance > lightRadius) return vec3(0.0);

    float attenuation = calculateAttenuation(distance, lightRadius);

    if (lightType == LIGHT_TYPE_SPOT) {
        vec3 spotDir = normalize(light.directionAndCone.xyz);
        float outerCone = light.directionAndCone.w;
        float innerCone = light.radiusAndInnerCone.y;
        float spotFalloff = calculateSpotFalloff(L, spotDir, innerCone, outerCone);
        attenuation *= spotFalloff;
    }

    // Two-sided diffuse for leaves (like grass)
    float NdotL = dot(N, L);
    float diffuse = max(NdotL, 0.0) + max(-NdotL, 0.0) * LEAF_TRANSLUCENCY;

    // SSS for dynamic lights
    vec3 sss = calculateSSS(L, V, N, lightColor, albedo, LEAF_SSS_STRENGTH * 0.5);

    return (albedo * diffuse + sss) * lightColor * lightIntensity * attenuation;
}

// Calculate contribution from all dynamic lights
vec3 calculateAllDynamicLightsLeaf(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(lightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLightLeaf(lightBuffer.lights[i], N, V, worldPos, albedo);
    }

    return totalLight;
}

void main() {
    // Create ellipse leaf shape using UV
    vec2 centered = fragUV * 2.0 - 1.0;
    float leafShape = 1.0 - length(centered * vec2(1.0, 0.7));
    leafShape = smoothstep(0.0, 0.3, leafShape);

    // Alpha test for leaf shape
    if (leafShape < 0.1) {
        discard;
    }

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // === COLOR PALETTE SYSTEM (Milestone 6) ===
    // Base color from compute shader already includes clump-based variation
    vec3 albedo = fragColor;

    // Additional subtle variation based on UV (leaf position)
    // Outer edges slightly lighter (more sun exposure)
    float edgeBrightness = 1.0 + (1.0 - leafShape) * 0.15;
    albedo *= edgeBrightness;

    // === SUN LIGHTING ===
    vec3 sunL = normalize(ubo.sunDirection.xyz);

    float shadow = calculateCascadedShadow(
        fragWorldPos, N, sunL,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Two-sided diffuse (leaves are thin like grass)
    float sunNdotL = dot(N, sunL);
    float sunDiffuse = max(sunNdotL, 0.0);

    // Translucency - light from behind passes through leaf (key for natural look)
    float backLight = max(-sunNdotL, 0.0) * LEAF_TRANSLUCENCY;

    // Warm the transmitted light (chlorophyll filtering)
    vec3 translucentColor = albedo * vec3(1.3, 1.15, 0.65);

    // Specular highlight (subtle waxy sheen)
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, LEAF_ROUGHNESS);
    vec3 F0 = vec3(0.04);
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * F * LEAF_SPECULAR_STRENGTH;

    // Subsurface scattering - light through leaves when backlit
    vec3 sss = calculateSSS(sunL, V, N, ubo.sunColor.rgb, albedo, LEAF_SSS_STRENGTH);

    vec3 sunLight = (albedo * sunDiffuse + translucentColor * backLight + specular + sss)
                    * ubo.sunColor.rgb * ubo.sunDirection.w * shadow;

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonNdotL = dot(N, moonL);
    float moonDiffuse = max(moonNdotL, 0.0) + max(-moonNdotL, 0.0) * LEAF_TRANSLUCENCY * 0.5;

    // Moon SSS (subtle, fades in during twilight)
    float twilightFactor = smoothstep(0.17, -0.1, ubo.sunDirection.y);
    float moonVisibility = smoothstep(-0.09, 0.1, ubo.moonDirection.y);
    float moonSssFactor = twilightFactor * moonVisibility;
    vec3 moonSss = calculateSSS(moonL, V, N, ubo.moonColor.rgb, albedo, LEAF_SSS_STRENGTH)
                   * 0.3 * moonSssFactor;

    vec3 moonLight = (albedo * moonDiffuse + moonSss) * ubo.moonColor.rgb * ubo.moonDirection.w;

    // === DYNAMIC LIGHTS ===
    vec3 dynamicLights = calculateAllDynamicLightsLeaf(N, V, fragWorldPos, albedo);

    // === FRESNEL RIM LIGHTING ===
    float NoV = max(dot(N, V), 0.0);
    float rimFresnel = pow(1.0 - NoV, 4.0);
    vec3 rimColor = ubo.ambientColor.rgb * 0.4 + ubo.sunColor.rgb * ubo.sunDirection.w * 0.15;
    vec3 rimLight = rimColor * rimFresnel * 0.12;

    // === AMBIENT LIGHTING ===
    // Self-shadowing approximation based on depth in canopy
    // Leaves deep in the canopy are darker (approximates occlusion from outer leaves)
    float canopyDepth = clamp(1.0 - length(centered) * 0.5, 0.0, 1.0);
    float selfShadow = mix(1.0, 0.5, canopyDepth * 0.3);

    // Height-based ambient (cooler at bottom like grass)
    vec3 ambientBase = ubo.ambientColor.rgb * vec3(0.85, 0.9, 0.95);
    vec3 ambientTop = ubo.ambientColor.rgb * vec3(1.0, 1.0, 0.95);
    float heightFactor = fragWorldPos.y * 0.02;  // Approximate based on world height
    heightFactor = clamp(heightFactor, 0.0, 1.0);
    vec3 ambient = albedo * mix(ambientBase, ambientTop, heightFactor);

    // === COMBINE LIGHTING ===
    vec3 finalColor = (ambient + sunLight + moonLight + dynamicLights + rimLight) * selfShadow;

    // === AERIAL PERSPECTIVE ===
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 atmosphericColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz,
                                                    normalize(cameraToFrag), viewDistance,
                                                    sunDir, sunColor);

    // Alpha based on leaf shape with soft edges
    float alpha = leafShape;

    outColor = vec4(atmosphericColor, alpha);
}
