#pragma once

#include <memory>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

#include "InitContext.h"
#include "CoreResources.h"
#include "SystemRegistry.h"
#include "AtmosphereSystemGroup.h"
#include "VegetationSystemGroup.h"
#include "WaterSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"
#include "scene/SceneCollection.h"
#include "interfaces/ITemporalSystem.h"

// Forward declarations for control subsystems (only those that coordinate multiple systems)
class EnvironmentControlSubsystem;
class WaterControlSubsystem;
class TreeControlSubsystem;
class GrassControlAdapter;
class DebugControlSubsystem;
class PerformanceControlSubsystem;
class SceneControlSubsystem;
class PlayerControlSubsystem;

namespace ecs { class World; }

// Forward declarations for interfaces
class ILocationControl;
class IWeatherState;
class IEnvironmentControl;
class IPostProcessState;
class ICloudShadowControl;
class ITerrainControl;
class IWaterControl;
class ITreeControl;
class IGrassControl;
class IDebugControl;
class IProfilerControl;
class IPerformanceControl;
class ISceneControl;
class IPlayerControl;

class VulkanContext;
struct PerformanceToggles;

// Forward declarations for all subsystems
class SkySystem;
class GrassSystem;
class WindSystem;
class DisplacementSystem;
class WeatherSystem;
class LeafSystem;
class PostProcessSystem;
class BloomSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class TerrainSystem;
class CatmullClarkSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class ScatterSystem;
class TreeSystem;
class TreeRenderer;
class TreeLODSystem;
class ImpostorCullSystem;
class CloudShadowSystem;
class HiZSystem;
class GPUSceneBuffer;
class GPUCullPass;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class ErosionDataLoader;
class RoadNetworkLoader;
class RoadRiverVisualization;
class UBOBuilder;
class Profiler;
class DebugLineSystem;
class ShadowSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;
class NPCRenderer;
class TimeSystem;
class CelestialCalculator;
class BilateralGridSystem;
class ScreenSpaceShadowSystem;
class GodRaysSystem;
class DeferredTerrainObjects;
struct EnvironmentSettings;
struct TerrainConfig;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

/**
 * RendererSystems - Owns all rendering subsystems with automatic lifecycle management
 *
 * Design goals:
 * - Groups related systems together
 * - Uses unique_ptr for automatic cleanup
 * - Reduces Renderer's direct knowledge of subsystem internals
 * - Provides typed access when needed
 *
 * Lifecycle:
 * - Create with create() factory method
 * - Initialize with init() after VulkanContext is ready
 * - Destroy automatically via destructor
 */
class RendererSystems {
public:
    RendererSystems();
    ~RendererSystems();

    // Non-copyable, non-movable (owns complex GPU resources)
    RendererSystems(const RendererSystems&) = delete;
    RendererSystems& operator=(const RendererSystems&) = delete;
    RendererSystems(RendererSystems&&) = delete;
    RendererSystems& operator=(RendererSystems&&) = delete;

    /**
     * Initialize all subsystems in proper dependency order
     * Returns false if any critical system fails to initialize
     */
    bool init(const InitContext& initCtx,
              VkRenderPass swapchainRenderPass,
              VkFormat swapchainImageFormat,
              VkDescriptorSetLayout mainDescriptorSetLayout,
              VkFormat depthFormat,
              VkSampler depthSampler,
              const std::string& resourcePath);

    /**
     * Destroy all subsystems in reverse dependency order
     */
    void destroy(VkDevice device, VmaAllocator allocator);

    /**
     * Get tier-1 core resources for dependent system initialization
     * Only valid after init() completes Phase 1
     */
    CoreResources getCoreResources(uint32_t framesInFlight) const;

    // ========================================================================
    // System accessors - delegate to SystemRegistry
    // These maintain the existing API while storage is handled by registry_.
    // ========================================================================

    // Tag types for disambiguating multiple instances of the same type
    struct RocksTag {};
    struct DetritusTag {};

    // Type-indexed system registry (new systems can be added without modifying this file)
    SystemRegistry& registry() { return registry_; }
    const SystemRegistry& registry() const { return registry_; }

    // Tier 1 - Core rendering
    PostProcessSystem& postProcess() { return registry_.get<PostProcessSystem>(); }
    const PostProcessSystem& postProcess() const { return registry_.get<PostProcessSystem>(); }
    void setPostProcess(std::unique_ptr<PostProcessSystem> system);
    BloomSystem& bloom() { return registry_.get<BloomSystem>(); }
    const BloomSystem& bloom() const { return registry_.get<BloomSystem>(); }
    void setBloom(std::unique_ptr<BloomSystem> system);
    BilateralGridSystem& bilateralGrid() { return registry_.get<BilateralGridSystem>(); }
    const BilateralGridSystem& bilateralGrid() const { return registry_.get<BilateralGridSystem>(); }
    void setBilateralGrid(std::unique_ptr<BilateralGridSystem> system);
    GodRaysSystem& godRays() { return registry_.get<GodRaysSystem>(); }
    const GodRaysSystem& godRays() const { return registry_.get<GodRaysSystem>(); }
    bool hasGodRays() const { return registry_.has<GodRaysSystem>(); }
    void setGodRays(std::unique_ptr<GodRaysSystem> system);
    ShadowSystem& shadow() { return registry_.get<ShadowSystem>(); }
    const ShadowSystem& shadow() const { return registry_.get<ShadowSystem>(); }
    void setShadow(std::unique_ptr<ShadowSystem> system);
    TerrainSystem& terrain() { return registry_.get<TerrainSystem>(); }
    const TerrainSystem& terrain() const { return registry_.get<TerrainSystem>(); }
    bool hasTerrain() const { return registry_.has<TerrainSystem>(); }
    TerrainSystem* terrainPtr() { return registry_.find<TerrainSystem>(); }
    const TerrainSystem* terrainPtr() const { return registry_.find<TerrainSystem>(); }
    void setTerrain(std::unique_ptr<TerrainSystem> system);

    // Sky and atmosphere
    SkySystem& sky() { return registry_.get<SkySystem>(); }
    const SkySystem& sky() const { return registry_.get<SkySystem>(); }
    void setSky(std::unique_ptr<SkySystem> system);
    AtmosphereLUTSystem& atmosphereLUT() { return registry_.get<AtmosphereLUTSystem>(); }
    const AtmosphereLUTSystem& atmosphereLUT() const { return registry_.get<AtmosphereLUTSystem>(); }
    void setAtmosphereLUT(std::unique_ptr<AtmosphereLUTSystem> system);
    FroxelSystem& froxel() { return registry_.get<FroxelSystem>(); }
    const FroxelSystem& froxel() const { return registry_.get<FroxelSystem>(); }
    bool hasFroxel() const { return registry_.has<FroxelSystem>(); }
    void setFroxel(std::unique_ptr<FroxelSystem> system);
    CloudShadowSystem& cloudShadow() { return registry_.get<CloudShadowSystem>(); }
    const CloudShadowSystem& cloudShadow() const { return registry_.get<CloudShadowSystem>(); }
    void setCloudShadow(std::unique_ptr<CloudShadowSystem> system);

    // Environment (grass, wind, weather)
    GrassSystem& grass() { return registry_.get<GrassSystem>(); }
    const GrassSystem& grass() const { return registry_.get<GrassSystem>(); }
    void setGrass(std::unique_ptr<GrassSystem> system);
    WindSystem& wind() { return registry_.get<WindSystem>(); }
    const WindSystem& wind() const { return registry_.get<WindSystem>(); }
    void setWind(std::unique_ptr<WindSystem> system);
    DisplacementSystem& displacement() { return registry_.get<DisplacementSystem>(); }
    const DisplacementSystem& displacement() const { return registry_.get<DisplacementSystem>(); }
    void setDisplacement(std::unique_ptr<DisplacementSystem> system);
    WeatherSystem& weather() { return registry_.get<WeatherSystem>(); }
    const WeatherSystem& weather() const { return registry_.get<WeatherSystem>(); }
    void setWeather(std::unique_ptr<WeatherSystem> system);
    LeafSystem& leaf() { return registry_.get<LeafSystem>(); }
    const LeafSystem& leaf() const { return registry_.get<LeafSystem>(); }
    void setLeaf(std::unique_ptr<LeafSystem> system);

    // Snow
    SnowMaskSystem& snowMask() { return registry_.get<SnowMaskSystem>(); }
    const SnowMaskSystem& snowMask() const { return registry_.get<SnowMaskSystem>(); }
    void setSnowMask(std::unique_ptr<SnowMaskSystem> system);
    VolumetricSnowSystem& volumetricSnow() { return registry_.get<VolumetricSnowSystem>(); }
    const VolumetricSnowSystem& volumetricSnow() const { return registry_.get<VolumetricSnowSystem>(); }
    void setVolumetricSnow(std::unique_ptr<VolumetricSnowSystem> system);

    // Water
    WaterSystem& water() { return registry_.get<WaterSystem>(); }
    const WaterSystem& water() const { return registry_.get<WaterSystem>(); }
    void setWater(std::unique_ptr<WaterSystem> system);
    WaterDisplacement& waterDisplacement() { return registry_.get<WaterDisplacement>(); }
    const WaterDisplacement& waterDisplacement() const { return registry_.get<WaterDisplacement>(); }
    void setWaterDisplacement(std::unique_ptr<WaterDisplacement> system);
    FlowMapGenerator& flowMap() { return registry_.get<FlowMapGenerator>(); }
    const FlowMapGenerator& flowMap() const { return registry_.get<FlowMapGenerator>(); }
    void setFlowMap(std::unique_ptr<FlowMapGenerator> system);
    FoamBuffer& foam() { return registry_.get<FoamBuffer>(); }
    const FoamBuffer& foam() const { return registry_.get<FoamBuffer>(); }
    void setFoam(std::unique_ptr<FoamBuffer> system);
    SSRSystem& ssr() { return registry_.get<SSRSystem>(); }
    const SSRSystem& ssr() const { return registry_.get<SSRSystem>(); }
    void setSSR(std::unique_ptr<SSRSystem> system);
    WaterTileCull& waterTileCull() { return registry_.get<WaterTileCull>(); }
    const WaterTileCull& waterTileCull() const { return registry_.get<WaterTileCull>(); }
    bool hasWaterTileCull() const { return registry_.has<WaterTileCull>(); }
    void setWaterTileCull(std::unique_ptr<WaterTileCull> system);
    WaterGBuffer& waterGBuffer() { return registry_.get<WaterGBuffer>(); }
    const WaterGBuffer& waterGBuffer() const { return registry_.get<WaterGBuffer>(); }
    void setWaterGBuffer(std::unique_ptr<WaterGBuffer> system);

    // Geometry processing
    CatmullClarkSystem& catmullClark() { return registry_.get<CatmullClarkSystem>(); }
    const CatmullClarkSystem& catmullClark() const { return registry_.get<CatmullClarkSystem>(); }
    void setCatmullClark(std::unique_ptr<CatmullClarkSystem> system);
    ScatterSystem& rocks() { return registry_.get<ScatterSystem, RocksTag>(); }
    const ScatterSystem& rocks() const { return registry_.get<ScatterSystem, RocksTag>(); }
    void setRocks(std::unique_ptr<ScatterSystem> system);
    TreeSystem* tree() { return registry_.find<TreeSystem>(); }
    const TreeSystem* tree() const { return registry_.find<TreeSystem>(); }
    void setTree(std::unique_ptr<TreeSystem> system);
    TreeRenderer* treeRenderer() { return registry_.find<TreeRenderer>(); }
    const TreeRenderer* treeRenderer() const { return registry_.find<TreeRenderer>(); }
    void setTreeRenderer(std::unique_ptr<TreeRenderer> renderer);
    TreeLODSystem* treeLOD() { return registry_.find<TreeLODSystem>(); }
    const TreeLODSystem* treeLOD() const { return registry_.find<TreeLODSystem>(); }
    void setTreeLOD(std::unique_ptr<TreeLODSystem> system);
    ImpostorCullSystem* impostorCull() { return registry_.find<ImpostorCullSystem>(); }
    const ImpostorCullSystem* impostorCull() const { return registry_.find<ImpostorCullSystem>(); }
    void setImpostorCull(std::unique_ptr<ImpostorCullSystem> system);
    ScatterSystem* detritus() { return registry_.find<ScatterSystem, DetritusTag>(); }
    const ScatterSystem* detritus() const { return registry_.find<ScatterSystem, DetritusTag>(); }
    void setDetritus(std::unique_ptr<ScatterSystem> system);

    // Deferred terrain object generation (trees, rocks, detritus)
    DeferredTerrainObjects* deferredTerrainObjects() { return registry_.find<DeferredTerrainObjects>(); }
    const DeferredTerrainObjects* deferredTerrainObjects() const { return registry_.find<DeferredTerrainObjects>(); }
    void setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred);

    // Scene collection for unified material iteration (used by shadow pass)
    SceneCollection& sceneCollection() { return sceneCollection_; }
    const SceneCollection& sceneCollection() const { return sceneCollection_; }

    // Culling and optimization
    HiZSystem& hiZ() { return registry_.get<HiZSystem>(); }
    const HiZSystem& hiZ() const { return registry_.get<HiZSystem>(); }
    void setHiZ(std::unique_ptr<HiZSystem> system);
    GPUSceneBuffer& gpuSceneBuffer() { return registry_.get<GPUSceneBuffer>(); }
    const GPUSceneBuffer& gpuSceneBuffer() const { return registry_.get<GPUSceneBuffer>(); }
    bool hasGPUSceneBuffer() const { return registry_.has<GPUSceneBuffer>(); }
    void setGPUSceneBuffer(std::unique_ptr<GPUSceneBuffer> buffer);
    GPUCullPass& gpuCullPass() { return registry_.get<GPUCullPass>(); }
    const GPUCullPass& gpuCullPass() const { return registry_.get<GPUCullPass>(); }
    bool hasGPUCullPass() const { return registry_.has<GPUCullPass>(); }
    void setGPUCullPass(std::unique_ptr<GPUCullPass> pass);

    // Screen-space shadow buffer
    ScreenSpaceShadowSystem* screenSpaceShadow() { return registry_.find<ScreenSpaceShadowSystem>(); }
    const ScreenSpaceShadowSystem* screenSpaceShadow() const { return registry_.find<ScreenSpaceShadowSystem>(); }
    bool hasScreenSpaceShadow() const { return registry_.has<ScreenSpaceShadowSystem>(); }
    void setScreenSpaceShadow(std::unique_ptr<ScreenSpaceShadowSystem> system);

    // Scene and resources
    SceneManager& scene() { return registry_.get<SceneManager>(); }
    const SceneManager& scene() const { return registry_.get<SceneManager>(); }
    SceneManager* scenePtr() { return registry_.find<SceneManager>(); }
    const SceneManager* scenePtr() const { return registry_.find<SceneManager>(); }
    void setScene(std::unique_ptr<SceneManager> system);
    GlobalBufferManager& globalBuffers() { return registry_.get<GlobalBufferManager>(); }
    const GlobalBufferManager& globalBuffers() const { return registry_.get<GlobalBufferManager>(); }
    void setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers);
    ErosionDataLoader& erosionData() { return registry_.get<ErosionDataLoader>(); }
    const ErosionDataLoader& erosionData() const { return registry_.get<ErosionDataLoader>(); }
    RoadNetworkLoader& roadData() { return registry_.get<RoadNetworkLoader>(); }
    const RoadNetworkLoader& roadData() const { return registry_.get<RoadNetworkLoader>(); }
    RoadRiverVisualization& roadRiverVis() { return registry_.get<RoadRiverVisualization>(); }
    const RoadRiverVisualization& roadRiverVis() const { return registry_.get<RoadRiverVisualization>(); }

    // Animation and skinning
    SkinnedMeshRenderer& skinnedMesh() { return registry_.get<SkinnedMeshRenderer>(); }
    const SkinnedMeshRenderer& skinnedMesh() const { return registry_.get<SkinnedMeshRenderer>(); }
    void setSkinnedMesh(std::unique_ptr<SkinnedMeshRenderer> system);

    // NPC rendering
    NPCRenderer* npcRenderer() { return registry_.find<NPCRenderer>(); }
    const NPCRenderer* npcRenderer() const { return registry_.find<NPCRenderer>(); }
    void setNPCRenderer(std::unique_ptr<NPCRenderer> renderer);

    // Tools and debug
    DebugLineSystem& debugLine() { return registry_.get<DebugLineSystem>(); }
    const DebugLineSystem& debugLine() const { return registry_.get<DebugLineSystem>(); }
    void setDebugLineSystem(std::unique_ptr<DebugLineSystem> system);
    void setProfiler(std::unique_ptr<Profiler> profiler);
    Profiler& profiler() { return registry_.get<Profiler>(); }
    const Profiler& profiler() const { return registry_.get<Profiler>(); }

    // Coordination
    UBOBuilder& uboBuilder() { return registry_.get<UBOBuilder>(); }
    const UBOBuilder& uboBuilder() const { return registry_.get<UBOBuilder>(); }

    // Time and celestial
    TimeSystem& time() { return registry_.get<TimeSystem>(); }
    const TimeSystem& time() const { return registry_.get<TimeSystem>(); }
    CelestialCalculator& celestial() { return registry_.get<CelestialCalculator>(); }
    const CelestialCalculator& celestial() const { return registry_.get<CelestialCalculator>(); }

    // Environment settings
    EnvironmentSettings& environmentSettings() { return registry_.get<EnvironmentSettings>(); }
    const EnvironmentSettings& environmentSettings() const { return registry_.get<EnvironmentSettings>(); }

    // ========================================================================
    // System group accessors (reduce coupling by grouping related systems)
    // ========================================================================

    /**
     * Get the atmosphere system group (sky, froxel, atmosphereLUT, cloudShadow)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    AtmosphereSystemGroup atmosphere() {
        return AtmosphereSystemGroup{
            registry_.find<SkySystem>(),
            registry_.find<FroxelSystem>(),
            registry_.find<AtmosphereLUTSystem>(),
            registry_.find<CloudShadowSystem>()
        };
    }

    /**
     * Get the vegetation system group (grass, wind, displacement, trees, rocks, detritus)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    VegetationSystemGroup vegetation() {
        return VegetationSystemGroup{
            registry_.find<GrassSystem>(),
            registry_.find<WindSystem>(),
            registry_.find<DisplacementSystem>(),
            registry_.find<TreeSystem>(),
            registry_.find<TreeRenderer>(),
            registry_.find<TreeLODSystem>(),
            registry_.find<ImpostorCullSystem>(),
            registry_.find<ScatterSystem, RocksTag>(),
            registry_.find<ScatterSystem, DetritusTag>()
        };
    }

    /**
     * Get the water system group (water surface, displacement, foam, SSR)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    WaterSystemGroup waterGroup() {
        return WaterSystemGroup{
            registry_.find<WaterSystem>(),
            registry_.find<WaterDisplacement>(),
            registry_.find<FlowMapGenerator>(),
            registry_.find<FoamBuffer>(),
            registry_.find<SSRSystem>(),
            registry_.find<WaterTileCull>(),
            registry_.find<WaterGBuffer>()
        };
    }

    /**
     * Get the snow/weather system group (snow mask, volumetric, weather, leaves)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    SnowSystemGroup snowGroup() {
        return SnowSystemGroup{
            registry_.find<SnowMaskSystem>(),
            registry_.find<VolumetricSnowSystem>(),
            registry_.find<WeatherSystem>(),
            registry_.find<LeafSystem>()
        };
    }

    /**
     * Get the geometry system group (Catmull-Clark subdivision, procedural meshes)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    GeometrySystemGroup geometry() {
        return GeometrySystemGroup{
            registry_.find<CatmullClarkSystem>()
        };
    }

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* physicsDebugRenderer() { return registry_.find<PhysicsDebugRenderer>(); }
    const PhysicsDebugRenderer* physicsDebugRenderer() const { return registry_.find<PhysicsDebugRenderer>(); }
    void createPhysicsDebugRenderer(const InitContext& ctx, VkRenderPass hdrRenderPass);
#endif

    // ========================================================================
    // Control subsystem accessors (implement GUI-facing interfaces)
    // ========================================================================

    /**
     * Initialize control subsystems after all other subsystems are ready.
     * Must be called after init() and after VulkanContext is available.
     */
    void initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles);

    // Control subsystem accessors - return interface references
    ILocationControl& locationControl();
    const ILocationControl& locationControl() const;
    IWeatherState& weatherState();
    const IWeatherState& weatherState() const;
    IEnvironmentControl& environmentControl();
    const IEnvironmentControl& environmentControl() const;
    IPostProcessState& postProcessState();
    const IPostProcessState& postProcessState() const;
    ICloudShadowControl& cloudShadowControl();
    const ICloudShadowControl& cloudShadowControl() const;
    ITerrainControl& terrainControl();
    const ITerrainControl& terrainControl() const;
    IWaterControl& waterControl();
    const IWaterControl& waterControl() const;
    ITreeControl& treeControl();
    const ITreeControl& treeControl() const;
    IGrassControl& grassControl();
    const IGrassControl& grassControl() const;
    IDebugControl& debugControl();
    const IDebugControl& debugControl() const;
    // Direct access for internal callers that need concrete type
    DebugControlSubsystem& debugControlSubsystem();
    const DebugControlSubsystem& debugControlSubsystem() const;
    IProfilerControl& profilerControl();
    const IProfilerControl& profilerControl() const;
    IPerformanceControl& performanceControl();
    const IPerformanceControl& performanceControl() const;
    ISceneControl& sceneControl();
    const ISceneControl& sceneControl() const;
    IPlayerControl& playerControl();
    const IPlayerControl& playerControl() const;

    // Set the sync callback for performance control (must be called after initControlSubsystems)
    void setPerformanceSyncCallback(std::function<void()> callback);

    // ========================================================================
    // ECS Integration (Phase 6: Renderable Elimination)
    // ========================================================================

    /**
     * Set the ECS world reference for render passes to query entities directly.
     * Must be called after the ECS world is created in Application.
     * Pass nullptr to disable ECS rendering and fall back to legacy renderables.
     */
    void setECSWorld(ecs::World* world) { ecsWorld_ = world; }

    /**
     * Get the ECS world (may be null if not set).
     */
    ecs::World* ecsWorld() { return ecsWorld_; }
    const ecs::World* ecsWorld() const { return ecsWorld_; }

    // ========================================================================
    // Temporal system management
    // ========================================================================

    /**
     * Register a system that has temporal state needing reset on window focus.
     * Systems implementing ITemporalSystem should be registered here.
     * This is called automatically during init() for known temporal systems.
     */
    void registerTemporalSystem(ITemporalSystem* system);

    /**
     * Reset all registered temporal systems.
     * Call this when the window regains focus to prevent ghost frames.
     */
    void resetAllTemporalHistory();

    /**
     * Get the number of registered temporal systems (for diagnostics).
     */
    size_t getTemporalSystemCount() const { return temporalSystems_.size(); }

private:
    // Type-indexed system storage - all subsystems live here
    SystemRegistry registry_;

    // ECS world reference (not owned - Application owns the world)
    ecs::World* ecsWorld_ = nullptr;

    // Scene collection for unified material iteration (not a system, just bookkeeping)
    SceneCollection sceneCollection_;

    bool initialized_ = false;
    bool controlsInitialized_ = false;

    // Temporal systems registry - non-owning pointers to systems that need reset on window focus
    std::vector<ITemporalSystem*> temporalSystems_;
};
