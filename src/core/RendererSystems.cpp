// RendererSystems.cpp - Subsystem lifecycle management
// Groups all rendering subsystems with automatic lifecycle via unique_ptr

#include "RendererSystems.h"
#include "RendererInit.h"
#include "CoreResources.h"

// Include all subsystem headers
#include "SkySystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DetritusSystem.h"
#include "CloudShadowSystem.h"
#include "HiZSystem.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "UBOBuilder.h"
#include "Profiler.h"
#include "DebugLineSystem.h"
#include "ResizeCoordinator.h"
#include "ShadowSystem.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "VulkanContext.h"
#include "PerformanceToggles.h"

// Include control subsystem headers (only those that coordinate multiple systems)
#include "controls/EnvironmentControlSubsystem.h"
#include "controls/WaterControlSubsystem.h"
#include "controls/TreeControlSubsystem.h"
#include "controls/DebugControlSubsystem.h"
#include "controls/PerformanceControlSubsystem.h"
#include "controls/SceneControlSubsystem.h"
#include "controls/PlayerControlSubsystem.h"
// Note: ILocationControl -> CelestialCalculator, ITerrainControl -> TerrainSystem,
//       IProfilerControl -> Profiler, IWeatherState -> WeatherSystem,
//       IPostProcessState -> PostProcessSystem, ICloudShadowControl -> CloudShadowSystem
//       implement their interfaces directly

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <SDL3/SDL_log.h>

RendererSystems::RendererSystems()
    // Tier 1
    // postProcessSystem_ created via factory in RendererInitPhases
    // bloomSystem_ created via factory in RendererInitPhases
    // shadowSystem_ created via factory in RendererInitPhases
    // terrainSystem_ created via factory in RendererInitPhases
    // Tier 2 - Sky/Atmosphere
    // skySystem_ created via factory in RendererInitPhases
    // atmosphereLUTSystem_ created via factory in RendererInitPhases
    // froxelSystem_ created via factory in RendererInitPhases
    // cloudShadowSystem_ created via factory in RendererInitPhases
    // Tier 2 - Environment
    // grassSystem_ created via factory in RendererInitPhases
    // windSystem_ created via factory in RendererInitPhases
    // weatherSystem_ created via factory in RendererInitPhases
    // leafSystem_ created via factory in RendererInitPhases
    // Tier 2 - Snow
    // snowMaskSystem_ created via factory in RendererInitPhases
    // volumetricSnowSystem_ created via factory in RendererInitPhases
    // Tier 2 - Water
    // waterSystem_ created via factory in RendererInitPhases
    // waterDisplacement_ created via factory in RendererInitPhases
    // flowMapGenerator_ created via factory in RendererInitPhases
    // foamBuffer_ created via factory in RendererInitPhases
    // ssrSystem_ created via factory in RendererInit
    // waterTileCull_ created via factory in RendererInitPhases
    // waterGBuffer_ created via factory in RendererInitPhases
    // Tier 2 - Geometry
    // catmullClarkSystem_ created via factory in RendererInitPhases
    // rockSystem_ created via factory in RendererInitPhases
    // Tier 2 - Culling
    // hiZSystem_ created via factory in RendererInit
    // Infrastructure
    // sceneManager_ created via factory in RendererInitPhases
    // globalBufferManager_ created via factory in RendererInitPhases
    : erosionDataLoader_(std::make_unique<ErosionDataLoader>())
    , roadNetworkLoader_(std::make_unique<RoadNetworkLoader>())
    , roadRiverVisualization_(std::make_unique<RoadRiverVisualization>())
    // skinnedMeshRenderer_ created via factory in RendererInitPhases
    // Tools
    // debugLineSystem_ created via factory in RendererInit
    // profiler_ created via Profiler::create() factory in RendererInitPhases
    // Coordination
    , resizeCoordinator_(std::make_unique<ResizeCoordinator>())
    , uboBuilder_(std::make_unique<UBOBuilder>())
    // Time
    , timeSystem_(std::make_unique<TimeSystem>())
    , celestialCalculator_(std::make_unique<CelestialCalculator>())
    , environmentSettings_(std::make_unique<EnvironmentSettings>())
{
}

RendererSystems::~RendererSystems() {
    // unique_ptrs handle destruction automatically in reverse order
    // No manual cleanup needed - this is the benefit of RAII
}

void RendererSystems::setDebugLineSystem(std::unique_ptr<DebugLineSystem> system) {
    debugLineSystem_ = std::move(system);
}

void RendererSystems::setProfiler(std::unique_ptr<Profiler> profiler) {
    profiler_ = std::move(profiler);
}

void RendererSystems::setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers) {
    globalBufferManager_ = std::move(buffers);
}

void RendererSystems::setShadow(std::unique_ptr<ShadowSystem> system) {
    shadowSystem_ = std::move(system);
}

void RendererSystems::setTerrain(std::unique_ptr<TerrainSystem> system) {
    terrainSystem_ = std::move(system);
}

void RendererSystems::setPostProcess(std::unique_ptr<PostProcessSystem> system) {
    postProcessSystem_ = std::move(system);
}

void RendererSystems::setBloom(std::unique_ptr<BloomSystem> system) {
    bloomSystem_ = std::move(system);
}

void RendererSystems::setBilateralGrid(std::unique_ptr<BilateralGridSystem> system) {
    bilateralGridSystem_ = std::move(system);
}

void RendererSystems::setSSR(std::unique_ptr<SSRSystem> system) {
    ssrSystem_ = std::move(system);
}

void RendererSystems::setHiZ(std::unique_ptr<HiZSystem> system) {
    hiZSystem_ = std::move(system);
}

void RendererSystems::setSky(std::unique_ptr<SkySystem> system) {
    skySystem_ = std::move(system);
}

void RendererSystems::setWind(std::unique_ptr<WindSystem> system) {
    windSystem_ = std::move(system);
}

void RendererSystems::setWeather(std::unique_ptr<WeatherSystem> system) {
    weatherSystem_ = std::move(system);
}

void RendererSystems::setGrass(std::unique_ptr<GrassSystem> system) {
    grassSystem_ = std::move(system);
}

void RendererSystems::setFroxel(std::unique_ptr<FroxelSystem> system) {
    froxelSystem_ = std::move(system);
}

void RendererSystems::setAtmosphereLUT(std::unique_ptr<AtmosphereLUTSystem> system) {
    atmosphereLUTSystem_ = std::move(system);
}

void RendererSystems::setCloudShadow(std::unique_ptr<CloudShadowSystem> system) {
    cloudShadowSystem_ = std::move(system);
}

void RendererSystems::setSnowMask(std::unique_ptr<SnowMaskSystem> system) {
    snowMaskSystem_ = std::move(system);
}

void RendererSystems::setVolumetricSnow(std::unique_ptr<VolumetricSnowSystem> system) {
    volumetricSnowSystem_ = std::move(system);
}

void RendererSystems::setLeaf(std::unique_ptr<LeafSystem> system) {
    leafSystem_ = std::move(system);
}

void RendererSystems::setWater(std::unique_ptr<WaterSystem> system) {
    waterSystem_ = std::move(system);
}

void RendererSystems::setWaterDisplacement(std::unique_ptr<WaterDisplacement> system) {
    waterDisplacement_ = std::move(system);
}

void RendererSystems::setFlowMap(std::unique_ptr<FlowMapGenerator> system) {
    flowMapGenerator_ = std::move(system);
}

void RendererSystems::setFoam(std::unique_ptr<FoamBuffer> system) {
    foamBuffer_ = std::move(system);
}

void RendererSystems::setWaterTileCull(std::unique_ptr<WaterTileCull> system) {
    waterTileCull_ = std::move(system);
}

void RendererSystems::setWaterGBuffer(std::unique_ptr<WaterGBuffer> system) {
    waterGBuffer_ = std::move(system);
}

void RendererSystems::setCatmullClark(std::unique_ptr<CatmullClarkSystem> system) {
    catmullClarkSystem_ = std::move(system);
}

void RendererSystems::setRock(std::unique_ptr<RockSystem> system) {
    rockSystem_ = std::move(system);
}

void RendererSystems::setTree(std::unique_ptr<TreeSystem> system) {
    treeSystem_ = std::move(system);
}

void RendererSystems::setTreeRenderer(std::unique_ptr<TreeRenderer> renderer) {
    treeRenderer_ = std::move(renderer);
}

void RendererSystems::setTreeLOD(std::unique_ptr<TreeLODSystem> system) {
    treeLODSystem_ = std::move(system);
}

void RendererSystems::setImpostorCull(std::unique_ptr<ImpostorCullSystem> system) {
    impostorCullSystem_ = std::move(system);
}

void RendererSystems::setDetritus(std::unique_ptr<DetritusSystem> system) {
    detritusSystem_ = std::move(system);
}

void RendererSystems::setScene(std::unique_ptr<SceneManager> system) {
    sceneManager_ = std::move(system);
}

void RendererSystems::setSkinnedMesh(std::unique_ptr<SkinnedMeshRenderer> system) {
    skinnedMeshRenderer_ = std::move(system);
}

bool RendererSystems::init(const InitContext& /*initCtx*/,
                            VkRenderPass /*swapchainRenderPass*/,
                            VkFormat /*swapchainImageFormat*/,
                            VkDescriptorSetLayout /*mainDescriptorSetLayout*/,
                            VkFormat /*depthFormat*/,
                            VkSampler /*depthSampler*/,
                            const std::string& /*resourcePath*/) {
    // NOTE: This centralized init is not currently used.
    // Initialization is done via RendererInitPhases.cpp which calls each subsystem directly.
    // This stub exists for potential future refactoring.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RendererSystems::init() is not implemented - use RendererInitPhases instead");
    return false;
}

void RendererSystems::destroy(VkDevice device, VmaAllocator allocator) {
    // Note: initialized_ flag is not used since initialization is done
    // via RendererInitPhases.cpp, not RendererSystems::init()
    SDL_Log("RendererSystems::destroy starting");

    // Destroy in reverse dependency order
    // Tier 2+ first, then Tier 1

    // debugLineSystem_ cleanup handled by destructor (RAII)
    debugLineSystem_.reset();

    // Water
    waterGBuffer_.reset();  // RAII cleanup via destructor
    waterTileCull_.reset();  // RAII cleanup via destructor
    ssrSystem_.reset();  // RAII cleanup via destructor
    foamBuffer_.reset();  // RAII cleanup via destructor
    flowMapGenerator_.reset();  // RAII cleanup via destructor
    waterDisplacement_.reset();  // RAII cleanup via destructor
    waterSystem_.reset();  // RAII cleanup via destructor

    profiler_.reset();
    hiZSystem_.reset();  // RAII cleanup via destructor

    // Geometry/Vegetation
    detritusSystem_.reset();  // RAII cleanup via destructor
    catmullClarkSystem_.reset();  // RAII cleanup via destructor
    rockSystem_.reset();  // RAII cleanup via destructor
    treeLODSystem_.reset();  // RAII cleanup via destructor
    impostorCullSystem_.reset();  // RAII cleanup via destructor
    treeRenderer_.reset();  // RAII cleanup via destructor
    treeSystem_.reset();  // RAII cleanup via destructor

    // Atmosphere
    cloudShadowSystem_.reset();  // RAII cleanup via destructor
    atmosphereLUTSystem_.reset();  // RAII cleanup via destructor
    froxelSystem_.reset();  // RAII cleanup via destructor

    // Weather/Snow
    leafSystem_.reset();  // RAII cleanup via destructor
    weatherSystem_.reset();  // RAII cleanup via destructor
    volumetricSnowSystem_.reset();  // RAII cleanup via destructor
    snowMaskSystem_.reset();  // RAII cleanup via destructor

    // Grass/Wind
    windSystem_.reset();  // RAII cleanup via destructor
    grassSystem_.reset();  // RAII cleanup via destructor

    sceneManager_.reset();  // RAII cleanup via destructor
    globalBufferManager_.reset();  // RAII cleanup via destructor
    roadRiverVisualization_.reset();  // RAII cleanup via destructor
    roadNetworkLoader_.reset();  // RAII cleanup via destructor

    // Tier 1
    skySystem_.reset();  // RAII cleanup via destructor
    terrainSystem_.reset();  // RAII cleanup via destructor
    shadowSystem_.reset();  // RAII cleanup via destructor
    skinnedMeshRenderer_.reset();  // RAII cleanup via destructor
    bilateralGridSystem_.reset();  // RAII cleanup via destructor
    bloomSystem_.reset();  // RAII cleanup via destructor
    postProcessSystem_.reset();  // RAII cleanup via destructor

    SDL_Log("RendererSystems::destroy complete");
}

CoreResources RendererSystems::getCoreResources(uint32_t framesInFlight) const {
    return CoreResources::collect(*postProcessSystem_, *shadowSystem_,
                                  *terrainSystem_, framesInFlight);
}

#ifdef JPH_DEBUG_RENDERER
void RendererSystems::createPhysicsDebugRenderer(const InitContext& /*ctx*/, VkRenderPass /*hdrRenderPass*/) {
    physicsDebugRenderer_ = std::make_unique<PhysicsDebugRenderer>();
    physicsDebugRenderer_->init();
}
#endif

// ============================================================================
// Control Subsystem Implementation
// ============================================================================

void RendererSystems::initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles) {
    // Systems that directly implement their interfaces:
    // - CelestialCalculator implements ILocationControl
    // - TerrainSystem implements ITerrainControl
    // - Profiler implements IProfilerControl
    // - WeatherSystem implements IWeatherState
    // - PostProcessSystem implements IPostProcessState
    // - CloudShadowSystem implements ICloudShadowControl

    // Subsystems that coordinate multiple systems:
    environmentControl_ = std::make_unique<EnvironmentControlSubsystem>(
        *froxelSystem_, *atmosphereLUTSystem_, *leafSystem_, *cloudShadowSystem_,
        *postProcessSystem_, *environmentSettings_);
    waterControl_ = std::make_unique<WaterControlSubsystem>(*waterSystem_, *waterTileCull_);
    treeControl_ = std::make_unique<TreeControlSubsystem>(treeSystem_.get(), *this);
    debugControl_ = std::make_unique<DebugControlSubsystem>(*debugLineSystem_, *hiZSystem_, *this);
    performanceControl_ = std::make_unique<PerformanceControlSubsystem>(perfToggles, nullptr);
    sceneControl_ = std::make_unique<SceneControlSubsystem>(*sceneManager_, vulkanContext);
    playerControl_ = std::make_unique<PlayerControlSubsystem>(*sceneManager_);

    controlsInitialized_ = true;
    SDL_Log("Control subsystems initialized");
}

void RendererSystems::setPerformanceSyncCallback(std::function<void()> callback) {
    if (performanceControl_) {
        performanceControl_->setSyncCallback(callback);
    }
}

// Control subsystem accessors
// Systems that directly implement their interfaces:
ILocationControl& RendererSystems::locationControl() { return *celestialCalculator_; }
const ILocationControl& RendererSystems::locationControl() const { return *celestialCalculator_; }

IWeatherState& RendererSystems::weatherState() { return *weatherSystem_; }
const IWeatherState& RendererSystems::weatherState() const { return *weatherSystem_; }

IEnvironmentControl& RendererSystems::environmentControl() { return *environmentControl_; }
const IEnvironmentControl& RendererSystems::environmentControl() const { return *environmentControl_; }

IPostProcessState& RendererSystems::postProcessState() { return *postProcessSystem_; }
const IPostProcessState& RendererSystems::postProcessState() const { return *postProcessSystem_; }

ICloudShadowControl& RendererSystems::cloudShadowControl() { return *cloudShadowSystem_; }
const ICloudShadowControl& RendererSystems::cloudShadowControl() const { return *cloudShadowSystem_; }

ITerrainControl& RendererSystems::terrainControl() { return *terrainSystem_; }
const ITerrainControl& RendererSystems::terrainControl() const { return *terrainSystem_; }

IWaterControl& RendererSystems::waterControl() { return *waterControl_; }
const IWaterControl& RendererSystems::waterControl() const { return *waterControl_; }

ITreeControl& RendererSystems::treeControl() { return *treeControl_; }
const ITreeControl& RendererSystems::treeControl() const { return *treeControl_; }

IDebugControl& RendererSystems::debugControl() { return *debugControl_; }
const IDebugControl& RendererSystems::debugControl() const { return *debugControl_; }

IProfilerControl& RendererSystems::profilerControl() { return *profiler_; }
const IProfilerControl& RendererSystems::profilerControl() const { return *profiler_; }

IPerformanceControl& RendererSystems::performanceControl() { return *performanceControl_; }
const IPerformanceControl& RendererSystems::performanceControl() const { return *performanceControl_; }

ISceneControl& RendererSystems::sceneControl() { return *sceneControl_; }
const ISceneControl& RendererSystems::sceneControl() const { return *sceneControl_; }

IPlayerControl& RendererSystems::playerControl() { return *playerControl_; }
const IPlayerControl& RendererSystems::playerControl() const { return *playerControl_; }
