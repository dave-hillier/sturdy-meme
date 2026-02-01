#pragma once

// ============================================================================
// HDRPassResources.h - Focused resource struct for HDR pass recording
// ============================================================================
//
// This struct provides only the resources needed by HDRPassRecorder,
// reducing coupling compared to passing the full RendererSystems reference.
//

#include "VegetationSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"

// Forward declarations
class Profiler;
class PostProcessSystem;
class SkySystem;
class TerrainSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;
class ShadowSystem;
class WaterSystem;
class WaterTileCull;
class WindSystem;
class DebugLineSystem;
class NPCRenderer;

class RendererSystems;

namespace ecs { class World; }

/**
 * Focused resource bundle for HDRPassRecorder.
 *
 * Contains non-owning pointers to all systems needed for HDR pass recording.
 * Organized by rendering stage for clarity.
 */
struct HDRPassResources {
    // Profiling
    Profiler* profiler = nullptr;

    // Core HDR rendering
    PostProcessSystem* postProcess = nullptr;  // For render pass, framebuffer, extent
    SkySystem* sky = nullptr;
    TerrainSystem* terrain = nullptr;

    // Procedural geometry (Catmull-Clark subdivision)
    GeometrySystemGroup geometry;

    // Scene objects
    SceneManager* scene = nullptr;
    SkinnedMeshRenderer* skinnedMesh = nullptr;
    GlobalBufferManager* globalBuffers = nullptr;
    ShadowSystem* shadow = nullptr;  // For impostor shadow bindings

    // NPC rendering (optional - may be null if no NPCs)
    NPCRenderer* npcRenderer = nullptr;

    // Vegetation (grass, trees, rocks, detritus)
    VegetationSystemGroup vegetation;

    // Water rendering
    WaterSystem* water = nullptr;
    WaterTileCull* waterTileCull = nullptr;  // May be null (optional optimization)

    // Weather effects (snow, rain, leaves)
    SnowSystemGroup snow;

    // Wind (for tree animation time)
    WindSystem* wind = nullptr;

    // Debug visualization
    DebugLineSystem* debugLine = nullptr;

    // ECS world for Phase 6 rendering (optional - if null, uses legacy renderables)
    ecs::World* ecsWorld = nullptr;

    /**
     * Factory: Collect resources from RendererSystems.
     */
    static HDRPassResources collect(RendererSystems& systems);

    /**
     * Check if all required resources are present.
     * Optional systems (waterTileCull, trees) may be null.
     */
    bool isValid() const {
        return profiler && postProcess && sky && terrain &&
               geometry.isValid() && scene && skinnedMesh &&
               globalBuffers && shadow && vegetation.isValid() &&
               water && snow.isValid() && wind && debugLine;
    }

    /**
     * Check if water tile culling is available.
     */
    bool hasWaterTileCull() const { return waterTileCull != nullptr; }
};
