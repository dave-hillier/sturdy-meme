// WaterSystemGroup.cpp - Self-initialization and configuration for water systems

#include "WaterSystemGroup.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "RendererSystems.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "PostProcessSystem.h"
#include <SDL3/SDL.h>
#include <array>

void WaterSystemGroup::Bundle::registerAll(RendererSystems& systems) {
    systems.registry().add<WaterSystem>(std::move(system));
    systems.registry().add<WaterDisplacement>(std::move(displacement));
    systems.registry().add<FlowMapGenerator>(std::move(flowMap));
    systems.registry().add<FoamBuffer>(std::move(foam));
    systems.registry().add<SSRSystem>(std::move(ssr));
    if (tileCull) systems.registry().add<WaterTileCull>(std::move(tileCull));
    if (gBuffer) systems.registry().add<WaterGBuffer>(std::move(gBuffer));
}

bool WaterSystemGroup::createAndRegister(const CreateDeps& deps, RendererSystems& systems) {
    auto bundle = createAll(deps);
    if (!bundle) return false;
    bundle->registerAll(systems);
    return true;
}

std::optional<WaterSystemGroup::Bundle> WaterSystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;
    const auto& ctx = deps.ctx;

    // 1. Create WaterSystem (main water rendering)
    {
        WaterSystem::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.descriptorPool = ctx.descriptorPool;
        info.hdrRenderPass = deps.hdrRenderPass;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.extent = ctx.extent;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.waterSize = deps.waterSize;
        info.assetPath = deps.assetPath;
        info.raiiDevice = ctx.raiiDevice;

        bundle.system = WaterSystem::create(info);
        if (!bundle.system) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create WaterSystem");
            return std::nullopt;
        }
    }

    // 2. Create FlowMapGenerator
    {
        FlowMapGenerator::InitInfo info{};
        info.device = ctx.device;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.queue = ctx.graphicsQueue;
        info.raiiDevice = ctx.raiiDevice;

        bundle.flowMap = FlowMapGenerator::create(info);
        if (!bundle.flowMap) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create FlowMapGenerator");
            return std::nullopt;
        }
    }

    // 3. Create WaterDisplacement (FFT waves)
    {
        WaterDisplacement::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.framesInFlight = ctx.framesInFlight;
        info.displacementResolution = 512;
        info.worldSize = deps.waterSize;
        info.raiiDevice = ctx.raiiDevice;
        info.shaderPath = ctx.shaderPath;

        bundle.displacement = WaterDisplacement::create(info);
        if (!bundle.displacement) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create WaterDisplacement");
            return std::nullopt;
        }
    }

    // 4. Create FoamBuffer
    {
        FoamBuffer::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.resolution = 512;
        info.worldSize = deps.waterSize;
        info.raiiDevice = ctx.raiiDevice;

        bundle.foam = FoamBuffer::create(info);
        if (!bundle.foam) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create FoamBuffer");
            return std::nullopt;
        }
    }

    // 5. Create SSRSystem
    bundle.ssr = SSRSystem::create(ctx);
    if (!bundle.ssr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create SSRSystem");
        return std::nullopt;
    }

    // 6. Create WaterTileCull (optional)
    {
        WaterTileCull::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.extent = ctx.extent;
        info.tileSize = 32;
        info.raiiDevice = ctx.raiiDevice;

        bundle.tileCull = WaterTileCull::create(info);
        if (!bundle.tileCull) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: WaterTileCull creation failed (non-fatal)");
        }
    }

    // 7. Create WaterGBuffer (optional)
    {
        WaterGBuffer::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.fullResExtent = ctx.extent;
        info.resolutionScale = 0.5f;
        info.framesInFlight = ctx.framesInFlight;
        info.shaderPath = ctx.shaderPath;
        info.descriptorPool = ctx.descriptorPool;
        info.raiiDevice = ctx.raiiDevice;

        bundle.gBuffer = WaterGBuffer::create(info);
        if (!bundle.gBuffer) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: WaterGBuffer creation failed (non-fatal)");
        }
    }

    SDL_Log("WaterSystemGroup: All systems created successfully");
    return bundle;
}

bool WaterSystemGroup::configureSubsystems(
    RendererSystems& systems,
    const TerrainConfig& terrainConfig
) {
    float seaLevel = terrainConfig.seaLevel;

    // Configure water surface
    auto& water = systems.water();
    water.setWaterLevel(seaLevel);
    water.setWaterExtent(glm::vec2(0.0f, 0.0f), glm::vec2(65536.0f, 65536.0f));
    // English estuary/coastal water style - murky grey-green, moderate chop
    water.setWaterColor(glm::vec4(0.15f, 0.22f, 0.25f, 0.9f));
    water.setWaveAmplitude(0.3f);
    water.setWaveLength(15.0f);
    water.setWaveSteepness(0.25f);
    water.setWaveSpeed(0.5f);
    water.setTidalRange(3.0f);
    water.setTerrainParams(terrainConfig.size, terrainConfig.heightScale);
    water.setShoreBlendDistance(8.0f);
    water.setShoreFoamWidth(15.0f);
    water.setCameraPlanes(0.1f, 50000.0f);

    // Generate flow map from terrain data
    FlowMapGenerator::Config flowConfig{};
    flowConfig.resolution = 512;
    flowConfig.worldSize = terrainConfig.size;
    flowConfig.waterLevel = seaLevel;
    flowConfig.maxFlowSpeed = 1.0f;
    flowConfig.slopeInfluence = 2.0f;
    flowConfig.shoreDistance = 100.0f;

    const float* heightData = systems.terrain().getHeightMapData();
    uint32_t heightRes = systems.terrain().getHeightMapResolution();
    if (heightData && heightRes > 0) {
        std::vector<float> heightVec(heightData, heightData + heightRes * heightRes);
        if (!systems.flowMap().generateFromTerrain(heightVec, heightRes, terrainConfig.heightScale, flowConfig)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Flow map generation failed, using radial flow fallback");
            systems.flowMap().generateRadialFlow(flowConfig, glm::vec2(0.0f));
        }
    } else {
        SDL_Log("No terrain height data available, generating radial flow map");
        systems.flowMap().generateRadialFlow(flowConfig, glm::vec2(0.0f));
    }

    return true;
}

bool WaterSystemGroup::createDescriptorSets(
    RendererSystems& systems,
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

    if (!systems.water().createDescriptorSets(
            uniformBuffers, uniformBufferSize, shadowSystem,
            terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
            systems.flowMap().getFlowMapView(), systems.flowMap().getFlowMapSampler(),
            systems.waterDisplacement().getDisplacementMapView(), systems.waterDisplacement().getSampler(),
            systems.foam().getFoamBufferView(), systems.foam().getSampler(),
            systems.ssr().getSSRResultView(), systems.ssr().getSampler(),
            postProcessSystem.getHDRDepthView(), depthSampler,
            terrainSystem.getTileArrayView(), terrainSystem.getTileSampler(),
            waterTileInfoBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create water descriptor sets");
        return false;
    }

    // Create water G-buffer descriptor sets
    auto waterGroup = systems.waterGroup();
    if (waterGroup.hasGBuffer() && waterGroup.gBuffer()->getPipeline() != VK_NULL_HANDLE) {
        if (!waterGroup.gBuffer()->createDescriptorSets(
                uniformBuffers, uniformBufferSize,
                systems.water().getUniformBuffers(), WaterSystem::getUniformBufferSize(),
                terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
                systems.flowMap().getFlowMapView(), systems.flowMap().getFlowMapSampler())) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create water G-buffer descriptor sets");
        }
    }

    return true;
}
