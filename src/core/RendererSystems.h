#pragma once

#include <memory>
#include <functional>
#include <vulkan/vulkan.h>

#include "InitContext.h"
#include "CoreResources.h"
#include "AtmosphereSystemGroup.h"
#include "VegetationSystemGroup.h"
#include "WaterSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"
#include "scene/SceneCollection.h"

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
class ResizeCoordinator;
class ShadowSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;
class NPCRenderer;
class TimeSystem;
class CelestialCalculator;
class BilateralGridSystem;
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
    // System accessors (for external coordination)
    // ========================================================================

    // Tier 1 - Core rendering
    PostProcessSystem& postProcess() { return *postProcessSystem_; }
    const PostProcessSystem& postProcess() const { return *postProcessSystem_; }
    void setPostProcess(std::unique_ptr<PostProcessSystem> system);
    BloomSystem& bloom() { return *bloomSystem_; }
    const BloomSystem& bloom() const { return *bloomSystem_; }
    void setBloom(std::unique_ptr<BloomSystem> system);
    BilateralGridSystem& bilateralGrid() { return *bilateralGridSystem_; }
    const BilateralGridSystem& bilateralGrid() const { return *bilateralGridSystem_; }
    void setBilateralGrid(std::unique_ptr<BilateralGridSystem> system);
    ShadowSystem& shadow() { return *shadowSystem_; }
    const ShadowSystem& shadow() const { return *shadowSystem_; }
    void setShadow(std::unique_ptr<ShadowSystem> system);
    TerrainSystem& terrain() { return *terrainSystem_; }
    const TerrainSystem& terrain() const { return *terrainSystem_; }
    void setTerrain(std::unique_ptr<TerrainSystem> system);

    // Sky and atmosphere
    SkySystem& sky() { return *skySystem_; }
    const SkySystem& sky() const { return *skySystem_; }
    void setSky(std::unique_ptr<SkySystem> system);
    AtmosphereLUTSystem& atmosphereLUT() { return *atmosphereLUTSystem_; }
    const AtmosphereLUTSystem& atmosphereLUT() const { return *atmosphereLUTSystem_; }
    void setAtmosphereLUT(std::unique_ptr<AtmosphereLUTSystem> system);
    FroxelSystem& froxel() { return *froxelSystem_; }
    const FroxelSystem& froxel() const { return *froxelSystem_; }
    bool hasFroxel() const { return froxelSystem_ != nullptr; }
    void setFroxel(std::unique_ptr<FroxelSystem> system);
    CloudShadowSystem& cloudShadow() { return *cloudShadowSystem_; }
    const CloudShadowSystem& cloudShadow() const { return *cloudShadowSystem_; }
    void setCloudShadow(std::unique_ptr<CloudShadowSystem> system);

    // Environment (grass, wind, weather)
    GrassSystem& grass() { return *grassSystem_; }
    const GrassSystem& grass() const { return *grassSystem_; }
    void setGrass(std::unique_ptr<GrassSystem> system);
    WindSystem& wind() { return *windSystem_; }
    const WindSystem& wind() const { return *windSystem_; }
    void setWind(std::unique_ptr<WindSystem> system);
    DisplacementSystem& displacement() { return *displacementSystem_; }
    const DisplacementSystem& displacement() const { return *displacementSystem_; }
    void setDisplacement(std::unique_ptr<DisplacementSystem> system);
    WeatherSystem& weather() { return *weatherSystem_; }
    const WeatherSystem& weather() const { return *weatherSystem_; }
    void setWeather(std::unique_ptr<WeatherSystem> system);
    LeafSystem& leaf() { return *leafSystem_; }
    const LeafSystem& leaf() const { return *leafSystem_; }
    void setLeaf(std::unique_ptr<LeafSystem> system);

    // Snow
    SnowMaskSystem& snowMask() { return *snowMaskSystem_; }
    const SnowMaskSystem& snowMask() const { return *snowMaskSystem_; }
    void setSnowMask(std::unique_ptr<SnowMaskSystem> system);
    VolumetricSnowSystem& volumetricSnow() { return *volumetricSnowSystem_; }
    const VolumetricSnowSystem& volumetricSnow() const { return *volumetricSnowSystem_; }
    void setVolumetricSnow(std::unique_ptr<VolumetricSnowSystem> system);

    // Water
    WaterSystem& water() { return *waterSystem_; }
    const WaterSystem& water() const { return *waterSystem_; }
    void setWater(std::unique_ptr<WaterSystem> system);
    WaterDisplacement& waterDisplacement() { return *waterDisplacement_; }
    const WaterDisplacement& waterDisplacement() const { return *waterDisplacement_; }
    void setWaterDisplacement(std::unique_ptr<WaterDisplacement> system);
    FlowMapGenerator& flowMap() { return *flowMapGenerator_; }
    const FlowMapGenerator& flowMap() const { return *flowMapGenerator_; }
    void setFlowMap(std::unique_ptr<FlowMapGenerator> system);
    FoamBuffer& foam() { return *foamBuffer_; }
    const FoamBuffer& foam() const { return *foamBuffer_; }
    void setFoam(std::unique_ptr<FoamBuffer> system);
    SSRSystem& ssr() { return *ssrSystem_; }
    const SSRSystem& ssr() const { return *ssrSystem_; }
    void setSSR(std::unique_ptr<SSRSystem> system);
    WaterTileCull& waterTileCull() { return *waterTileCull_; }
    const WaterTileCull& waterTileCull() const { return *waterTileCull_; }
    bool hasWaterTileCull() const { return waterTileCull_ != nullptr; }
    void setWaterTileCull(std::unique_ptr<WaterTileCull> system);
    WaterGBuffer& waterGBuffer() { return *waterGBuffer_; }
    const WaterGBuffer& waterGBuffer() const { return *waterGBuffer_; }
    void setWaterGBuffer(std::unique_ptr<WaterGBuffer> system);

    // Geometry processing
    CatmullClarkSystem& catmullClark() { return *catmullClarkSystem_; }
    const CatmullClarkSystem& catmullClark() const { return *catmullClarkSystem_; }
    void setCatmullClark(std::unique_ptr<CatmullClarkSystem> system);
    ScatterSystem& rocks() { return *rocksSystem_; }
    const ScatterSystem& rocks() const { return *rocksSystem_; }
    void setRocks(std::unique_ptr<ScatterSystem> system);
    TreeSystem* tree() { return treeSystem_.get(); }
    const TreeSystem* tree() const { return treeSystem_.get(); }
    void setTree(std::unique_ptr<TreeSystem> system);
    TreeRenderer* treeRenderer() { return treeRenderer_.get(); }
    const TreeRenderer* treeRenderer() const { return treeRenderer_.get(); }
    void setTreeRenderer(std::unique_ptr<TreeRenderer> renderer);
    TreeLODSystem* treeLOD() { return treeLODSystem_.get(); }
    const TreeLODSystem* treeLOD() const { return treeLODSystem_.get(); }
    void setTreeLOD(std::unique_ptr<TreeLODSystem> system);
    ImpostorCullSystem* impostorCull() { return impostorCullSystem_.get(); }
    const ImpostorCullSystem* impostorCull() const { return impostorCullSystem_.get(); }
    void setImpostorCull(std::unique_ptr<ImpostorCullSystem> system);
    ScatterSystem* detritus() { return detritusSystem_.get(); }
    const ScatterSystem* detritus() const { return detritusSystem_.get(); }
    void setDetritus(std::unique_ptr<ScatterSystem> system);

    // Deferred terrain object generation (trees, rocks, detritus)
    DeferredTerrainObjects* deferredTerrainObjects() { return deferredTerrainObjects_.get(); }
    const DeferredTerrainObjects* deferredTerrainObjects() const { return deferredTerrainObjects_.get(); }
    void setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred);

    // Scene collection for unified material iteration (used by shadow pass)
    SceneCollection& sceneCollection() { return sceneCollection_; }
    const SceneCollection& sceneCollection() const { return sceneCollection_; }

    // Culling and optimization
    HiZSystem& hiZ() { return *hiZSystem_; }
    const HiZSystem& hiZ() const { return *hiZSystem_; }
    void setHiZ(std::unique_ptr<HiZSystem> system);
    GPUSceneBuffer& gpuSceneBuffer() { return *gpuSceneBuffer_; }
    const GPUSceneBuffer& gpuSceneBuffer() const { return *gpuSceneBuffer_; }
    bool hasGPUSceneBuffer() const { return gpuSceneBuffer_ != nullptr; }
    void setGPUSceneBuffer(std::unique_ptr<GPUSceneBuffer> buffer);
    GPUCullPass& gpuCullPass() { return *gpuCullPass_; }
    const GPUCullPass& gpuCullPass() const { return *gpuCullPass_; }
    bool hasGPUCullPass() const { return gpuCullPass_ != nullptr; }
    void setGPUCullPass(std::unique_ptr<GPUCullPass> pass);

    // Scene and resources
    SceneManager& scene() { return *sceneManager_; }
    const SceneManager& scene() const { return *sceneManager_; }
    void setScene(std::unique_ptr<SceneManager> system);
    GlobalBufferManager& globalBuffers() { return *globalBufferManager_; }
    const GlobalBufferManager& globalBuffers() const { return *globalBufferManager_; }
    void setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers);
    ErosionDataLoader& erosionData() { return *erosionDataLoader_; }
    const ErosionDataLoader& erosionData() const { return *erosionDataLoader_; }
    RoadNetworkLoader& roadData() { return *roadNetworkLoader_; }
    const RoadNetworkLoader& roadData() const { return *roadNetworkLoader_; }
    RoadRiverVisualization& roadRiverVis() { return *roadRiverVisualization_; }
    const RoadRiverVisualization& roadRiverVis() const { return *roadRiverVisualization_; }

    // Animation and skinning
    SkinnedMeshRenderer& skinnedMesh() { return *skinnedMeshRenderer_; }
    const SkinnedMeshRenderer& skinnedMesh() const { return *skinnedMeshRenderer_; }
    void setSkinnedMesh(std::unique_ptr<SkinnedMeshRenderer> system);

    // NPC rendering
    NPCRenderer* npcRenderer() { return npcRenderer_.get(); }
    const NPCRenderer* npcRenderer() const { return npcRenderer_.get(); }
    void setNPCRenderer(std::unique_ptr<NPCRenderer> renderer);

    // Tools and debug
    DebugLineSystem& debugLine() { return *debugLineSystem_; }
    const DebugLineSystem& debugLine() const { return *debugLineSystem_; }
    void setDebugLineSystem(std::unique_ptr<DebugLineSystem> system);
    void setProfiler(std::unique_ptr<Profiler> profiler);
    Profiler& profiler() { return *profiler_; }
    const Profiler& profiler() const { return *profiler_; }

    // Coordination
    ResizeCoordinator& resizeCoordinator() { return *resizeCoordinator_; }
    const ResizeCoordinator& resizeCoordinator() const { return *resizeCoordinator_; }
    UBOBuilder& uboBuilder() { return *uboBuilder_; }
    const UBOBuilder& uboBuilder() const { return *uboBuilder_; }

    // Time and celestial
    TimeSystem& time() { return *timeSystem_; }
    const TimeSystem& time() const { return *timeSystem_; }
    CelestialCalculator& celestial() { return *celestialCalculator_; }
    const CelestialCalculator& celestial() const { return *celestialCalculator_; }

    // Environment settings
    EnvironmentSettings& environmentSettings() { return *environmentSettings_; }
    const EnvironmentSettings& environmentSettings() const { return *environmentSettings_; }

    // ========================================================================
    // System group accessors (reduce coupling by grouping related systems)
    // ========================================================================

    /**
     * Get the atmosphere system group (sky, froxel, atmosphereLUT, cloudShadow)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    AtmosphereSystemGroup atmosphere() {
        return AtmosphereSystemGroup{
            skySystem_.get(),
            froxelSystem_.get(),
            atmosphereLUTSystem_.get(),
            cloudShadowSystem_.get()
        };
    }

    /**
     * Get the vegetation system group (grass, wind, displacement, trees, rocks, detritus)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    VegetationSystemGroup vegetation() {
        return VegetationSystemGroup{
            grassSystem_.get(),
            windSystem_.get(),
            displacementSystem_.get(),
            treeSystem_.get(),
            treeRenderer_.get(),
            treeLODSystem_.get(),
            impostorCullSystem_.get(),
            rocksSystem_.get(),
            detritusSystem_.get()
        };
    }

    /**
     * Get the water system group (water surface, displacement, foam, SSR)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    WaterSystemGroup waterGroup() {
        return WaterSystemGroup{
            waterSystem_.get(),
            waterDisplacement_.get(),
            flowMapGenerator_.get(),
            foamBuffer_.get(),
            ssrSystem_.get(),
            waterTileCull_.get(),
            waterGBuffer_.get()
        };
    }

    /**
     * Get the snow/weather system group (snow mask, volumetric, weather, leaves)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    SnowSystemGroup snowGroup() {
        return SnowSystemGroup{
            snowMaskSystem_.get(),
            volumetricSnowSystem_.get(),
            weatherSystem_.get(),
            leafSystem_.get()
        };
    }

    /**
     * Get the geometry system group (Catmull-Clark subdivision, procedural meshes)
     * Returns a lightweight struct with non-owning references to the systems.
     */
    GeometrySystemGroup geometry() {
        return GeometrySystemGroup{
            catmullClarkSystem_.get()
        };
    }

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* physicsDebugRenderer() { return physicsDebugRenderer_.get(); }
    const PhysicsDebugRenderer* physicsDebugRenderer() const { return physicsDebugRenderer_.get(); }
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

private:
    // ECS world reference (not owned - Application owns the world)
    ecs::World* ecsWorld_ = nullptr;

    // Tier 1 - Core rendering systems (initialize first)
    std::unique_ptr<PostProcessSystem> postProcessSystem_;
    std::unique_ptr<BloomSystem> bloomSystem_;
    std::unique_ptr<BilateralGridSystem> bilateralGridSystem_;
    std::unique_ptr<ShadowSystem> shadowSystem_;
    std::unique_ptr<TerrainSystem> terrainSystem_;

    // Tier 2 - Sky and atmosphere
    std::unique_ptr<SkySystem> skySystem_;
    std::unique_ptr<AtmosphereLUTSystem> atmosphereLUTSystem_;
    std::unique_ptr<FroxelSystem> froxelSystem_;
    std::unique_ptr<CloudShadowSystem> cloudShadowSystem_;

    // Tier 2 - Environment
    std::unique_ptr<GrassSystem> grassSystem_;
    std::unique_ptr<WindSystem> windSystem_;
    std::unique_ptr<DisplacementSystem> displacementSystem_;
    std::unique_ptr<WeatherSystem> weatherSystem_;
    std::unique_ptr<LeafSystem> leafSystem_;

    // Tier 2 - Snow
    std::unique_ptr<SnowMaskSystem> snowMaskSystem_;
    std::unique_ptr<VolumetricSnowSystem> volumetricSnowSystem_;

    // Tier 2 - Water
    std::unique_ptr<WaterSystem> waterSystem_;
    std::unique_ptr<WaterDisplacement> waterDisplacement_;
    std::unique_ptr<FlowMapGenerator> flowMapGenerator_;
    std::unique_ptr<FoamBuffer> foamBuffer_;
    std::unique_ptr<SSRSystem> ssrSystem_;
    std::unique_ptr<WaterTileCull> waterTileCull_;
    std::unique_ptr<WaterGBuffer> waterGBuffer_;

    // Tier 2 - Geometry
    std::unique_ptr<CatmullClarkSystem> catmullClarkSystem_;
    std::unique_ptr<ScatterSystem> rocksSystem_;
    std::unique_ptr<TreeSystem> treeSystem_;
    std::unique_ptr<TreeRenderer> treeRenderer_;
    std::unique_ptr<TreeLODSystem> treeLODSystem_;
    std::unique_ptr<ImpostorCullSystem> impostorCullSystem_;
    std::unique_ptr<ScatterSystem> detritusSystem_;

    // Deferred terrain object generation
    std::unique_ptr<DeferredTerrainObjects> deferredTerrainObjects_;

    // Scene collection for unified material iteration
    SceneCollection sceneCollection_;

    // Tier 2 - Culling
    std::unique_ptr<HiZSystem> hiZSystem_;
    std::unique_ptr<GPUSceneBuffer> gpuSceneBuffer_;
    std::unique_ptr<GPUCullPass> gpuCullPass_;

    // Infrastructure (needed throughout)
    std::unique_ptr<SceneManager> sceneManager_;
    std::unique_ptr<GlobalBufferManager> globalBufferManager_;
    std::unique_ptr<ErosionDataLoader> erosionDataLoader_;
    std::unique_ptr<RoadNetworkLoader> roadNetworkLoader_;
    std::unique_ptr<RoadRiverVisualization> roadRiverVisualization_;
    std::unique_ptr<SkinnedMeshRenderer> skinnedMeshRenderer_;
    std::unique_ptr<NPCRenderer> npcRenderer_;

    // Tools and debug
    std::unique_ptr<DebugLineSystem> debugLineSystem_;
    std::unique_ptr<Profiler> profiler_;

    // Coordination
    std::unique_ptr<ResizeCoordinator> resizeCoordinator_;
    std::unique_ptr<UBOBuilder> uboBuilder_;

    // Time and environment
    std::unique_ptr<TimeSystem> timeSystem_;
    std::unique_ptr<CelestialCalculator> celestialCalculator_;
    std::unique_ptr<EnvironmentSettings> environmentSettings_;

#ifdef JPH_DEBUG_RENDERER
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRenderer_;
#endif

    // Control subsystems (only for interfaces that coordinate multiple systems)
    // Note: ILocationControl -> CelestialCalculator, ITerrainControl -> TerrainSystem,
    //       IProfilerControl -> Profiler, IWeatherState -> WeatherSystem,
    //       IPostProcessState -> PostProcessSystem, ICloudShadowControl -> CloudShadowSystem
    //       implement their interfaces directly
    std::unique_ptr<EnvironmentControlSubsystem> environmentControl_;
    std::unique_ptr<WaterControlSubsystem> waterControl_;
    std::unique_ptr<TreeControlSubsystem> treeControl_;
    std::unique_ptr<GrassControlAdapter> grassControl_;
    std::unique_ptr<DebugControlSubsystem> debugControl_;
    std::unique_ptr<PerformanceControlSubsystem> performanceControl_;
    std::unique_ptr<SceneControlSubsystem> sceneControl_;
    std::unique_ptr<PlayerControlSubsystem> playerControl_;

    bool initialized_ = false;
    bool controlsInitialized_ = false;
};
