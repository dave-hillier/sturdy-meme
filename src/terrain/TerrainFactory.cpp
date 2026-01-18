#include "TerrainFactory.h"

std::unique_ptr<TerrainSystem> TerrainFactory::create(const InitContext& ctx, const Config& config) {
    // Build terrain init params from config
    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = config.hdrRenderPass;
    terrainParams.shadowRenderPass = config.shadowRenderPass;
    terrainParams.shadowMapSize = config.shadowMapSize;
    terrainParams.texturePath = config.resourcePath + "/textures";

    // Build terrain config with sensible defaults
    TerrainConfig terrainConfig = buildTerrainConfig(config);

    return TerrainSystem::create(ctx, terrainParams, terrainConfig);
}

TerrainConfig TerrainFactory::buildTerrainConfig(const Config& config) {
    TerrainConfig terrainConfig{};
    terrainConfig.size = config.size;
    terrainConfig.maxDepth = config.maxDepth;
    terrainConfig.minDepth = config.minDepth;
    terrainConfig.targetEdgePixels = config.targetEdgePixels;
    terrainConfig.splitThreshold = config.splitThreshold;
    terrainConfig.mergeThreshold = config.mergeThreshold;
    terrainConfig.minAltitude = config.minAltitude;
    terrainConfig.maxAltitude = config.maxAltitude;

    // LOD tile streaming
    terrainConfig.tileCacheDir = config.resourcePath + "/terrain_data";
    terrainConfig.tileLoadRadius = config.tileLoadRadius;
    terrainConfig.tileUnloadRadius = config.tileUnloadRadius;

    // Virtual texturing
    terrainConfig.virtualTextureTileDir = config.resourcePath + "/vt_tiles";
    terrainConfig.useVirtualTexture = config.useVirtualTexture;

    return terrainConfig;
}
