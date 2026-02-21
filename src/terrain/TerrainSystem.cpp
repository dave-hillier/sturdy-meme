#include "TerrainSystem.h"
#include "TerrainBuffers.h"
#include "TerrainCameraOptimizer.h"
#include "TerrainEffects.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>

std::unique_ptr<TerrainSystem> TerrainSystem::create(const InitContext& ctx,
                                                      const TerrainInitParams& params,
                                                      const TerrainConfig& config) {
    auto system = std::make_unique<TerrainSystem>(ConstructToken{});
    if (!system->initInternal(ctx, params, config)) {
        return nullptr;
    }
    return system;
}

TerrainSystem::~TerrainSystem() {
    cleanup();
}

bool TerrainSystem::initInternal(const InitInfo& info, const TerrainConfig& cfg) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem: raiiDevice is null");
        return false;
    }
    raiiDevice_ = info.raiiDevice;
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    raiiDevice_ = info.raiiDevice;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    texturePath = info.texturePath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    config = cfg;

    // Initialize textures with RAII wrapper
    TerrainTextures::InitInfo texturesInfo{};
    texturesInfo.raiiDevice = raiiDevice_;
    texturesInfo.device = device;
    texturesInfo.allocator = allocator;
    texturesInfo.graphicsQueue = graphicsQueue;
    texturesInfo.commandPool = commandPool;
    texturesInfo.resourcePath = texturePath;
    textures = TerrainTextures::create(texturesInfo);
    if (!textures) return false;

    // Initialize CBT
    TerrainCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.initDepth = 6;  // Start with 64 triangles
    cbt = TerrainCBT::create(cbtInfo);
    if (!cbt) return false;

    // Initialize meshlet for high-resolution rendering
    if (config.useMeshlets) {
        TerrainMeshlet::InitInfo meshletInfo{};
        meshletInfo.allocator = allocator;
        meshletInfo.subdivisionLevel = static_cast<uint32_t>(config.meshletSubdivisionLevel);
        meshletInfo.framesInFlight = framesInFlight;  // For per-frame staging buffers
        meshlet = TerrainMeshlet::create(meshletInfo);
        if (!meshlet) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet, falling back to direct triangles");
            config.useMeshlets = false;
        }
    }

    // Initialize tile cache for LOD-based height streaming (if configured) with RAII wrapper
    if (!config.tileCacheDir.empty()) {
        TerrainTileCache::InitInfo tileCacheInfo{};
        tileCacheInfo.raiiDevice = raiiDevice_;
        tileCacheInfo.cacheDirectory = config.tileCacheDir;
        tileCacheInfo.device = device;
        tileCacheInfo.allocator = allocator;
        tileCacheInfo.graphicsQueue = graphicsQueue;
        tileCacheInfo.commandPool = commandPool;
        tileCacheInfo.terrainSize = config.size;
        tileCacheInfo.heightScale = config.heightScale;
        tileCacheInfo.yieldCallback = yieldCallback_;
        tileCache = TerrainTileCache::create(tileCacheInfo);
        if (!tileCache) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize tile cache, using global heightmap only");
        } else {
            SDL_Log("Tile cache initialized: %s", config.tileCacheDir.c_str());

            // Pre-load tiles around origin for immediate height queries during scene init
            // This ensures objects spawned near (0,0) use high-res terrain heights
            // Radius 600m covers ~2 tiles in each direction from origin (tiles are ~512m each)
            tileCache->preloadTilesAround(0.0f, 0.0f, 600.0f);
        }
    }

    // Initialize virtual texture system (if configured)
    if (config.useVirtualTexture && !config.virtualTextureTileDir.empty()) {
        virtualTexture = std::make_unique<VirtualTexture::VirtualTextureSystem>();
        VirtualTexture::VirtualTextureConfig vtConfig{};
        // Use smaller config for testing - 64 tiles per axis, 6 mip levels
        vtConfig.virtualSizePixels = 8192;  // 64 * 128 = 8192
        vtConfig.tileSizePixels = 128;
        vtConfig.cacheSizePixels = 2048;    // 16x16 tiles in cache
        vtConfig.borderPixels = 4;
        vtConfig.maxMipLevels = 6;

        VirtualTexture::VirtualTextureSystem::InitInfo vtInfo;
        vtInfo.raiiDevice = raiiDevice_;
        vtInfo.device = device;
        vtInfo.allocator = allocator;
        vtInfo.commandPool = commandPool;
        vtInfo.queue = graphicsQueue;
        vtInfo.tilePath = config.virtualTextureTileDir;
        vtInfo.config = vtConfig;
        vtInfo.framesInFlight = framesInFlight;

        if (!virtualTexture->init(vtInfo)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize virtual texture system");
            virtualTexture.reset();
        } else {
            SDL_Log("Virtual texture system initialized: %s", config.virtualTextureTileDir.c_str());
        }
    }

    // Query GPU subgroup capabilities for optimized compute paths
    querySubgroupCapabilities();

    // Initialize buffers subsystem
    TerrainBuffers::InitInfo bufferInfo{};
    bufferInfo.allocator = allocator;
    bufferInfo.framesInFlight = framesInFlight;
    bufferInfo.maxVisibleTriangles = MAX_VISIBLE_TRIANGLES;
    buffers = TerrainBuffers::create(bufferInfo);
    if (!buffers) return false;

    // Initialize effects subsystem
    TerrainEffects::InitInfo effectsInfo{};
    effectsInfo.framesInFlight = framesInFlight;
    effects.init(effectsInfo);

    // Create descriptor set layouts and sets
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;

    // Initialize pipelines subsystem (RAII-managed)
    TerrainPipelines::InitInfo pipelineInfo{};
    pipelineInfo.raiiDevice = raiiDevice_;
    pipelineInfo.device = device;
    pipelineInfo.physicalDevice = physicalDevice;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.shadowRenderPass = shadowRenderPass;
    pipelineInfo.computeDescriptorSetLayout = computeDescriptorSetLayout;
    pipelineInfo.renderDescriptorSetLayout = renderDescriptorSetLayout;
    pipelineInfo.shaderPath = shaderPath;
    pipelineInfo.useMeshlets = config.useMeshlets;
    pipelineInfo.meshletIndexCount = config.useMeshlets && meshlet ? meshlet->getIndexCount() : 0;
    pipelineInfo.subgroupCaps = &subgroupCaps;
    pipelines = TerrainPipelines::create(pipelineInfo);
    if (!pipelines) return false;

    SDL_Log("TerrainSystem initialized with CBT max depth %d, meshlets %s, shadow culling %s",
            config.maxDepth, config.useMeshlets ? "enabled" : "disabled",
            shadowCullingEnabled ? "enabled" : "disabled");
    return true;
}

bool TerrainSystem::initInternal(const InitContext& ctx, const TerrainInitParams& params, const TerrainConfig& cfg) {
    // Store yield callback for use during initialization
    yieldCallback_ = params.yieldCallback;

    InitInfo info{};
    info.raiiDevice = ctx.raiiDevice;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.raiiDevice = ctx.raiiDevice;
    info.renderPass = params.renderPass;
    info.shadowRenderPass = params.shadowRenderPass;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shadowMapSize = params.shadowMapSize;
    info.shaderPath = ctx.shaderPath;
    info.texturePath = params.texturePath;
    info.framesInFlight = ctx.framesInFlight;
    info.graphicsQueue = ctx.graphicsQueue;
    info.commandPool = ctx.commandPool;
    return initInternal(info, cfg);
}

void TerrainSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;  // Not initialized
    vk::Device vkDevice(device);
    vkDevice.waitIdle();

    // RAII-managed subsystems destroyed automatically via std::optional reset
    pipelines.reset();

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDevice.destroyDescriptorSetLayout(computeDescriptorSetLayout);
    if (renderDescriptorSetLayout) vkDevice.destroyDescriptorSetLayout(renderDescriptorSetLayout);

    // Reset all RAII-managed subsystems
    buffers.reset();
    if (virtualTexture) {
        virtualTexture->destroy(device, allocator);
        virtualTexture.reset();
    }
    tileCache.reset();
    meshlet.reset();
    cbt.reset();
    textures.reset();
}


uint32_t TerrainSystem::getTriangleCount() const {
    void* mappedPtr = buffers->getIndirectDrawMappedPtr();
    if (!mappedPtr) {
        return 0;
    }
    // Indirect draw buffer layout depends on rendering mode:
    // - Meshlet mode: {indexCount, instanceCount, ...} where total = instanceCount * meshletTriangles
    // - Direct mode: {vertexCount, instanceCount, ...} where total = vertexCount / 3
    const uint32_t* drawArgs = static_cast<const uint32_t*>(mappedPtr);
    if (config.useMeshlets) {
        uint32_t instanceCount = drawArgs[1];  // Number of CBT leaf nodes
        return instanceCount * meshlet->getTriangleCount();
    } else {
        return drawArgs[0] / 3;
    }
}

void TerrainSystem::querySubgroupCapabilities() {
    auto subgroupProps = vk::PhysicalDeviceSubgroupProperties{};
    auto deviceProps2 = vk::PhysicalDeviceProperties2{};
    deviceProps2.pNext = &subgroupProps;

    vk::PhysicalDevice vkPhysDevice(physicalDevice);
    vkPhysDevice.getProperties2(&deviceProps2);

    subgroupCaps.subgroupSize = subgroupProps.subgroupSize;
    subgroupCaps.hasSubgroupArithmetic =
        (subgroupProps.supportedOperations & vk::SubgroupFeatureFlagBits::eArithmetic) != vk::SubgroupFeatureFlags{};

    SDL_Log("TerrainSystem: Subgroup size=%u, arithmetic=%s",
            subgroupCaps.subgroupSize,
            subgroupCaps.hasSubgroupArithmetic ? "yes" : "no");
}

void TerrainSystem::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );
    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
}

float TerrainSystem::getHeightAt(float x, float z) const {
    // Tile cache is required - base LOD tiles cover entire terrain
    if (!tileCache) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainSystem::getHeightAt(%.1f, %.1f): tile cache not initialized - cannot query height",
                     x, z);
        return 0.0f;
    }

    float tileHeight;
    if (tileCache->getHeightAt(x, z, tileHeight)) {
        return tileHeight;
    }

    // This should never happen - base LOD tiles cover entire terrain
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainSystem::getHeightAt(%.1f, %.1f): tile cache miss - base LOD should cover entire terrain",
                 x, z);
    return 0.0f;
}

TerrainSystem::HeightQueryInfo TerrainSystem::getHeightAtDebug(float x, float z) const {
    HeightQueryInfo result{};
    result.found = false;
    result.height = 0.0f;
    result.tileX = 0;
    result.tileZ = 0;
    result.lod = 0;
    result.source = "none";

    if (!tileCache) {
        return result;
    }

    auto cacheInfo = tileCache->getHeightAtDebug(x, z);
    result.height = cacheInfo.height;
    result.tileX = cacheInfo.tileX;
    result.tileZ = cacheInfo.tileZ;
    result.lod = cacheInfo.lod;
    result.source = cacheInfo.source;
    result.found = cacheInfo.found;
    return result;
}

bool TerrainSystem::setMeshletSubdivisionLevel(int level) {
    if (level < 0 || level > 6) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Meshlet subdivision level %d out of range [0-6], clamping", level);
        level = std::clamp(level, 0, 6);
    }

    if (level == config.meshletSubdivisionLevel) {
        return true;  // No change needed
    }

    if (!meshlet) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Cannot change meshlet subdivision level: meshlet not initialized");
        return false;
    }

    // Request subdivision change - NO vkDeviceWaitIdle needed!
    // Uses per-frame staging buffers like virtual texture system.
    // The geometry is generated to CPU memory immediately, then uploaded
    // via recordUpload() over the next N frames (where N = framesInFlight).
    if (!meshlet->requestSubdivisionChange(static_cast<uint32_t>(level))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to request meshlet subdivision change to level %d", level);
        return false;
    }

    config.meshletSubdivisionLevel = level;
    SDL_Log("Meshlet subdivision level changed to %d (%u triangles per leaf)",
            level, meshlet->getTriangleCount());
    return true;
}
