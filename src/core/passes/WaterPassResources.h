#pragma once

// ============================================================================
// WaterPassResources.h - Focused resource struct for water pass recording
// ============================================================================
//
// This struct provides only the resources needed by WaterPasses,
// reducing coupling compared to passing the full RendererSystems reference.
//

// Forward declarations
class Profiler;
class WaterSystem;
class WaterTileCull;
class WaterGBuffer;
class SSRSystem;
class PostProcessSystem;

class RendererSystems;

/**
 * Focused resource bundle for WaterPasses.
 *
 * Contains non-owning pointers to all systems needed for water pass recording.
 */
struct WaterPassResources {
    // Profiling
    Profiler* profiler = nullptr;

    // Water systems
    WaterSystem* water = nullptr;
    WaterGBuffer* waterGBuffer = nullptr;
    WaterTileCull* waterTileCull = nullptr;  // Optional
    SSRSystem* ssr = nullptr;

    // HDR buffer access
    PostProcessSystem* postProcess = nullptr;

    /**
     * Factory: Collect resources from RendererSystems.
     */
    static WaterPassResources collect(RendererSystems& systems);

    /**
     * Check if all required resources are present.
     * waterTileCull is optional.
     */
    bool isValid() const {
        return profiler && water && waterGBuffer && ssr && postProcess;
    }

    /**
     * Check if water tile culling is available.
     */
    bool hasWaterTileCull() const { return waterTileCull != nullptr; }
};
