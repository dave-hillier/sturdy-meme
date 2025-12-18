#include "RendererInit.h"
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
    PostProcessSystem& postProcessSystem,
    BloomSystem& bloomSystem,
    const InitContext& ctx,
    VkRenderPass finalRenderPass,
    VkFormat swapchainImageFormat
) {
    // Initialize post-process system early to get HDR render pass
    if (!postProcessSystem.init(ctx, finalRenderPass, swapchainImageFormat)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize PostProcessSystem");
        return false;
    }

    // Initialize bloom system
    if (!bloomSystem.init(ctx)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize BloomSystem");
        return false;
    }

    // Bind bloom texture to post-process system
    postProcessSystem.setBloomTexture(bloomSystem.getBloomOutput(), bloomSystem.getBloomSampler());

    return true;
}

bool RendererInit::initSnowSubsystems(
    SnowMaskSystem& snowMaskSystem,
    VolumetricSnowSystem& volumetricSnowSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Initialize snow mask system
    SnowMaskSystem::InitInfo snowMaskInfo{};
    snowMaskInfo.device = ctx.device;
    snowMaskInfo.allocator = ctx.allocator;
    snowMaskInfo.renderPass = hdrRenderPass;
    snowMaskInfo.descriptorPool = ctx.descriptorPool;
    snowMaskInfo.extent = ctx.extent;
    snowMaskInfo.shaderPath = ctx.shaderPath;
    snowMaskInfo.framesInFlight = ctx.framesInFlight;

    if (!snowMaskSystem.init(snowMaskInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SnowMaskSystem");
        return false;
    }

    // Initialize volumetric snow system (cascaded heightfield)
    VolumetricSnowSystem::InitInfo volumetricSnowInfo{};
    volumetricSnowInfo.device = ctx.device;
    volumetricSnowInfo.allocator = ctx.allocator;
    volumetricSnowInfo.renderPass = hdrRenderPass;
    volumetricSnowInfo.descriptorPool = ctx.descriptorPool;
    volumetricSnowInfo.extent = ctx.extent;
    volumetricSnowInfo.shaderPath = ctx.shaderPath;
    volumetricSnowInfo.framesInFlight = ctx.framesInFlight;

    if (!volumetricSnowSystem.init(volumetricSnowInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VolumetricSnowSystem");
        return false;
    }

    return true;
}

bool RendererInit::initGrassSubsystem(
    GrassSystem& grassSystem,
    WindSystem& windSystem,
    LeafSystem& leafSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass,
    VkRenderPass shadowRenderPass,
    uint32_t shadowMapSize
) {
    // Initialize grass system using HDR render pass
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

    if (!grassSystem.init(grassInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassSystem");
        return false;
    }

    // Initialize wind system
    WindSystem::InitInfo windInfo{};
    windInfo.device = ctx.device;
    windInfo.allocator = ctx.allocator;
    windInfo.framesInFlight = ctx.framesInFlight;

    if (!windSystem.init(windInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WindSystem");
        return false;
    }

    // Connect environment settings
    const EnvironmentSettings* environmentSettings = &windSystem.getEnvironmentSettings();
    grassSystem.setEnvironmentSettings(environmentSettings);
    leafSystem.setEnvironmentSettings(environmentSettings);

    return true;
}

bool RendererInit::initWeatherSubsystems(
    WeatherSystem& weatherSystem,
    LeafSystem& leafSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Initialize weather particle system (rain/snow)
    WeatherSystem::InitInfo weatherInfo{};
    weatherInfo.device = ctx.device;
    weatherInfo.allocator = ctx.allocator;
    weatherInfo.renderPass = hdrRenderPass;
    weatherInfo.descriptorPool = ctx.descriptorPool;
    weatherInfo.extent = ctx.extent;
    weatherInfo.shaderPath = ctx.shaderPath;
    weatherInfo.framesInFlight = ctx.framesInFlight;

    if (!weatherSystem.init(weatherInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WeatherSystem");
        return false;
    }

    // Initialize leaf particle system
    LeafSystem::InitInfo leafInfo{};
    leafInfo.device = ctx.device;
    leafInfo.allocator = ctx.allocator;
    leafInfo.renderPass = hdrRenderPass;
    leafInfo.descriptorPool = ctx.descriptorPool;
    leafInfo.extent = ctx.extent;
    leafInfo.shaderPath = ctx.shaderPath;
    leafInfo.framesInFlight = ctx.framesInFlight;

    if (!leafSystem.init(leafInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize LeafSystem");
        return false;
    }

    // Set default leaf intensity (autumn scene)
    leafSystem.setIntensity(0.5f);

    return true;
}

bool RendererInit::initAtmosphereSubsystems(
    FroxelSystem& froxelSystem,
    AtmosphereLUTSystem& atmosphereLUTSystem,
    CloudShadowSystem& cloudShadowSystem,
    PostProcessSystem& postProcessSystem,
    const InitContext& ctx,
    VkImageView shadowMapView,
    VkSampler shadowMapSampler,
    const std::vector<VkBuffer>& lightBuffers
) {
    // Initialize froxel volumetric fog system
    if (!froxelSystem.init(ctx, shadowMapView, shadowMapSampler, lightBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize FroxelSystem");
        return false;
    }

    // Connect froxel volume to post-process system for compositing
    postProcessSystem.setFroxelVolume(froxelSystem.getIntegratedVolumeView(), froxelSystem.getVolumeSampler());
    postProcessSystem.setFroxelParams(froxelSystem.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);
    postProcessSystem.setFroxelEnabled(true);

    // Initialize atmosphere LUT system
    if (!atmosphereLUTSystem.init(ctx)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize AtmosphereLUTSystem");
        return false;
    }

    // Compute atmosphere LUTs at startup
    {
        CommandScope cmdScope(ctx.device, ctx.commandPool, ctx.graphicsQueue);
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to begin command buffer for atmosphere LUT computation");
            return false;
        }

        // Compute transmittance and multi-scatter LUTs (once at startup)
        atmosphereLUTSystem.computeTransmittanceLUT(cmdScope.get());
        atmosphereLUTSystem.computeMultiScatterLUT(cmdScope.get());
        atmosphereLUTSystem.computeIrradianceLUT(cmdScope.get());

        // Compute sky-view LUT for current sun direction
        glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);  // Default 45 degree sun
        atmosphereLUTSystem.computeSkyViewLUT(cmdScope.get(), sunDir, glm::vec3(0.0f), 0.0f);

        // Compute cloud map LUT (paraboloid projection)
        atmosphereLUTSystem.computeCloudMapLUT(cmdScope.get(), glm::vec3(0.0f), 0.0f);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to end command buffer for atmosphere LUT computation");
            return false;
        }
    }

    SDL_Log("Atmosphere LUTs computed successfully");

    // Export LUTs as PNG files for visualization
    atmosphereLUTSystem.exportLUTsAsPNG(ctx.resourcePath);
    SDL_Log("Atmosphere LUTs exported as PNG to: %s", ctx.resourcePath.c_str());

    // Initialize cloud shadow system
    if (!cloudShadowSystem.init(ctx, atmosphereLUTSystem.getCloudMapLUTView(), atmosphereLUTSystem.getLUTSampler())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize CloudShadowSystem");
        return false;
    }

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

    // Initialize water system - sea covering terrain areas below sea level
    WaterSystem::InitInfo waterInfo{};
    waterInfo.device = ctx.device;
    waterInfo.physicalDevice = ctx.physicalDevice;
    waterInfo.allocator = ctx.allocator;
    waterInfo.descriptorPool = ctx.descriptorPool;
    waterInfo.hdrRenderPass = hdrRenderPass;
    waterInfo.shaderPath = ctx.shaderPath;
    waterInfo.framesInFlight = ctx.framesInFlight;
    waterInfo.extent = ctx.extent;
    waterInfo.commandPool = ctx.commandPool;
    waterInfo.graphicsQueue = ctx.graphicsQueue;
    waterInfo.waterSize = 65536.0f;  // Extend well beyond terrain for horizon
    waterInfo.assetPath = ctx.resourcePath;

    if (!water.system.init(waterInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WaterSystem");
        return false;
    }

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

    // Initialize flow map generator
    if (!water.flowMapGenerator.init(ctx.device, ctx.allocator, ctx.commandPool, ctx.graphicsQueue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize flow map generator");
        return false;
    }

    // Generate flow map from terrain data
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

    // Initialize water displacement system (Phase 4: interactive splashes)
    WaterDisplacement::InitInfo dispInfo{};
    dispInfo.device = ctx.device;
    dispInfo.physicalDevice = ctx.physicalDevice;
    dispInfo.allocator = ctx.allocator;
    dispInfo.commandPool = ctx.commandPool;
    dispInfo.computeQueue = ctx.graphicsQueue;
    dispInfo.framesInFlight = ctx.framesInFlight;
    dispInfo.displacementResolution = 512;
    dispInfo.worldSize = 65536.0f;

    if (!water.displacement.init(dispInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize water displacement");
        return false;
    }

    // Initialize foam buffer (Phase 14: temporal foam persistence)
    FoamBuffer::InitInfo foamInfo{};
    foamInfo.device = ctx.device;
    foamInfo.physicalDevice = ctx.physicalDevice;
    foamInfo.allocator = ctx.allocator;
    foamInfo.commandPool = ctx.commandPool;
    foamInfo.computeQueue = ctx.graphicsQueue;
    foamInfo.shaderPath = ctx.shaderPath;
    foamInfo.framesInFlight = ctx.framesInFlight;
    foamInfo.resolution = 512;
    foamInfo.worldSize = 65536.0f;

    if (!water.foamBuffer.init(foamInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize foam buffer");
        return false;
    }

    // Initialize SSR system (Phase 10: Screen-Space Reflections)
    if (!water.ssrSystem.init(ctx)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SSR system - continuing without SSR");
        // Don't fail init - SSR is optional
    }

    // Initialize water tile culling (Phase 7: screen-space tile visibility)
    WaterTileCull::InitInfo tileCullInfo{};
    tileCullInfo.device = ctx.device;
    tileCullInfo.physicalDevice = ctx.physicalDevice;
    tileCullInfo.allocator = ctx.allocator;
    tileCullInfo.commandPool = ctx.commandPool;
    tileCullInfo.computeQueue = ctx.graphicsQueue;
    tileCullInfo.shaderPath = ctx.shaderPath;
    tileCullInfo.framesInFlight = ctx.framesInFlight;
    tileCullInfo.extent = ctx.extent;
    tileCullInfo.tileSize = 32;

    if (!water.tileCull.init(tileCullInfo)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize water tile cull - continuing without");
        // Don't fail init - tile culling is optional optimization
    }

    // Initialize water G-buffer (Phase 3)
    WaterGBuffer::InitInfo gbufferInfo{};
    gbufferInfo.device = ctx.device;
    gbufferInfo.physicalDevice = ctx.physicalDevice;
    gbufferInfo.allocator = ctx.allocator;
    gbufferInfo.fullResExtent = ctx.extent;
    gbufferInfo.resolutionScale = 0.5f;
    gbufferInfo.framesInFlight = ctx.framesInFlight;
    gbufferInfo.shaderPath = ctx.shaderPath;
    gbufferInfo.descriptorPool = ctx.descriptorPool;

    if (!water.gBuffer.init(gbufferInfo)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize water G-buffer - continuing without");
        // Don't fail init - G-buffer is optional optimization
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
    if (!water.system.createDescriptorSets(
            uniformBuffers, uniformBufferSize, shadowSystem,
            terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
            water.flowMapGenerator.getFlowMapView(), water.flowMapGenerator.getFlowMapSampler(),
            water.displacement.getDisplacementMapView(), water.displacement.getSampler(),
            water.foamBuffer.getFoamBufferView(), water.foamBuffer.getSampler(),
            water.ssrSystem.getSSRResultView(), water.ssrSystem.getSampler(),
            postProcessSystem.getHDRDepthView(), depthSampler,
            terrainSystem.getTileArrayView(), terrainSystem.getTileSampler(),
            terrainSystem.getTileInfoBuffer())) {
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

bool RendererInit::initTerrainSubsystems(
    TerrainSystem& terrainSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass,
    VkRenderPass shadowRenderPass,
    uint32_t shadowMapSize,
    const std::string& heightmapPath,
    const TerrainConfig& config
) {
    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = hdrRenderPass;
    terrainParams.shadowRenderPass = shadowRenderPass;
    terrainParams.shadowMapSize = shadowMapSize;
    terrainParams.texturePath = ctx.resourcePath + "/textures";

    if (!terrainSystem.init(ctx, terrainParams, config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize TerrainSystem");
        return false;
    }

    return true;
}

bool RendererInit::initRockSystem(
    RockSystem& rockSystem,
    const InitContext& ctx,
    float terrainSize,
    std::function<float(float, float)> getTerrainHeight
) {
    RockSystem::InitInfo rockInfo{};
    rockInfo.device = ctx.device;
    rockInfo.allocator = ctx.allocator;
    rockInfo.commandPool = ctx.commandPool;
    rockInfo.graphicsQueue = ctx.graphicsQueue;
    rockInfo.physicalDevice = ctx.physicalDevice;
    rockInfo.resourcePath = ctx.resourcePath;
    rockInfo.terrainSize = terrainSize;
    rockInfo.getTerrainHeight = std::move(getTerrainHeight);

    RockConfig rockConfig{};
    rockConfig.rockVariations = 6;
    rockConfig.rocksPerVariation = 10;
    rockConfig.minRadius = 0.4f;
    rockConfig.maxRadius = 2.0f;
    rockConfig.placementRadius = 100.0f;
    rockConfig.minDistanceBetween = 4.0f;
    rockConfig.roughness = 0.35f;
    rockConfig.asymmetry = 0.3f;
    rockConfig.subdivisions = 3;
    rockConfig.materialRoughness = 0.75f;
    rockConfig.materialMetallic = 0.0f;

    if (!rockSystem.init(rockInfo, rockConfig)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize RockSystem");
        return false;
    }

    return true;
}

bool RendererInit::initCatmullClarkSystem(
    CatmullClarkSystem& catmullClarkSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass,
    const glm::vec3& position
) {
    CatmullClarkSystem::InitInfo catmullClarkInfo{};
    catmullClarkInfo.device = ctx.device;
    catmullClarkInfo.physicalDevice = ctx.physicalDevice;
    catmullClarkInfo.allocator = ctx.allocator;
    catmullClarkInfo.renderPass = hdrRenderPass;
    catmullClarkInfo.descriptorPool = ctx.descriptorPool;
    catmullClarkInfo.extent = ctx.extent;
    catmullClarkInfo.shaderPath = ctx.shaderPath;
    catmullClarkInfo.framesInFlight = ctx.framesInFlight;
    catmullClarkInfo.graphicsQueue = ctx.graphicsQueue;
    catmullClarkInfo.commandPool = ctx.commandPool;

    CatmullClarkConfig catmullClarkConfig{};
    catmullClarkConfig.position = position;
    catmullClarkConfig.scale = glm::vec3(2.0f);
    catmullClarkConfig.targetEdgePixels = 12.0f;
    catmullClarkConfig.maxDepth = 16;
    catmullClarkConfig.splitThreshold = 18.0f;
    catmullClarkConfig.mergeThreshold = 6.0f;
    catmullClarkConfig.objPath = ctx.resourcePath + "/assets/suzanne.obj";

    if (!catmullClarkSystem.init(catmullClarkInfo, catmullClarkConfig)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize CatmullClarkSystem");
        return false;
    }

    return true;
}

bool RendererInit::initHiZSystem(
    HiZSystem& hiZSystem,
    const InitContext& ctx,
    VkFormat depthFormat,
    VkImageView hdrDepthView,
    VkSampler depthSampler
) {
    if (!hiZSystem.init(ctx, depthFormat)) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        return true;  // Don't fail - Hi-Z is optional
    }

    // Connect depth buffer to Hi-Z system
    hiZSystem.setDepthBuffer(hdrDepthView, depthSampler);

    return true;
}

bool RendererInit::initTreeEditSystem(
    TreeEditSystem& treeEditSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    TreeEditSystem::InitInfo treeEditInfo{};
    treeEditInfo.device = ctx.device;
    treeEditInfo.physicalDevice = ctx.physicalDevice;
    treeEditInfo.allocator = ctx.allocator;
    treeEditInfo.renderPass = hdrRenderPass;
    treeEditInfo.descriptorPool = ctx.descriptorPool;
    treeEditInfo.extent = ctx.extent;
    treeEditInfo.shaderPath = ctx.shaderPath;
    treeEditInfo.framesInFlight = ctx.framesInFlight;
    treeEditInfo.graphicsQueue = ctx.graphicsQueue;
    treeEditInfo.commandPool = ctx.commandPool;

    if (!treeEditSystem.init(treeEditInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize TreeEditSystem");
        return false;
    }

    return true;
}

bool RendererInit::initDebugLineSystem(
    DebugLineSystem& debugLineSystem,
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    if (!debugLineSystem.init(ctx.device, ctx.allocator, hdrRenderPass,
                              ctx.shaderPath, ctx.framesInFlight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize debug line system");
        return false;
    }
    SDL_Log("Debug line system initialized");
    return true;
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
