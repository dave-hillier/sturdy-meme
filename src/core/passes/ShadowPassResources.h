#pragma once

// ============================================================================
// ShadowPassResources.h - Focused resource struct for shadow pass recording
// ============================================================================
//
// This struct provides only the resources needed by ShadowPassRecorder,
// reducing coupling compared to passing the full RendererSystems reference.
//
// Benefits:
// - Explicit dependencies: clear what the shadow pass actually needs
// - Reduced header dependencies: includes only forward declarations
// - Testability: can construct with mock systems for unit testing
//

#include "VegetationSystemGroup.h"

// Forward declarations
class Profiler;
class ShadowSystem;
class TerrainSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;

class RendererSystems;

/**
 * Focused resource bundle for ShadowPassRecorder.
 *
 * Contains non-owning pointers to all systems needed for shadow pass recording.
 * This replaces the previous RendererSystems& dependency with explicit requirements.
 */
struct ShadowPassResources {
    // Profiling
    Profiler* profiler = nullptr;

    // Core shadow rendering
    ShadowSystem* shadow = nullptr;
    TerrainSystem* terrain = nullptr;

    // Vegetation (grass, trees, rocks, detritus)
    VegetationSystemGroup vegetation;

    // Scene objects and infrastructure
    SceneManager* scene = nullptr;
    GlobalBufferManager* globalBuffers = nullptr;
    SkinnedMeshRenderer* skinnedMesh = nullptr;

    /**
     * Factory: Collect resources from RendererSystems.
     * This is the primary way to construct ShadowPassResources.
     */
    static ShadowPassResources collect(RendererSystems& systems);

    /**
     * Check if all required resources are present.
     * Optional systems (trees, detritus) may be null.
     */
    bool isValid() const {
        return profiler && shadow && terrain && vegetation.isValid() &&
               scene && globalBuffers && skinnedMesh;
    }
};
