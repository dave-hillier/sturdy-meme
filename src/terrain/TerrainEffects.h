#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include "core/material/TerrainLiquidUBO.h"
#include "core/material/MaterialLayer.h"

class TerrainBuffers;

/**
 * TerrainEffects - Manages visual effects state for terrain rendering
 *
 * Consolidates management of:
 * - Caustics (underwater light projection)
 * - Liquid effects (puddles, wetness, streams)
 * - Material layer blending (height/slope-based materials)
 * - Snow, cloud shadow, and other effect texture bindings
 *
 * This class owns the CPU-side state and handles UBO updates.
 * Descriptor set updates are handled through provided callbacks.
 */
class TerrainEffects {
public:
    TerrainEffects() = default;

    struct InitInfo {
        uint32_t framesInFlight = 0;
    };

    void init(const InitInfo& info);

    // --- Caustics ---

    // Set caustics parameters and texture
    // Call this when water/caustics system is configured
    void setCausticsParams(float waterLevel, bool enabled);

    // Get caustics state for descriptor updates
    float getCausticsWaterLevel() const { return causticsWaterLevel_; }
    bool isCausticsEnabled() const { return causticsEnabled_; }

    // --- Liquid Effects ---

    // Set global wetness (0-1, from rain intensity)
    void setLiquidWetness(float wetness);

    // Set full liquid configuration
    void setLiquidConfig(const material::TerrainLiquidUBO& config);
    const material::TerrainLiquidUBO& getLiquidConfig() const { return liquidConfig_; }

    // --- Material Layers ---

    // Set material layer configuration (height/slope-based blending)
    void setMaterialLayerStack(const material::MaterialLayerStack& stack);
    const material::MaterialLayerStack& getMaterialLayerStack() const { return materialLayerStack_; }
    const material::MaterialLayerUBO& getMaterialLayerUBO() const { return materialLayerUBO_; }

    // --- Per-frame Updates ---

    // Update animation time and write UBOs to GPU buffers
    // Call this once per frame before rendering
    void updatePerFrame(uint32_t frameIndex, float deltaTime, TerrainBuffers* buffers);

    // Initialize UBO contents in all frame buffers (call after descriptor setup)
    void initializeUBOs(TerrainBuffers* buffers);

private:
    uint32_t framesInFlight_ = 0;

    // Caustics state
    float causticsWaterLevel_ = 0.0f;
    bool causticsEnabled_ = false;
    float causticsTime_ = 0.0f;

    // Liquid effects state
    material::TerrainLiquidUBO liquidConfig_;

    // Material layer state
    material::MaterialLayerStack materialLayerStack_;
    material::MaterialLayerUBO materialLayerUBO_;
};
