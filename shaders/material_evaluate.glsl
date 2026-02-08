// Unified Material Evaluation - evaluates composed materials
// Part of the composable material system (Phase 5)
//
// This shader include provides a single entry point for evaluating materials
// composed from multiple components. Use with ComposedMaterialUBO.
//
// Usage:
//   #include "material_evaluate.glsl"
//   ...
//   MaterialResult result = evaluateMaterial(materialUBO, inputs);
//   fragColor = result.color;

#ifndef MATERIAL_EVALUATE_GLSL
#define MATERIAL_EVALUATE_GLSL

#include "weathering_common.glsl"
#include "terrain_liquid_common.glsl"

// Feature flags (must match FeatureFlags enum in MaterialComponents.h)
#define FEATURE_LIQUID      (1 << 0)
#define FEATURE_WEATHERING  (1 << 1)
#define FEATURE_SUBSURFACE  (1 << 2)
#define FEATURE_DISPLACEMENT (1 << 3)
#define FEATURE_EMISSIVE    (1 << 4)

// Liquid flags (must match LiquidFlags enum)
#define LIQUID_CAUSTICS     (1 << 0)
#define LIQUID_FOAM         (1 << 1)
#define LIQUID_REFLECTION   (1 << 2)
#define LIQUID_REFRACTION   (1 << 3)
#define LIQUID_FLOW         (1 << 4)
#define LIQUID_WAVES        (1 << 5)
#define LIQUID_SUBSURFACE   (1 << 6)

// ============================================================================
// UBO Definition (matches ComposedMaterialUBO in C++)
// ============================================================================

#ifdef COMPOSED_MATERIAL_UBO_BINDING
layout(std140, binding = COMPOSED_MATERIAL_UBO_BINDING) uniform ComposedMaterialUBO {
    // Surface component
    vec4 u_baseColor;
    float u_roughness;
    float u_metallic;
    float u_normalScale;
    float u_aoStrength;

    // Liquid component
    vec4 u_liquidColor;
    vec4 u_liquidAbsorption;
    float u_liquidDepth;
    float u_liquidAbsorptionScale;
    float u_liquidScatteringScale;
    float u_liquidRoughness;
    vec4 u_liquidFlowParams;  // flowDir.xy, flowSpeed, flowStrength
    float u_liquidFoamIntensity;
    float u_liquidSssIntensity;
    float u_liquidFresnelPower;
    float u_liquidRefractionStrength;
    uint u_liquidFlags;
    // Note: Use individual floats instead of float[3] array because std140
    // layout gives arrays a 16-byte stride per element, creating a size
    // mismatch with the C++ struct where floats are packed contiguously.
    float u_liquidPadding0;
    float u_liquidPadding1;
    float u_liquidPadding2;

    // Weathering component
    float u_snowCoverage;
    float u_snowBlendSharpness;
    float u_snowRoughness;
    float u_wetness;
    vec4 u_snowColor;
    float u_wetnessRoughnessScale;
    float u_dirtAccumulation;
    float u_moss;
    float u_weatheringPadding;
    vec4 u_dirtColor;
    vec4 u_mossColor;

    // Subsurface component
    vec4 u_scatterColor;
    float u_scatterDistance;
    float u_sssIntensity;
    float u_sssDistortion;
    float u_sssPadding;

    // Displacement component
    float u_heightScale;
    float u_heightMidLevel;
    float u_tessellationLevel;
    float u_parallaxSteps;
    float u_waveAmplitude;
    float u_waveFrequency;
    float u_waveSpeed;
    uint u_displacementFlags;

    // Emissive component
    vec4 u_emissiveColor;  // RGB + intensity

    // Feature flags
    uint u_enabledFeatures;
    float u_time;
    float u_emissivePulseSpeed;
    float u_emissivePulseMin;
};
#endif

// ============================================================================
// Input/Output Structures
// ============================================================================

// Input for material evaluation
struct MaterialInputs {
    vec3 worldPos;
    vec3 normalWS;          // World-space normal
    vec3 tangentWS;         // World-space tangent
    vec3 bitangentWS;       // World-space bitangent
    vec3 viewDir;           // Direction to camera (normalized)
    vec2 uv;                // Texture coordinates
    vec3 lightDir;          // Primary light direction
    vec3 lightColor;        // Primary light color
    vec3 ambientColor;      // Ambient/sky color
};

// Output from material evaluation
struct MaterialResult {
    vec3 albedo;            // Final albedo after all effects
    vec3 normal;            // Final normal after all perturbations
    float roughness;        // Final roughness
    float metallic;         // Final metallic
    float ao;               // Final ambient occlusion
    vec3 emissive;          // Emissive contribution
    vec3 specular;          // Specular reflection (SSR/environment)
    float specularStrength; // Specular blend factor
    float subsurface;       // Subsurface scattering intensity
    vec3 subsurfaceColor;   // Subsurface scatter color
};

// ============================================================================
// Helper Functions
// ============================================================================

bool hasFeature(uint features, uint flag) {
    return (features & flag) != 0u;
}

bool hasLiquidFlag(uint flags, uint flag) {
    return (flags & flag) != 0u;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Component Evaluators
// ============================================================================

// Evaluate surface component (always applied)
void evaluateSurface(
    inout MaterialResult result,
    vec4 baseColor,
    float roughness,
    float metallic,
    float normalScale,
    float aoStrength
) {
    result.albedo = baseColor.rgb;
    result.roughness = roughness;
    result.metallic = metallic;
    result.ao = 1.0;  // Modified by AO texture if available
}

// Evaluate liquid component
void evaluateLiquid(
    inout MaterialResult result,
    MaterialInputs inputs,
    vec4 liquidColor,
    vec4 absorption,
    float depth,
    float absorptionScale,
    float liquidRoughness,
    vec4 flowParams,
    float foamIntensity,
    float fresnelPower,
    uint liquidFlags,
    float time
) {
    if (depth <= 0.0) return;

    // Blend factor based on depth
    float blend = smoothstep(0.0, 0.1, depth);

    // Water absorption (Beer-Lambert)
    vec3 absorbed = exp(-absorption.rgb * depth * absorptionScale);
    result.albedo *= mix(vec3(1.0), absorbed * liquidColor.rgb, blend);

    // Smooth water surface
    result.roughness = mix(result.roughness, liquidRoughness, blend);
    result.metallic = mix(result.metallic, 0.0, blend);

    // Flow animation for normal perturbation
    if (hasLiquidFlag(liquidFlags, LIQUID_FLOW) && flowParams.z > 0.0) {
        vec2 flowDir = flowParams.xy;
        float flowSpeed = flowParams.z;
        vec2 flowOffset = flowDir * time * flowSpeed;

        // Simple flow distortion
        float flowPhase = dot(inputs.worldPos.xz + flowOffset, flowDir) * 5.0;
        vec3 flowNormal = normalize(vec3(
            sin(flowPhase) * 0.1,
            1.0,
            cos(flowPhase * 0.7) * 0.1
        ));
        result.normal = normalize(mix(result.normal, flowNormal, blend * 0.5));
    }

    // Fresnel reflection
    if (hasLiquidFlag(liquidFlags, LIQUID_REFLECTION)) {
        float NdotV = max(dot(result.normal, inputs.viewDir), 0.0);
        float fresnel = pow(1.0 - NdotV, fresnelPower);
        result.specularStrength = max(result.specularStrength, fresnel * blend);

        // Simple sky reflection
        vec3 reflectDir = reflect(-inputs.viewDir, result.normal);
        result.specular = mix(result.specular, inputs.ambientColor * (0.5 + 0.5 * reflectDir.y), blend);
    }

    // Foam (at edges or high turbulence)
    if (hasLiquidFlag(liquidFlags, LIQUID_FOAM) && foamIntensity > 0.0) {
        float foam = foamIntensity * (1.0 - smoothstep(0.0, 0.2, depth));
        result.albedo = mix(result.albedo, vec3(0.9), foam);
        result.roughness = mix(result.roughness, 0.8, foam);
    }
}

// Evaluate weathering component
void evaluateWeathering(
    inout MaterialResult result,
    MaterialInputs inputs,
    float snowCoverage,
    float snowBlendSharpness,
    float snowRoughness,
    vec3 snowColor,
    float wetness,
    float wetnessRoughnessScale,
    float dirtAccumulation,
    vec3 dirtColor,
    float moss,
    vec3 mossColor
) {
    // Snow accumulation (top-facing surfaces)
    if (snowCoverage > 0.0) {
        float upDot = max(dot(inputs.normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
        float snowFactor = smoothstep(0.5, 0.8, upDot) * snowCoverage;
        snowFactor = pow(snowFactor, 1.0 / snowBlendSharpness);

        result.albedo = mix(result.albedo, snowColor, snowFactor);
        result.roughness = mix(result.roughness, snowRoughness, snowFactor);
        result.metallic = mix(result.metallic, 0.0, snowFactor);
    }

    // Wetness (darkens albedo, reduces roughness)
    if (wetness > 0.0) {
        result.albedo *= mix(1.0, 0.6, wetness);
        result.roughness *= mix(1.0, wetnessRoughnessScale, wetness);
    }

    // Dirt accumulation (crevices)
    if (dirtAccumulation > 0.0) {
        // Accumulate in crevices (low AO areas)
        float dirtFactor = dirtAccumulation * (1.0 - result.ao);
        result.albedo = mix(result.albedo, dirtColor, dirtFactor);
        result.roughness = mix(result.roughness, 0.9, dirtFactor * 0.5);
    }

    // Moss growth (north-facing + wet areas)
    if (moss > 0.0) {
        float northDot = max(dot(inputs.normalWS, vec3(0.0, 0.0, -1.0)), 0.0);
        float mossFactor = moss * northDot * wetness;
        result.albedo = mix(result.albedo, mossColor, mossFactor);
        result.roughness = mix(result.roughness, 0.7, mossFactor);
    }
}

// Evaluate subsurface scattering
void evaluateSubsurface(
    inout MaterialResult result,
    MaterialInputs inputs,
    vec3 scatterColor,
    float scatterDistance,
    float intensity,
    float distortion
) {
    if (intensity <= 0.0) return;

    // Simple view-dependent SSS approximation
    vec3 scatterDir = inputs.lightDir + inputs.normalWS * distortion;
    float scatterDot = pow(clamp(dot(inputs.viewDir, -scatterDir), 0.0, 1.0), 2.0);

    result.subsurface = intensity * scatterDot;
    result.subsurfaceColor = scatterColor * inputs.lightColor;
}

// Evaluate emissive component
void evaluateEmissive(
    inout MaterialResult result,
    vec4 emissiveColor,
    float pulseSpeed,
    float pulseMin,
    float time
) {
    if (emissiveColor.a <= 0.0) return;

    float intensity = emissiveColor.a;

    // Pulse animation
    if (pulseSpeed > 0.0) {
        float pulse = sin(time * pulseSpeed * 6.28318) * 0.5 + 0.5;
        intensity *= mix(pulseMin, 1.0, pulse);
    }

    result.emissive = emissiveColor.rgb * intensity;
}

// ============================================================================
// Main Evaluation Function
// ============================================================================

#ifdef COMPOSED_MATERIAL_UBO_BINDING
// Evaluate complete material from UBO
MaterialResult evaluateMaterial(MaterialInputs inputs) {
    MaterialResult result;
    result.albedo = vec3(0.5);
    result.normal = inputs.normalWS;
    result.roughness = 0.5;
    result.metallic = 0.0;
    result.ao = 1.0;
    result.emissive = vec3(0.0);
    result.specular = vec3(0.0);
    result.specularStrength = 0.0;
    result.subsurface = 0.0;
    result.subsurfaceColor = vec3(0.0);

    // Surface (always)
    evaluateSurface(result, u_baseColor, u_roughness, u_metallic, u_normalScale, u_aoStrength);

    // Weathering (before liquid, affects base appearance)
    if (hasFeature(u_enabledFeatures, FEATURE_WEATHERING)) {
        evaluateWeathering(result, inputs,
            u_snowCoverage, u_snowBlendSharpness, u_snowRoughness, u_snowColor.rgb,
            u_wetness, u_wetnessRoughnessScale,
            u_dirtAccumulation, u_dirtColor.rgb,
            u_moss, u_mossColor.rgb);
    }

    // Liquid (can overlay weathering effects)
    if (hasFeature(u_enabledFeatures, FEATURE_LIQUID)) {
        evaluateLiquid(result, inputs,
            u_liquidColor, u_liquidAbsorption,
            u_liquidDepth, u_liquidAbsorptionScale,
            u_liquidRoughness, u_liquidFlowParams,
            u_liquidFoamIntensity, u_liquidFresnelPower,
            u_liquidFlags, u_time);
    }

    // Subsurface scattering
    if (hasFeature(u_enabledFeatures, FEATURE_SUBSURFACE)) {
        evaluateSubsurface(result, inputs,
            u_scatterColor.rgb, u_scatterDistance,
            u_sssIntensity, u_sssDistortion);
    }

    // Emissive
    if (hasFeature(u_enabledFeatures, FEATURE_EMISSIVE)) {
        evaluateEmissive(result,
            u_emissiveColor, u_emissivePulseSpeed,
            u_emissivePulseMin, u_time);
    }

    return result;
}
#endif

// Evaluate from explicit parameters (for materials without UBO)
MaterialResult evaluateMaterialExplicit(
    MaterialInputs inputs,
    vec4 baseColor,
    float roughness,
    float metallic,
    float normalScale,
    float aoStrength,
    uint enabledFeatures
) {
    MaterialResult result;
    result.albedo = baseColor.rgb;
    result.normal = inputs.normalWS;
    result.roughness = roughness;
    result.metallic = metallic;
    result.ao = 1.0;
    result.emissive = vec3(0.0);
    result.specular = vec3(0.0);
    result.specularStrength = 0.0;
    result.subsurface = 0.0;
    result.subsurfaceColor = vec3(0.0);

    evaluateSurface(result, baseColor, roughness, metallic, normalScale, aoStrength);

    return result;
}

#endif // MATERIAL_EVALUATE_GLSL
