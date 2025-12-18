#include <array>
#include "RendererInit.h"
#include "RendererSystems.h"
#include "VulkanRAII.h"
#include "MaterialDescriptorFactory.h"

// Post-processing systems
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "SSRSystem.h"
#include "HiZSystem.h"

// Atmosphere/weather systems
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"

// Vegetation systems
#include "GrassSystem.h"
#include "LeafSystem.h"
#include "RockSystem.h"
#include "TreeEditSystem.h"

// Water systems
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"

// Terrain
#include "TerrainSystem.h"

// Other systems
#include "CatmullClarkSystem.h"
#include "ShadowSystem.h"
#include "MaterialRegistry.h"
#include "SkinnedMeshRenderer.h"
#include "DebugLineSystem.h"

#include <SDL3/SDL.h>

bool RendererInit::initPostProcessing(
    RendererSystems& systems,
    const InitContext& ctx,
    VkRenderPass finalRenderPass,
    VkFormat swapchainImageFormat
) {
    // Initialize post-process system via factory
    auto postProcessSystem = PostProcessSystem::create(ctx, finalRenderPass, swapchainImageFormat);
    if (!postProcessSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize PostProcessSystem");
        return false;
    }

    // Initialize bloom system via factory
    auto bloomSystem = BloomSystem::create(ctx);
    if (!bloomSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize BloomSystem");
        return false;
    }

    // Bind bloom texture to post-process system
    postProcessSystem->setBloomTexture(bloomSystem->getBloomOutput(), bloomSystem->getBloomSampler());

    // Store in RendererSystems
    systems.setPostProcess(std::move(postProcessSystem));
    systems.setBloom(std::move(bloomSystem));

    return true;
}

bool RendererInit::initSnowSubsystems(
    RendererSystems& systems,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Initialize snow mask system via factory
    SnowMaskSystem::InitInfo snowMaskInfo{};
    snowMaskInfo.device = ctx.device;
    snowMaskInfo.allocator = ctx.allocator;
    snowMaskInfo.renderPass = hdrRenderPass;
    snowMaskInfo.descriptorPool = ctx.descriptorPool;
    snowMaskInfo.extent = ctx.extent;
    snowMaskInfo.shaderPath = ctx.shaderPath;
    snowMaskInfo.framesInFlight = ctx.framesInFlight;

    auto snowMaskSystem = SnowMaskSystem::create(snowMaskInfo);
    if (!snowMaskSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SnowMaskSystem");
        return false;
    }
    systems.setSnowMask(std::move(snowMaskSystem));

    // Initialize volumetric snow system via factory
    VolumetricSnowSystem::InitInfo volumetricSnowInfo{};
    volumetricSnowInfo.device = ctx.device;
    volumetricSnowInfo.allocator = ctx.allocator;
    volumetricSnowInfo.renderPass = hdrRenderPass;
    volumetricSnowInfo.descriptorPool = ctx.descriptorPool;
    volumetricSnowInfo.extent = ctx.extent;
    volumetricSnowInfo.shaderPath = ctx.shaderPath;
    volumetricSnowInfo.framesInFlight = ctx.framesInFlight;

    auto volumetricSnowSystem = VolumetricSnowSystem::create(volumetricSnowInfo);
    if (!volumetricSnowSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VolumetricSnowSystem");
        return false;
    }
    systems.setVolumetricSnow(std::move(volumetricSnowSystem));

    return true;
}

bool RendererInit::initGrassSubsystem(
    RendererSystems& systems,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass,
    VkRenderPass shadowRenderPass,
    uint32_t shadowMapSize
) {
    // Initialize wind system via factory
    WindSystem::InitInfo windInfo{};
    windInfo.device = ctx.device;
    windInfo.allocator = ctx.allocator;
    windInfo.framesInFlight = ctx.framesInFlight;

    auto windSystem = WindSystem::create(windInfo);
    if (!windSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WindSystem");
        return false;
    }
    systems.setWind(std::move(windSystem));

    // Initialize grass system via factory
    GrassSystem::InitInfo grassInfo{};
    grassInfo.device = ctx.device;
    grassInfo.allocator = ctx.allocator;
    grassInfo.renderPass = hdrRenderPass;
    grassInfo.shadowRenderPass = shadowRenderPass;
    grassInfo.descriptorPool = ctx.descriptorPool;
    grassInfo.extent = ctx.extent;
    grassInfo.shadowMapSize = shadowMapSize;
    grassInfo.shaderPath = ctx.shaderPath;
    grassInfo.framesInFlight = ctx.framesInFlight;

    auto grassSystem = GrassSystem::create(grassInfo);
    if (!grassSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassSystem");
        return false;
    }
    systems.setGrass(std::move(grassSystem));

    // Connect environment settings to grass (leaf is connected later after initWeatherSubsystems)
    const EnvironmentSettings* environmentSettings = &systems.wind().getEnvironmentSettings();
    systems.grass().setEnvironmentSettings(environmentSettings);

    return true;
}

bool RendererInit::initWeatherSubsystems(
    RendererSystems& systems,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Initialize weather particle system (rain/snow) via factory
    WeatherSystem::InitInfo weatherInfo{};
    weatherInfo.device = ctx.device;
    weatherInfo.allocator = ctx.allocator;
    weatherInfo.renderPass = hdrRenderPass;
    weatherInfo.descriptorPool = ctx.descriptorPool;
    weatherInfo.extent = ctx.extent;
    weatherInfo.shaderPath = ctx.shaderPath;
    weatherInfo.framesInFlight = ctx.framesInFlight;

    auto weatherSystem = WeatherSystem::create(weatherInfo);
    if (!weatherSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WeatherSystem");
        return false;
    }
    systems.setWeather(std::move(weatherSystem));

    // Initialize leaf particle system via factory
    LeafSystem::InitInfo leafInfo{};
    leafInfo.device = ctx.device;
    leafInfo.allocator = ctx.allocator;
    leafInfo.renderPass = hdrRenderPass;
    leafInfo.descriptorPool = ctx.descriptorPool;
    leafInfo.extent = ctx.extent;
    leafInfo.shaderPath = ctx.shaderPath;
    leafInfo.framesInFlight = ctx.framesInFlight;

    auto leafSystem = LeafSystem::create(leafInfo);
    if (!leafSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize LeafSystem");
        return false;
    }

    // Set default leaf intensity (autumn scene)
    leafSystem->setIntensity(0.5f);
    systems.setLeaf(std::move(leafSystem));

    return true;
}

bool RendererInit::initAtmosphereSubsystems(
    RendererSystems& systems,
    const InitContext& ctx,
    VkImageView shadowMapView,
    VkSampler shadowMapSampler,
    const std::vector<VkBuffer>& lightBuffers
) {
    // Initialize froxel volumetric fog system via factory
    auto froxelSystem = FroxelSystem::create(ctx, shadowMapView, shadowMapSampler, lightBuffers);
    if (!froxelSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize FroxelSystem");
        return false;
    }
    systems.setFroxel(std::move(froxelSystem));

    // Connect froxel volume to post-process system for compositing
    systems.postProcess().setFroxelVolume(systems.froxel().getIntegratedVolumeView(), systems.froxel().getVolumeSampler());
    systems.postProcess().setFroxelParams(systems.froxel().getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);
    systems.postProcess().setFroxelEnabled(true);

    // Initialize atmosphere LUT system via factory
    auto atmosphereLUTSystem = AtmosphereLUTSystem::create(ctx);
    if (!atmosphereLUTSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize AtmosphereLUTSystem");
        return false;
    }
    systems.setAtmosphereLUT(std::move(atmosphereLUTSystem));

    // Compute atmosphere LUTs at startup
    {
        CommandScope cmdScope(ctx.device, ctx.commandPool, ctx.graphicsQueue);
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to begin command buffer for atmosphere LUT computation");
            return false;
        }

        // Compute transmittance and multi-scatter LUTs (once at startup)
        systems.atmosphereLUT().computeTransmittanceLUT(cmdScope.get());
        systems.atmosphereLUT().computeMultiScatterLUT(cmdScope.get());
        systems.atmosphereLUT().computeIrradianceLUT(cmdScope.get());

        // Compute sky-view LUT for current sun direction
        glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);  // Default 45 degree sun
        systems.atmosphereLUT().computeSkyViewLUT(cmdScope.get(), sunDir, glm::vec3(0.0f), 0.0f);

        // Compute cloud map LUT (paraboloid projection)
        systems.atmosphereLUT().computeCloudMapLUT(cmdScope.get(), glm::vec3(0.0f), 0.0f);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to end command buffer for atmosphere LUT computation");
            return false;
        }
    }

    SDL_Log("Atmosphere LUTs computed successfully");

    // Export LUTs as PNG files for visualization
    systems.atmosphereLUT().exportLUTsAsPNG(ctx.resourcePath);
    SDL_Log("Atmosphere LUTs exported as PNG to: %s", ctx.resourcePath.c_str());

    // Initialize cloud shadow system via factory
    auto cloudShadowSystem = CloudShadowSystem::create(ctx, systems.atmosphereLUT().getCloudMapLUTView(), systems.atmosphereLUT().getLUTSampler());
    if (!cloudShadowSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize CloudShadowSystem");
        return false;
    }
    systems.setCloudShadow(std::move(cloudShadowSystem));

    return true;
}

bool RendererInit::initWaterSubsystems(
    WaterSubsystems& water,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass,
    const ShadowSystem& shadowSystem,
    const TerrainSystem& terrainSystem,
    const TerrainConfig& terrainConfig,
    const PostProcessSystem& postProcessSystem,
    VkSampler depthSampler
) {
    float seaLevel = -terrainConfig.minAltitude;

    // All water subsystems are created via factories in RendererInitPhases before this function is called
    // This function now only configures them

    // Configure water surface
    water.system.setWaterLevel(seaLevel);
    water.system.setWaterExtent(glm::vec2(0.0f, 0.0f), glm::vec2(65536.0f, 65536.0f));
    // English estuary/coastal water style - murky grey-green, moderate chop
    water.system.setWaterColor(glm::vec4(0.15f, 0.22f, 0.25f, 0.9f));
    water.system.setWaveAmplitude(0.3f);
    water.system.setWaveLength(15.0f);
    water.system.setWaveSteepness(0.25f);
    water.system.setWaveSpeed(0.5f);
    water.system.setTidalRange(3.0f);
    water.system.setTerrainParams(terrainConfig.size, terrainConfig.heightScale);
    water.system.setShoreBlendDistance(8.0f);
    water.system.setShoreFoamWidth(15.0f);
    water.system.setCameraPlanes(0.1f, 50000.0f);

    // Generate flow map from terrain data (FlowMapGenerator already created via factory)
    FlowMapGenerator::Config flowConfig{};
    flowConfig.resolution = 512;
    flowConfig.worldSize = terrainConfig.size;
    flowConfig.waterLevel = seaLevel;
    flowConfig.maxFlowSpeed = 1.0f;
    flowConfig.slopeInfluence = 2.0f;
    flowConfig.shoreDistance = 100.0f;

    const float* heightData = terrainSystem.getHeightMapData();
    uint32_t heightRes = terrainSystem.getHeightMapResolution();
    if (heightData && heightRes > 0) {
        std::vector<float> heightVec(heightData, heightData + heightRes * heightRes);
        if (!water.flowMapGenerator.generateFromTerrain(heightVec, heightRes, terrainConfig.heightScale, flowConfig)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Flow map generation failed, using radial flow fallback");
            water.flowMapGenerator.generateRadialFlow(flowConfig, glm::vec2(0.0f));
        }
    } else {
        SDL_Log("No terrain height data available, generating radial flow map");
        water.flowMapGenerator.generateRadialFlow(flowConfig, glm::vec2(0.0f));
    }

    // Initialize SSR system (Phase 10: Screen-Space Reflections) via factory
    auto ssrSystem = SSRSystem::create(ctx);
    if (ssrSystem) {
        water.rendererSystems.setSSR(std::move(ssrSystem));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SSR system - continuing without SSR");
        // Don't fail init - SSR is optional
    }

    return true;
}

bool RendererInit::createWaterDescriptorSets(
    WaterSubsystems& water,
    const std::vector<VkBuffer>& uniformBuffers,
    size_t uniformBufferSize,
    ShadowSystem& shadowSystem,
    const TerrainSystem& terrainSystem,
    const PostProcessSystem& postProcessSystem,
    VkSampler depthSampler
) {
    // Create water descriptor sets with terrain heightmap, flow map, displacement map, temporal foam, SSR, scene depth, and tile cache
    // Pass triple-buffered tile info buffers to avoid CPU-GPU sync issues
    std::array<VkBuffer, 3> waterTileInfoBuffers = {
        terrainSystem.getTileInfoBuffer(0),
        terrainSystem.getTileInfoBuffer(1),
        terrainSystem.getTileInfoBuffer(2)
    };
    if (!water.system.createDescriptorSets(
            uniformBuffers, uniformBufferSize, shadowSystem,
            terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
            water.flowMapGenerator.getFlowMapView(), water.flowMapGenerator.getFlowMapSampler(),
            water.displacement.getDisplacementMapView(), water.displacement.getSampler(),
            water.foamBuffer.getFoamBufferView(), water.foamBuffer.getSampler(),
            water.rendererSystems.ssr().getSSRResultView(), water.rendererSystems.ssr().getSampler(),
            postProcessSystem.getHDRDepthView(), depthSampler,
            terrainSystem.getTileArrayView(), terrainSystem.getTileSampler(),
            waterTileInfoBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create water descriptor sets");
        return false;
    }

    // Create water G-buffer descriptor sets
    if (water.gBuffer.getPipeline() != VK_NULL_HANDLE) {
        if (!water.gBuffer.createDescriptorSets(
                uniformBuffers, uniformBufferSize,
                water.system.getUniformBuffers(), WaterSystem::getUniformBufferSize(),
                terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
                water.flowMapGenerator.getFlowMapView(), water.flowMapGenerator.getFlowMapSampler())) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create water G-buffer descriptor sets");
        }
    }

    return true;
}

bool RendererInit::initHiZSystem(
    RendererSystems& systems,
    const InitContext& ctx,
    VkFormat depthFormat,
    VkImageView hdrDepthView,
    VkSampler depthSampler
) {
    auto hiZSystem = HiZSystem::create(ctx, depthFormat);
    if (!hiZSystem) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        return true;  // Don't fail - Hi-Z is optional
    }

    // Connect depth buffer to Hi-Z system
    hiZSystem->setDepthBuffer(hdrDepthView, depthSampler);

    // Store in RendererSystems
    systems.setHiZ(std::move(hiZSystem));

    return true;
}

std::unique_ptr<DebugLineSystem> RendererInit::createDebugLineSystem(
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    auto system = DebugLineSystem::create(ctx, hdrRenderPass);
    if (!system) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create debug line system");
        return nullptr;
    }
    SDL_Log("Debug line system created");
    return system;
}

void RendererInit::updateCloudShadowBindings(
    VkDevice device,
    MaterialRegistry& materialRegistry,
    const std::vector<VkDescriptorSet>& rockDescriptorSets,
    SkinnedMeshRenderer& skinnedMeshRenderer,
    VkImageView cloudShadowView,
    VkSampler cloudShadowSampler
) {
    // Update MaterialRegistry-managed descriptor sets
    materialRegistry.updateCloudShadowBinding(device, cloudShadowView, cloudShadowSampler);

    // Update descriptor sets not managed by MaterialRegistry (rocks, skinned)
    MaterialDescriptorFactory factory(device);
    for (auto set : rockDescriptorSets) {
        factory.updateCloudShadowBinding(set, cloudShadowView, cloudShadowSampler);
    }

    // Update skinned mesh renderer cloud shadow binding
    skinnedMeshRenderer.updateCloudShadowBinding(cloudShadowView, cloudShadowSampler);
}
