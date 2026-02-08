#pragma once

#include <glm/glm.hpp>
#include "MaterialComponents.h"

namespace material {

/**
 * TerrainLiquidUBO - GPU-compatible uniform buffer for terrain liquid effects
 *
 * Enables puddles, wet surfaces, and streams on terrain without separate
 * water geometry. Works with the terrain_liquid_common.glsl shader include.
 */
struct TerrainLiquidUBO {
    // Global wetness (from rain, etc.)
    float globalWetness;        // 0-1 overall wetness level
    float puddleThreshold;      // Wetness level for puddles to form
    float maxPuddleDepth;       // Maximum puddle water depth (meters)
    float puddleEdgeSoftness;   // Edge blend distance

    // Puddle appearance
    glm::vec4 puddleWaterColor; // RGB + unused
    float puddleRoughness;      // Water surface roughness (0.02-0.1)
    float puddleReflectivity;   // Base reflection strength
    float puddleRippleStrength; // Rain ripple intensity
    float puddleRippleScale;    // Ripple pattern scale

    // Stream parameters
    glm::vec4 streamWaterColor; // RGB + unused
    glm::vec2 streamFlowDirection; // Normalized flow direction
    float streamFlowSpeed;      // Animation speed
    float streamWidth;          // Stream width (meters)
    float streamDepth;          // Water depth
    float streamFoamIntensity;  // White water amount
    float streamTurbulence;     // Surface roughness from flow
    float streamEnabled;        // 1.0 = enabled

    // Shore wetness
    float shoreWetnessRange;    // How far wetness extends from water
    float shoreWaveHeight;      // For splash zone calculation
    float waterLevel;           // Water surface Y position
    float padding;

    // Animation
    float time;                 // Animation time (seconds)
    // Note: Individual floats instead of float[3] array to match GLSL std140
    // layout (arrays get 16-byte stride per element in std140, scalars don't)
    float padding2a;
    float padding2b;
    float padding2c;

    // Default constructor
    TerrainLiquidUBO() :
        globalWetness(0.0f),
        puddleThreshold(0.5f),
        maxPuddleDepth(0.03f),  // 3cm max puddle depth
        puddleEdgeSoftness(0.01f),
        puddleWaterColor(0.02f, 0.03f, 0.04f, 1.0f),
        puddleRoughness(0.02f),
        puddleReflectivity(0.8f),
        puddleRippleStrength(0.5f),
        puddleRippleScale(2.0f),
        streamWaterColor(0.04f, 0.06f, 0.05f, 1.0f),
        streamFlowDirection(1.0f, 0.0f),
        streamFlowSpeed(0.5f),
        streamWidth(5.0f),
        streamDepth(0.3f),
        streamFoamIntensity(0.3f),
        streamTurbulence(0.2f),
        streamEnabled(0.0f),
        shoreWetnessRange(5.0f),
        shoreWaveHeight(0.3f),
        waterLevel(0.0f),
        padding(0.0f),
        time(0.0f),
        padding2a(0.0f),
        padding2b(0.0f),
        padding2c(0.0f)
    {}

    // Configure for rain
    void setRainConditions(float intensity) {
        globalWetness = intensity;
        puddleRippleStrength = intensity * 0.8f;
    }

    // Configure from LiquidComponent preset
    void setPuddleFromLiquid(const LiquidComponent& liquid) {
        puddleWaterColor = liquid.color;
        puddleRoughness = liquid.roughness;
        maxPuddleDepth = liquid.depth;
    }

    // Configure stream from LiquidComponent
    void setStreamFromLiquid(const LiquidComponent& liquid) {
        streamWaterColor = liquid.color;
        streamFlowSpeed = liquid.flowSpeed;
        streamDepth = liquid.depth;
        streamTurbulence = liquid.scatteringScale * 0.1f;  // Approximate
        streamEnabled = 1.0f;
    }

    // Enable/disable streams
    void enableStream(bool enable) {
        streamEnabled = enable ? 1.0f : 0.0f;
    }

    // Update time for animation
    void updateTime(float deltaTime) {
        time += deltaTime;
    }
};

// Verify std140 alignment
static_assert(sizeof(TerrainLiquidUBO) % 16 == 0, "TerrainLiquidUBO must be 16-byte aligned");

/**
 * TerrainLiquidConfig - Helper for configuring terrain liquid effects
 */
struct TerrainLiquidConfig {
    // Weather presets
    static TerrainLiquidUBO dryConditions() {
        TerrainLiquidUBO ubo;
        ubo.globalWetness = 0.0f;
        return ubo;
    }

    static TerrainLiquidUBO lightRain() {
        TerrainLiquidUBO ubo;
        ubo.globalWetness = 0.3f;
        ubo.puddleRippleStrength = 0.3f;
        ubo.maxPuddleDepth = 0.01f;
        return ubo;
    }

    static TerrainLiquidUBO heavyRain() {
        TerrainLiquidUBO ubo;
        ubo.globalWetness = 0.8f;
        ubo.puddleRippleStrength = 0.8f;
        ubo.maxPuddleDepth = 0.05f;
        ubo.puddleThreshold = 0.3f;  // Puddles form more easily
        return ubo;
    }

    static TerrainLiquidUBO afterRain() {
        TerrainLiquidUBO ubo;
        ubo.globalWetness = 0.5f;
        ubo.puddleRippleStrength = 0.0f;  // No rain currently
        ubo.maxPuddleDepth = 0.03f;
        return ubo;
    }

    // Stream configuration
    static void addStream(TerrainLiquidUBO& ubo, const glm::vec2& flowDir, float speed) {
        ubo.streamFlowDirection = glm::normalize(flowDir);
        ubo.streamFlowSpeed = speed;
        ubo.streamEnabled = 1.0f;
    }
};

} // namespace material
