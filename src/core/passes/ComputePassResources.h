#pragma once

// ============================================================================
// ComputePassResources.h - Focused resource struct for compute pass recording
// ============================================================================
//
// This struct provides only the resources needed by ComputePasses,
// reducing coupling compared to passing the full RendererSystems reference.
//

// Forward declarations
class Profiler;
class PostProcessSystem;
class GlobalBufferManager;
class TerrainSystem;
class CatmullClarkSystem;
class DisplacementSystem;
class GrassSystem;
class WeatherSystem;
class LeafSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class TreeSystem;
class TreeRenderer;
class TreeLODSystem;
class ImpostorCullSystem;
class HiZSystem;
class FlowMapGenerator;
class FoamBuffer;
class CloudShadowSystem;
class WindSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class ShadowSystem;

class RendererSystems;

/**
 * Focused resource bundle for ComputePasses.
 *
 * Contains non-owning pointers to all systems needed for compute pass recording.
 */
struct ComputePassResources {
    // Profiling
    Profiler* profiler = nullptr;

    // Core systems
    PostProcessSystem* postProcess = nullptr;
    GlobalBufferManager* globalBuffers = nullptr;
    ShadowSystem* shadow = nullptr;

    // Terrain
    TerrainSystem* terrain = nullptr;

    // Geometry
    CatmullClarkSystem* catmullClark = nullptr;

    // Vegetation
    DisplacementSystem* displacement = nullptr;
    GrassSystem* grass = nullptr;

    // Weather and snow
    WeatherSystem* weather = nullptr;
    LeafSystem* leaf = nullptr;
    SnowMaskSystem* snowMask = nullptr;
    VolumetricSnowSystem* volumetricSnow = nullptr;

    // Trees (all optional)
    TreeSystem* tree = nullptr;
    TreeRenderer* treeRenderer = nullptr;
    TreeLODSystem* treeLOD = nullptr;
    ImpostorCullSystem* impostorCull = nullptr;

    // Utility systems
    HiZSystem* hiZ = nullptr;
    FlowMapGenerator* flowMap = nullptr;
    FoamBuffer* foam = nullptr;
    CloudShadowSystem* cloudShadow = nullptr;
    WindSystem* wind = nullptr;

    // Atmosphere
    FroxelSystem* froxel = nullptr;
    AtmosphereLUTSystem* atmosphereLUT = nullptr;

    /**
     * Factory: Collect resources from RendererSystems.
     */
    static ComputePassResources collect(RendererSystems& systems);

    /**
     * Check if all required resources are present.
     * Tree systems are optional.
     */
    bool isValid() const {
        return profiler && postProcess && globalBuffers && shadow &&
               terrain && catmullClark && displacement && grass &&
               weather && leaf && snowMask && volumetricSnow &&
               hiZ && flowMap && foam && cloudShadow && wind &&
               froxel && atmosphereLUT;
    }

    // Tree system availability checks
    bool hasTree() const { return tree != nullptr; }
    bool hasTreeRenderer() const { return treeRenderer != nullptr; }
    bool hasTreeLOD() const { return treeLOD != nullptr; }
    bool hasImpostorCull() const { return impostorCull != nullptr; }
};
