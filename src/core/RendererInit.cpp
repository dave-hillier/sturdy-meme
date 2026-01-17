#include <array>
#include "RendererInit.h"
#include "RendererSystems.h"
#include "CommandBufferUtils.h"
#include "MaterialDescriptorFactory.h"

// Atmosphere systems
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "SSRSystem.h"

// Water systems
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"

// Other systems
#include "PostProcessSystem.h"
#include "TerrainSystem.h"
#include "ShadowSystem.h"
#include "MaterialRegistry.h"
#include "SkinnedMeshRenderer.h"
#include "RockSystem.h"
#include "DetritusSystem.h"

#include <SDL3/SDL.h>

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

    // SSR system is created by WaterSystemGroup::createAll() before this function

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

void RendererInit::updateCloudShadowBindings(
    VkDevice device,
    MaterialRegistry& materialRegistry,
    RockSystem& rockSystem,
    DetritusSystem* detritusSystem,
    SkinnedMeshRenderer& skinnedMeshRenderer,
    VkImageView cloudShadowView,
    VkSampler cloudShadowSampler,
    uint32_t frameCount
) {
    // Update MaterialRegistry-managed descriptor sets
    materialRegistry.updateCloudShadowBinding(device, cloudShadowView, cloudShadowSampler);

    // Update descriptor sets owned by systems (rocks, detritus)
    MaterialDescriptorFactory factory(device);
    if (rockSystem.hasDescriptorSets()) {
        for (uint32_t i = 0; i < frameCount; i++) {
            factory.updateCloudShadowBinding(rockSystem.getDescriptorSet(i), cloudShadowView, cloudShadowSampler);
        }
    }
    if (detritusSystem && detritusSystem->hasDescriptorSets()) {
        for (uint32_t i = 0; i < frameCount; i++) {
            factory.updateCloudShadowBinding(detritusSystem->getDescriptorSet(i), cloudShadowView, cloudShadowSampler);
        }
    }

    // Update skinned mesh renderer cloud shadow binding
    skinnedMeshRenderer.updateCloudShadowBinding(cloudShadowView, cloudShadowSampler);
}
