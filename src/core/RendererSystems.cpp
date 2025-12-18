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
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "RockSystem.h"
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
#include "TreeEditSystem.h"
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

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <SDL3/SDL_log.h>

RendererSystems::RendererSystems()
    // Tier 1
    : postProcessSystem_(std::make_unique<PostProcessSystem>())
    , bloomSystem_(std::make_unique<BloomSystem>())
    , shadowSystem_(std::make_unique<ShadowSystem>())
    , terrainSystem_(std::make_unique<TerrainSystem>())
    // Tier 2 - Sky/Atmosphere
    , skySystem_(std::make_unique<SkySystem>())
    , atmosphereLUTSystem_(std::make_unique<AtmosphereLUTSystem>())
    , froxelSystem_(std::make_unique<FroxelSystem>())
    , cloudShadowSystem_(std::make_unique<CloudShadowSystem>())
    // Tier 2 - Environment
    , grassSystem_(std::make_unique<GrassSystem>())
    , windSystem_(std::make_unique<WindSystem>())
    , weatherSystem_(std::make_unique<WeatherSystem>())
    , leafSystem_(std::make_unique<LeafSystem>())
    // Tier 2 - Snow
    , snowMaskSystem_(std::make_unique<SnowMaskSystem>())
    , volumetricSnowSystem_(std::make_unique<VolumetricSnowSystem>())
    // Tier 2 - Water
    , waterSystem_(std::make_unique<WaterSystem>())
    , waterDisplacement_(std::make_unique<WaterDisplacement>())
    , flowMapGenerator_(std::make_unique<FlowMapGenerator>())
    , foamBuffer_(std::make_unique<FoamBuffer>())
    , ssrSystem_(std::make_unique<SSRSystem>())
    , waterTileCull_(std::make_unique<WaterTileCull>())
    , waterGBuffer_(std::make_unique<WaterGBuffer>())
    // Tier 2 - Geometry
    , catmullClarkSystem_(std::make_unique<CatmullClarkSystem>())
    , rockSystem_(std::make_unique<RockSystem>())
    // Tier 2 - Culling
    , hiZSystem_(std::make_unique<HiZSystem>())
    // Infrastructure
    , sceneManager_(std::make_unique<SceneManager>())
    , globalBufferManager_(std::make_unique<GlobalBufferManager>())
    , erosionDataLoader_(std::make_unique<ErosionDataLoader>())
    , skinnedMeshRenderer_(std::make_unique<SkinnedMeshRenderer>())
    // Tools
    , treeEditSystem_(std::make_unique<TreeEditSystem>())
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
    treeEditSystem_->destroy(device, allocator);

    // Water
    waterGBuffer_->destroy();
    waterTileCull_->destroy();
    ssrSystem_->destroy();
    foamBuffer_->destroy();
    flowMapGenerator_->destroy(device, allocator);
    waterDisplacement_->destroy();
    waterSystem_->destroy(device, allocator);

    profiler_.reset();
    hiZSystem_->destroy();

    catmullClarkSystem_->destroy(device, allocator);
    rockSystem_->destroy(allocator, device);

    // Atmosphere
    cloudShadowSystem_->destroy();
    atmosphereLUTSystem_->destroy(device, allocator);
    froxelSystem_->destroy(device, allocator);

    // Weather/Snow
    leafSystem_->destroy(device, allocator);
    weatherSystem_->destroy(device, allocator);
    volumetricSnowSystem_->destroy(device, allocator);
    snowMaskSystem_->destroy(device, allocator);

    // Grass/Wind
    windSystem_->destroy(device, allocator);
    grassSystem_->destroy(device, allocator);

    sceneManager_->destroy(allocator, device);
    globalBufferManager_->destroy(allocator);

    // Tier 1
    skySystem_->destroy(device, allocator);
    terrainSystem_->destroy(device, allocator);
    shadowSystem_->destroy();
    skinnedMeshRenderer_->destroy();
    bloomSystem_->destroy(device, allocator);
    postProcessSystem_->destroy(device, allocator);

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
