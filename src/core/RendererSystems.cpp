// RendererSystems.cpp - Subsystem lifecycle management
// All subsystems are stored in SystemRegistry (type-indexed via EnTT ctx).
// Setters delegate to registry_.add<T>(), getters are inline in the header.

#include "RendererSystems.h"
#include "CoreResources.h"

// Needed for constructor (registry_.emplace<T>())
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "UBOBuilder.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"

// Needed for non-trivial setters (sceneCollection_ interaction)
#include "ScatterSystem.h"
#include "scene/SceneMaterial.h"

// Needed for remaining trivial setters (non-grouped systems)
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "GodRaysSystem.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "DeferredTerrainObjects.h"
#include "HiZSystem.h"
#include "ScreenSpaceShadowSystem.h"
#include "GPUSceneBuffer.h"
#include "culling/GPUCullPass.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "npc/NPCRenderer.h"
#include "DebugLineSystem.h"
#include "Profiler.h"

// Needed for control subsystem creation (registry_.get<T>() requires complete types for casting)
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "LeafSystem.h"
#include "CloudShadowSystem.h"
#include "WeatherSystem.h"
#include "GrassSystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "TreeSystem.h"
#include "VulkanContext.h"
#include "PerformanceToggles.h"

// Include control subsystem headers
#include "controls/EnvironmentControlSubsystem.h"
#include "controls/WaterControlSubsystem.h"
#include "controls/TreeControlSubsystem.h"
#include "vegetation/GrassControlAdapter.h"
#include "controls/DebugControlSubsystem.h"
#include "controls/PerformanceControlSubsystem.h"
#include "controls/SceneControlSubsystem.h"
#include "controls/PlayerControlSubsystem.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <SDL3/SDL_log.h>

RendererSystems::RendererSystems() {
    // Pre-register always-present infrastructure systems
    registry_.emplace<ErosionDataLoader>();
    registry_.emplace<RoadNetworkLoader>();
    registry_.emplace<RoadRiverVisualization>();
    registry_.emplace<UBOBuilder>();
    registry_.emplace<TimeSystem>();
    registry_.emplace<CelestialCalculator>();
    registry_.emplace<EnvironmentSettings>();
}

RendererSystems::~RendererSystems() {
    // SystemRegistry destructor handles reverse-order cleanup
}

// ============================================================================
// Non-trivial setters (have sceneCollection_ side effects)
// ============================================================================

void RendererSystems::setRocks(std::unique_ptr<ScatterSystem> system) {
    // Unregister old material if exists
    if (auto* old = registry_.find<ScatterSystem, RocksTag>()) {
        sceneCollection_.unregisterMaterial(&old->getMaterial());
    }
    auto& ref = registry_.add<ScatterSystem, RocksTag>(std::move(system));
    sceneCollection_.registerMaterial(&ref.getMaterial());
}

void RendererSystems::setDetritus(std::unique_ptr<ScatterSystem> system) {
    // Unregister old material if exists
    if (auto* old = registry_.find<ScatterSystem, DetritusTag>()) {
        sceneCollection_.unregisterMaterial(&old->getMaterial());
    }
    auto& ref = registry_.add<ScatterSystem, DetritusTag>(std::move(system));
    sceneCollection_.registerMaterial(&ref.getMaterial());
}

// ============================================================================
// Trivial setters for non-grouped systems
// (Grouped systems are registered via Bundle::registerAll() in their system group .cpp files)
// ============================================================================

void RendererSystems::setPostProcess(std::unique_ptr<PostProcessSystem> system) { registry_.add<PostProcessSystem>(std::move(system)); }
void RendererSystems::setBloom(std::unique_ptr<BloomSystem> system) { registry_.add<BloomSystem>(std::move(system)); }
void RendererSystems::setBilateralGrid(std::unique_ptr<BilateralGridSystem> system) { registry_.add<BilateralGridSystem>(std::move(system)); }
void RendererSystems::setGodRays(std::unique_ptr<GodRaysSystem> system) { registry_.add<GodRaysSystem>(std::move(system)); }
void RendererSystems::setShadow(std::unique_ptr<ShadowSystem> system) { registry_.add<ShadowSystem>(std::move(system)); }
void RendererSystems::setTerrain(std::unique_ptr<TerrainSystem> system) { registry_.add<TerrainSystem>(std::move(system)); }
void RendererSystems::setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred) { registry_.add<DeferredTerrainObjects>(std::move(deferred)); }
void RendererSystems::setHiZ(std::unique_ptr<HiZSystem> system) { registry_.add<HiZSystem>(std::move(system)); }
void RendererSystems::setGPUSceneBuffer(std::unique_ptr<GPUSceneBuffer> buffer) { registry_.add<GPUSceneBuffer>(std::move(buffer)); }
void RendererSystems::setGPUCullPass(std::unique_ptr<GPUCullPass> pass) { registry_.add<GPUCullPass>(std::move(pass)); }
void RendererSystems::setScreenSpaceShadow(std::unique_ptr<ScreenSpaceShadowSystem> system) { registry_.add<ScreenSpaceShadowSystem>(std::move(system)); }
void RendererSystems::setScene(std::unique_ptr<SceneManager> system) { registry_.add<SceneManager>(std::move(system)); }
void RendererSystems::setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers) { registry_.add<GlobalBufferManager>(std::move(buffers)); }
void RendererSystems::setSkinnedMesh(std::unique_ptr<SkinnedMeshRenderer> system) { registry_.add<SkinnedMeshRenderer>(std::move(system)); }
void RendererSystems::setNPCRenderer(std::unique_ptr<NPCRenderer> renderer) { registry_.add<NPCRenderer>(std::move(renderer)); }
void RendererSystems::setDebugLineSystem(std::unique_ptr<DebugLineSystem> system) { registry_.add<DebugLineSystem>(std::move(system)); }
void RendererSystems::setProfiler(std::unique_ptr<Profiler> profiler) { registry_.add<Profiler>(std::move(profiler)); }

// ============================================================================
// Initialization
// ============================================================================

bool RendererSystems::init(const InitContext& /*initCtx*/,
                            VkRenderPass /*swapchainRenderPass*/,
                            VkFormat /*swapchainImageFormat*/,
                            VkDescriptorSetLayout /*mainDescriptorSetLayout*/,
                            VkFormat /*depthFormat*/,
                            VkSampler /*depthSampler*/,
                            const std::string& /*resourcePath*/) {
    // NOTE: This centralized init is not currently used.
    // Initialization is done via RendererInitPhases.cpp which calls each subsystem directly.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RendererSystems::init() is not implemented - use RendererInitPhases instead");
    return false;
}

void RendererSystems::destroy(VkDevice device, VmaAllocator allocator) {
    SDL_Log("RendererSystems::destroy starting");

    // GPUSceneBuffer needs explicit cleanup before its destructor
    if (auto* gpuScene = registry_.find<GPUSceneBuffer>()) {
        gpuScene->cleanup();
    }

    // SystemRegistry::destroyAll() destroys in reverse registration order,
    // which mirrors the original reverse-dependency destruction.
    registry_.destroyAll();

    SDL_Log("RendererSystems::destroy complete");
}

CoreResources RendererSystems::getCoreResources(uint32_t framesInFlight) const {
    return CoreResources::collect(registry_.get<PostProcessSystem>(),
                                  registry_.get<ShadowSystem>(),
                                  registry_.get<TerrainSystem>(), framesInFlight);
}

#ifdef JPH_DEBUG_RENDERER
void RendererSystems::createPhysicsDebugRenderer(const InitContext& /*ctx*/, VkRenderPass /*hdrRenderPass*/) {
    auto renderer = std::make_unique<PhysicsDebugRenderer>();
    renderer->init();
    registry_.add<PhysicsDebugRenderer>(std::move(renderer));
}
#endif

// ============================================================================
// Control Subsystem Implementation
// ============================================================================

void RendererSystems::initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles) {
    registry_.add<EnvironmentControlSubsystem>(std::make_unique<EnvironmentControlSubsystem>(
        registry_.get<FroxelSystem>(), registry_.get<AtmosphereLUTSystem>(),
        registry_.get<LeafSystem>(), registry_.get<CloudShadowSystem>(),
        registry_.get<PostProcessSystem>(), registry_.get<EnvironmentSettings>()));
    registry_.add<WaterControlSubsystem>(std::make_unique<WaterControlSubsystem>(
        registry_.get<WaterSystem>(), registry_.get<WaterTileCull>()));
    registry_.add<TreeControlSubsystem>(std::make_unique<TreeControlSubsystem>(
        registry_.find<TreeSystem>(), *this));
    registry_.add<GrassControlAdapter>(std::make_unique<GrassControlAdapter>(
        registry_.get<GrassSystem>()));
    registry_.add<DebugControlSubsystem>(std::make_unique<DebugControlSubsystem>(
        registry_.get<DebugLineSystem>(), registry_.get<HiZSystem>(), *this));
    registry_.add<PerformanceControlSubsystem>(std::make_unique<PerformanceControlSubsystem>(
        perfToggles, nullptr));
    registry_.add<SceneControlSubsystem>(std::make_unique<SceneControlSubsystem>(
        registry_.get<SceneManager>(), vulkanContext));
    registry_.add<PlayerControlSubsystem>(std::make_unique<PlayerControlSubsystem>(
        registry_.get<SceneManager>(), vulkanContext));

    controlsInitialized_ = true;
    SDL_Log("Control subsystems initialized");
}

void RendererSystems::setPerformanceSyncCallback(std::function<void()> callback) {
    if (auto* perf = registry_.find<PerformanceControlSubsystem>()) {
        perf->setSyncCallback(callback);
    }
}

// ============================================================================
// Temporal system management
// ============================================================================

void RendererSystems::registerTemporalSystem(ITemporalSystem* system) {
    if (system) {
        temporalSystems_.push_back(system);
    }
}

void RendererSystems::resetAllTemporalHistory() {
    SDL_Log("Resetting temporal history for %zu systems", temporalSystems_.size());
    for (auto* system : temporalSystems_) {
        if (system) {
            system->resetTemporalHistory();
        }
    }
}

// ============================================================================
// Control subsystem accessors
// ============================================================================

// Systems that directly implement their interfaces:
ILocationControl& RendererSystems::locationControl() { return registry_.get<CelestialCalculator>(); }
const ILocationControl& RendererSystems::locationControl() const { return registry_.get<CelestialCalculator>(); }

IWeatherState& RendererSystems::weatherState() { return registry_.get<WeatherSystem>(); }
const IWeatherState& RendererSystems::weatherState() const { return registry_.get<WeatherSystem>(); }

IEnvironmentControl& RendererSystems::environmentControl() { return registry_.get<EnvironmentControlSubsystem>(); }
const IEnvironmentControl& RendererSystems::environmentControl() const { return registry_.get<EnvironmentControlSubsystem>(); }

IPostProcessState& RendererSystems::postProcessState() { return registry_.get<PostProcessSystem>(); }
const IPostProcessState& RendererSystems::postProcessState() const { return registry_.get<PostProcessSystem>(); }

ICloudShadowControl& RendererSystems::cloudShadowControl() { return registry_.get<CloudShadowSystem>(); }
const ICloudShadowControl& RendererSystems::cloudShadowControl() const { return registry_.get<CloudShadowSystem>(); }

ITerrainControl& RendererSystems::terrainControl() { return registry_.get<TerrainSystem>(); }
const ITerrainControl& RendererSystems::terrainControl() const { return registry_.get<TerrainSystem>(); }

IWaterControl& RendererSystems::waterControl() { return registry_.get<WaterControlSubsystem>(); }
const IWaterControl& RendererSystems::waterControl() const { return registry_.get<WaterControlSubsystem>(); }

ITreeControl& RendererSystems::treeControl() { return registry_.get<TreeControlSubsystem>(); }
const ITreeControl& RendererSystems::treeControl() const { return registry_.get<TreeControlSubsystem>(); }

IGrassControl& RendererSystems::grassControl() { return registry_.get<GrassControlAdapter>(); }
const IGrassControl& RendererSystems::grassControl() const { return registry_.get<GrassControlAdapter>(); }

IDebugControl& RendererSystems::debugControl() { return registry_.get<DebugControlSubsystem>(); }
const IDebugControl& RendererSystems::debugControl() const { return registry_.get<DebugControlSubsystem>(); }
DebugControlSubsystem& RendererSystems::debugControlSubsystem() { return registry_.get<DebugControlSubsystem>(); }
const DebugControlSubsystem& RendererSystems::debugControlSubsystem() const { return registry_.get<DebugControlSubsystem>(); }

IProfilerControl& RendererSystems::profilerControl() { return registry_.get<Profiler>(); }
const IProfilerControl& RendererSystems::profilerControl() const { return registry_.get<Profiler>(); }

IPerformanceControl& RendererSystems::performanceControl() { return registry_.get<PerformanceControlSubsystem>(); }
const IPerformanceControl& RendererSystems::performanceControl() const { return registry_.get<PerformanceControlSubsystem>(); }

ISceneControl& RendererSystems::sceneControl() { return registry_.get<SceneControlSubsystem>(); }
const ISceneControl& RendererSystems::sceneControl() const { return registry_.get<SceneControlSubsystem>(); }

IPlayerControl& RendererSystems::playerControl() { return registry_.get<PlayerControlSubsystem>(); }
const IPlayerControl& RendererSystems::playerControl() const { return registry_.get<PlayerControlSubsystem>(); }
