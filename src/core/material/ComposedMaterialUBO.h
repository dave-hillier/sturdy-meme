#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include "MaterialComponents.h"

namespace material {

/**
 * ComposedMaterialUBO - GPU-compatible uniform buffer for composed materials
 *
 * This struct packs all material components into a single UBO that can be
 * uploaded to the GPU. The enabledFeatures field controls which components
 * are active in the shader (via specialization constants or branching).
 *
 * Alignment follows std140 rules for cross-platform compatibility.
 */
struct ComposedMaterialUBO {
    // Surface component (always present) - 32 bytes
    glm::vec4 baseColor;              // RGB + alpha
    float roughness;
    float metallic;
    float normalScale;
    float aoStrength;

    // Liquid component - 80 bytes
    glm::vec4 liquidColor;            // RGB + transparency
    glm::vec4 liquidAbsorption;       // RGB coefficients + turbidity
    float liquidDepth;
    float liquidAbsorptionScale;
    float liquidScatteringScale;
    float liquidRoughness;
    glm::vec4 liquidFlowParams;       // flowDir.xy, flowSpeed, flowStrength
    float liquidFoamIntensity;
    float liquidSssIntensity;
    float liquidFresnelPower;
    float liquidRefractionStrength;
    uint32_t liquidFlags;
    float liquidPadding[3];

    // Weathering component - 64 bytes
    float snowCoverage;
    float snowBlendSharpness;
    float snowRoughness;
    float wetness;
    glm::vec4 snowColor;              // RGB + padding
    float wetnessRoughnessScale;
    float dirtAccumulation;
    float moss;
    float weatheringPadding;
    glm::vec4 dirtColor;              // RGB + padding
    glm::vec4 mossColor;              // RGB + padding

    // Subsurface component - 32 bytes
    glm::vec4 scatterColor;           // RGB + padding
    float scatterDistance;
    float sssIntensity;
    float sssDistortion;
    float sssPadding;

    // Displacement component - 32 bytes
    float heightScale;
    float heightMidLevel;
    float tessellationLevel;          // As float for GPU compatibility
    float parallaxSteps;              // As float for GPU compatibility
    float waveAmplitude;
    float waveFrequency;
    float waveSpeed;
    uint32_t displacementFlags;       // useParallax, useTessellation, useWaves

    // Emissive component - 16 bytes
    glm::vec4 emissiveColor;          // RGB + intensity

    // Feature flags - 16 bytes
    uint32_t enabledFeatures;
    float time;                       // Animation time
    float emissivePulseSpeed;
    float emissivePulseMin;

    // Default constructor
    ComposedMaterialUBO();

    // Construct from ComposedMaterial
    static ComposedMaterialUBO fromMaterial(const ComposedMaterial& mat, float animTime = 0.0f);

    // Update time for animation
    void updateTime(float deltaTime) {
        time += deltaTime;
    }
};

// Verify alignment for std140
static_assert(sizeof(ComposedMaterialUBO) % 16 == 0, "ComposedMaterialUBO must be 16-byte aligned");
static_assert(sizeof(ComposedMaterialUBO) <= 512, "ComposedMaterialUBO should fit in typical UBO limits");

/**
 * PackedSurfaceUBO - Minimal UBO for basic PBR materials (no extra features)
 *
 * Use when you only need surface properties to minimize GPU bandwidth.
 */
struct PackedSurfaceUBO {
    glm::vec4 baseColor;
    float roughness;
    float metallic;
    float normalScale;
    float aoStrength;

    static PackedSurfaceUBO fromSurface(const SurfaceComponent& surface) {
        PackedSurfaceUBO ubo;
        ubo.baseColor = surface.baseColor;
        ubo.roughness = surface.roughness;
        ubo.metallic = surface.metallic;
        ubo.normalScale = surface.normalScale;
        ubo.aoStrength = surface.aoStrength;
        return ubo;
    }
};

static_assert(sizeof(PackedSurfaceUBO) == 32, "PackedSurfaceUBO should be 32 bytes");

} // namespace material
