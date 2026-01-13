#include "ComposedMaterialUBO.h"

namespace material {

ComposedMaterialUBO::ComposedMaterialUBO()
    : baseColor(1.0f)
    , roughness(0.5f)
    , metallic(0.0f)
    , normalScale(1.0f)
    , aoStrength(1.0f)
    // Liquid defaults
    , liquidColor(0.0f, 0.3f, 0.5f, 0.8f)
    , liquidAbsorption(0.4f, 0.08f, 0.04f, 0.1f)
    , liquidDepth(0.0f)
    , liquidAbsorptionScale(1.0f)
    , liquidScatteringScale(0.3f)
    , liquidRoughness(0.02f)
    , liquidFlowParams(0.0f)
    , liquidFoamIntensity(0.0f)
    , liquidSssIntensity(0.3f)
    , liquidFresnelPower(5.0f)
    , liquidRefractionStrength(1.0f)
    , liquidFlags(0)
    , liquidPadding{0.0f, 0.0f, 0.0f}
    // Weathering defaults
    , snowCoverage(0.0f)
    , snowBlendSharpness(2.0f)
    , snowRoughness(0.8f)
    , wetness(0.0f)
    , snowColor(0.95f, 0.95f, 0.98f, 0.0f)
    , wetnessRoughnessScale(0.3f)
    , dirtAccumulation(0.0f)
    , moss(0.0f)
    , weatheringPadding(0.0f)
    , dirtColor(0.3f, 0.25f, 0.2f, 0.0f)
    , mossColor(0.2f, 0.35f, 0.15f, 0.0f)
    // Subsurface defaults
    , scatterColor(1.0f, 0.2f, 0.1f, 0.0f)
    , scatterDistance(0.1f)
    , sssIntensity(0.0f)
    , sssDistortion(0.5f)
    , sssPadding(0.0f)
    // Displacement defaults
    , heightScale(0.0f)
    , heightMidLevel(0.5f)
    , tessellationLevel(1.0f)
    , parallaxSteps(8.0f)
    , waveAmplitude(0.0f)
    , waveFrequency(1.0f)
    , waveSpeed(1.0f)
    , displacementFlags(0)
    // Emissive defaults
    , emissiveColor(0.0f)
    // Feature flags
    , enabledFeatures(0)
    , time(0.0f)
    , emissivePulseSpeed(0.0f)
    , emissivePulseMin(0.5f)
{
}

ComposedMaterialUBO ComposedMaterialUBO::fromMaterial(const ComposedMaterial& mat, float animTime) {
    ComposedMaterialUBO ubo;

    // Surface (always present)
    ubo.baseColor = mat.surface.baseColor;
    ubo.roughness = mat.surface.roughness;
    ubo.metallic = mat.surface.metallic;
    ubo.normalScale = mat.surface.normalScale;
    ubo.aoStrength = mat.surface.aoStrength;

    // Liquid
    if (hasFeature(mat.enabledFeatures, FeatureFlags::Liquid)) {
        ubo.liquidColor = mat.liquid.color;
        ubo.liquidAbsorption = mat.liquid.absorption;
        ubo.liquidDepth = mat.liquid.depth;
        ubo.liquidAbsorptionScale = mat.liquid.absorptionScale;
        ubo.liquidScatteringScale = mat.liquid.scatteringScale;
        ubo.liquidRoughness = mat.liquid.roughness;
        ubo.liquidFlowParams = glm::vec4(
            mat.liquid.flowDirection.x,
            mat.liquid.flowDirection.y,
            mat.liquid.flowSpeed,
            mat.liquid.flowStrength
        );
        ubo.liquidFoamIntensity = mat.liquid.foamIntensity;
        ubo.liquidSssIntensity = mat.liquid.sssIntensity;
        ubo.liquidFresnelPower = mat.liquid.fresnelPower;
        ubo.liquidRefractionStrength = mat.liquid.refractionStrength;
        ubo.liquidFlags = static_cast<uint32_t>(mat.liquid.flags);
    }

    // Weathering
    if (hasFeature(mat.enabledFeatures, FeatureFlags::Weathering)) {
        ubo.snowCoverage = mat.weathering.snowCoverage;
        ubo.snowBlendSharpness = mat.weathering.snowBlendSharpness;
        ubo.snowRoughness = mat.weathering.snowRoughness;
        ubo.wetness = mat.weathering.wetness;
        ubo.snowColor = glm::vec4(mat.weathering.snowColor, 0.0f);
        ubo.wetnessRoughnessScale = mat.weathering.wetnessRoughnessScale;
        ubo.dirtAccumulation = mat.weathering.dirtAccumulation;
        ubo.moss = mat.weathering.moss;
        ubo.dirtColor = glm::vec4(mat.weathering.dirtColor, 0.0f);
        ubo.mossColor = glm::vec4(mat.weathering.mossColor, 0.0f);
    }

    // Subsurface
    if (hasFeature(mat.enabledFeatures, FeatureFlags::Subsurface)) {
        ubo.scatterColor = glm::vec4(mat.subsurface.scatterColor, 0.0f);
        ubo.scatterDistance = mat.subsurface.scatterDistance;
        ubo.sssIntensity = mat.subsurface.intensity;
        ubo.sssDistortion = mat.subsurface.distortion;
    }

    // Displacement
    if (hasFeature(mat.enabledFeatures, FeatureFlags::Displacement)) {
        ubo.heightScale = mat.displacement.heightScale;
        ubo.heightMidLevel = mat.displacement.midLevel;
        ubo.tessellationLevel = static_cast<float>(mat.displacement.tessellationLevel);
        ubo.parallaxSteps = static_cast<float>(mat.displacement.parallaxSteps);
        ubo.waveAmplitude = mat.displacement.waveAmplitude;
        ubo.waveFrequency = mat.displacement.waveFrequency;
        ubo.waveSpeed = mat.displacement.waveSpeed;
        ubo.displacementFlags = mat.displacement.useParallax ? 1u : 0u;
    }

    // Emissive
    if (hasFeature(mat.enabledFeatures, FeatureFlags::Emissive)) {
        ubo.emissiveColor = glm::vec4(
            mat.emissive.emissiveColor * mat.emissive.intensity,
            mat.emissive.intensity
        );
        ubo.emissivePulseSpeed = mat.emissive.pulseSpeed;
        ubo.emissivePulseMin = mat.emissive.pulseMin;
    }

    ubo.enabledFeatures = static_cast<uint32_t>(mat.enabledFeatures);
    ubo.time = animTime;

    return ubo;
}

} // namespace material
