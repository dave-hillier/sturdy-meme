#pragma once

// ============================================================================
// PostPassResources.h - Focused resource struct for post-processing passes
// ============================================================================
//
// This struct provides only the resources needed by PostPasses,
// reducing coupling compared to passing the full RendererSystems reference.
//

// Forward declarations
class Profiler;
class PostProcessSystem;
class BloomSystem;
class BilateralGridSystem;
class HiZSystem;

class RendererSystems;

/**
 * Focused resource bundle for PostPasses.
 *
 * Contains non-owning pointers to all systems needed for post-processing passes.
 */
struct PostPassResources {
    // Profiling
    Profiler* profiler = nullptr;

    // Post-processing systems
    PostProcessSystem* postProcess = nullptr;
    BloomSystem* bloom = nullptr;
    BilateralGridSystem* bilateralGrid = nullptr;
    HiZSystem* hiZ = nullptr;

    /**
     * Factory: Collect resources from RendererSystems.
     */
    static PostPassResources collect(RendererSystems& systems);

    /**
     * Check if all required resources are present.
     */
    bool isValid() const {
        return profiler && postProcess && bloom && bilateralGrid && hiZ;
    }
};
