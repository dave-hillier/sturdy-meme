#include "TerrainSystem.h"
#include "TerrainBuffers.h"
#include "TerrainCameraOptimizer.h"
#include "DescriptorManager.h"
#include "GpuProfiler.h"
#include "UBOs.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>

std::unique_ptr<TerrainSystem> TerrainSystem::create(const InitContext& ctx,
                                                      const TerrainInitParams& params,
                                                      const TerrainConfig& config) {
    std::unique_ptr<TerrainSystem> system(new TerrainSystem());
    if (!system->initInternal(ctx, params, config)) {
        return nullptr;
    }
    return system;
}

TerrainSystem::~TerrainSystem() {
    cleanup();
}

bool TerrainSystem::initInternal(const InitInfo& info, const TerrainConfig& cfg) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
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

    // Compute heightScale from altitude range
    config.heightScale = config.maxAltitude - config.minAltitude;

    // Initialize height map with RAII wrapper
    TerrainHeightMap::InitInfo heightMapInfo{};
    heightMapInfo.device = device;
    heightMapInfo.allocator = allocator;
    heightMapInfo.graphicsQueue = graphicsQueue;
    heightMapInfo.commandPool = commandPool;
    heightMapInfo.resolution = 512;
    heightMapInfo.terrainSize = config.size;
    heightMapInfo.heightScale = config.heightScale;
    heightMapInfo.heightmapPath = config.heightmapPath;
    heightMapInfo.minAltitude = config.minAltitude;
    heightMapInfo.maxAltitude = config.maxAltitude;
    heightMap = TerrainHeightMap::create(heightMapInfo);
    if (!heightMap) return false;

    // Initialize textures with RAII wrapper
    TerrainTextures::InitInfo texturesInfo{};
    texturesInfo.device = device;
    texturesInfo.allocator = allocator;
    texturesInfo.graphicsQueue = graphicsQueue;
    texturesInfo.commandPool = commandPool;
    texturesInfo.resourcePath = texturePath;
    textures = RAIIAdapter<TerrainTextures>::create(
        [&](auto& t) { return t.init(texturesInfo); },
        [this](auto& t) { t.destroy(device, allocator); }
    );
    if (!textures) return false;

    // Initialize CBT with RAII wrapper
    TerrainCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.initDepth = 6;  // Start with 64 triangles
    cbt = RAIIAdapter<TerrainCBT>::create(
        [&](auto& c) { return c.init(cbtInfo); },
        [](auto& c) { c.destroy(); }
    );
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
        tileCacheInfo.cacheDirectory = config.tileCacheDir;
        tileCacheInfo.device = device;
        tileCacheInfo.allocator = allocator;
        tileCacheInfo.graphicsQueue = graphicsQueue;
        tileCacheInfo.commandPool = commandPool;
        tileCacheInfo.terrainSize = config.size;
        tileCacheInfo.heightScale = config.heightScale;
        tileCacheInfo.minAltitude = config.minAltitude;
        tileCacheInfo.maxAltitude = config.maxAltitude;
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

    // Query GPU subgroup capabilities for optimized compute paths
    querySubgroupCapabilities();

    // Initialize buffers subsystem with RAII wrapper
    TerrainBuffers::InitInfo bufferInfo{};
    bufferInfo.allocator = allocator;
    bufferInfo.framesInFlight = framesInFlight;
    bufferInfo.maxVisibleTriangles = MAX_VISIBLE_TRIANGLES;
    buffers = RAIIAdapter<TerrainBuffers>::create(
        [&](auto& b) { return b.init(bufferInfo); },
        [this](auto& b) { b.destroy(allocator); }
    );
    if (!buffers) return false;

    // Create descriptor set layouts and sets
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;

    // Initialize pipelines subsystem (RAII-managed)
    TerrainPipelines::InitInfo pipelineInfo{};
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
    InitInfo info{};
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
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
    vkDeviceWaitIdle(device);

    // RAII-managed subsystems destroyed automatically via std::optional reset
    pipelines.reset();

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);

    // Reset all RAII-managed subsystems
    buffers.reset();
    tileCache.reset();
    meshlet.reset();
    cbt.reset();
    textures.reset();
    heightMap.reset();
}


uint32_t TerrainSystem::getTriangleCount() const {
    void* mappedPtr = (*buffers)->getIndirectDrawMappedPtr();
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

bool TerrainSystem::createComputeDescriptorSetLayout() {
    // Compute bindings:
    // 0: CBT buffer, 1: indirect dispatch, 2: indirect draw, 3: height map
    // 4: terrain uniforms, 5: visible indices, 6: cull indirect dispatch
    // 14: shadow visible indices, 15: shadow indirect draw

    computeDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 0: CBT buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 1: indirect dispatch
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: indirect draw
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 3: height map
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: terrain uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 5: visible indices
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 6: cull indirect dispatch
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // shadow visible indices
        .addBinding(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // shadow indirect draw
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)  // tile array
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)          // tile info
        .build();

    return computeDescriptorSetLayout != VK_NULL_HANDLE;
}

bool TerrainSystem::createRenderDescriptorSetLayout() {
    // Render bindings:
    // 0: CBT buffer (vertex), 3: height map, 4: terrain uniforms, 5: scene UBO
    // 6: terrain albedo, 7: shadow map, 8: grass far LOD, 9: snow mask
    // 10-12: volumetric snow cascades, 13: cloud shadow map
    // 14: shadow visible indices, 16: hole mask
    // 17: snow UBO, 18: cloud shadow UBO
    // 19: tile array texture, 20: tile info SSBO

    renderDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 0
        .addBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 1
        .addBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // snow cascade 2
        .addBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // cloud shadow map
        .addBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)            // shadow visible indices
        .addBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // hole mask
        .addBinding(17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // snow UBO
        .addBinding(18, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)          // cloud shadow UBO
        .addBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT)    // tile array texture
        .addBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)            // tile info SSBO
        .build();

    return renderDescriptorSetLayout != VK_NULL_HANDLE;
}

bool TerrainSystem::createDescriptorSets() {
    // Allocate compute descriptor sets using managed pool
    computeDescriptorSets = descriptorPool->allocate(computeDescriptorSetLayout, framesInFlight);
    if (computeDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem: Failed to allocate compute descriptor sets");
        return false;
    }

    // Allocate render descriptor sets using managed pool
    renderDescriptorSets = descriptorPool->allocate(renderDescriptorSetLayout, framesInFlight);
    if (renderDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem: Failed to allocate render descriptor sets");
        return false;
    }

    // Update compute descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Get tile cache resources if available
        VkImageView tileArrayView = VK_NULL_HANDLE;
        VkSampler tileSampler = VK_NULL_HANDLE;
        if (tileCache) {
            tileArrayView = tileCache->getTileArrayView();
            tileSampler = tileCache->getSampler();
        }

        // Build writer with all bindings - use separate statements to avoid
        // copy/reference issues with the fluent API pattern
        DescriptorManager::SetWriter writer(device, computeDescriptorSets[i]);
        writer.writeBuffer(0, (*cbt)->getBuffer(), 0, (*cbt)->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, (*buffers)->getIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, (*buffers)->getIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeImage(3, heightMap->getView(), heightMap->getSampler());
        writer.writeBuffer(4, (*buffers)->getUniformBuffer(i), 0, sizeof(TerrainUniforms));
        writer.writeBuffer(5, (*buffers)->getVisibleIndicesBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(6, (*buffers)->getCullIndirectDispatchBuffer(), 0, sizeof(VkDispatchIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(14, (*buffers)->getShadowVisibleBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(15, (*buffers)->getShadowIndirectDrawBuffer(), 0, sizeof(VkDrawIndexedIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // LOD tile cache bindings (19 and 20) - for subdivision to use high-res terrain data
        // Note: tile info buffer (binding 20) is updated per-frame in recordCompute
        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            writer.writeImage(19, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame i) - will be updated per-frame in recordCompute
        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        writer.update();
    }
    return true;
}


void TerrainSystem::querySubgroupCapabilities() {
    VkPhysicalDeviceSubgroupProperties subgroupProps{};
    subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps2.pNext = &subgroupProps;

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProps2);

    subgroupCaps.subgroupSize = subgroupProps.subgroupSize;
    subgroupCaps.hasSubgroupArithmetic =
        (subgroupProps.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0;

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

void TerrainSystem::updateDescriptorSets(VkDevice device,
                                          const std::vector<VkBuffer>& sceneUniformBuffers,
                                          VkImageView shadowMapView,
                                          VkSampler shadowSampler,
                                          const std::vector<VkBuffer>& snowUBOBuffers,
                                          const std::vector<VkBuffer>& cloudShadowUBOBuffers) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        auto writer = DescriptorManager::SetWriter(device, renderDescriptorSets[i]);

        // CBT buffer (binding 0)
        writer.writeBuffer(0, (*cbt)->getBuffer(), 0, (*cbt)->getBufferSize(),
                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // Height map (binding 3)
        writer.writeImage(3, heightMap->getView(), heightMap->getSampler(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Terrain uniforms (binding 4)
        writer.writeBuffer(4, (*buffers)->getUniformBuffer(i), 0, sizeof(TerrainUniforms));

        // Scene UBO (binding 5)
        if (i < sceneUniformBuffers.size()) {
            writer.writeBuffer(5, sceneUniformBuffers[i], 0, VK_WHOLE_SIZE);
        }

        // Terrain albedo (binding 6)
        writer.writeImage(6, (*textures)->getAlbedoView(), (*textures)->getAlbedoSampler(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Shadow map (binding 7)
        if (shadowMapView != VK_NULL_HANDLE) {
            writer.writeImage(7, shadowMapView, shadowSampler,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        // Grass far LOD texture (binding 8)
        if ((*textures)->getGrassFarLODView() != VK_NULL_HANDLE) {
            writer.writeImage(8, (*textures)->getGrassFarLODView(), (*textures)->getGrassFarLODSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Shadow visible indices (binding 14)
        if ((*buffers)->getShadowVisibleBuffer() != VK_NULL_HANDLE) {
            writer.writeBuffer(14, (*buffers)->getShadowVisibleBuffer(), 0,
                              sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES),
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Hole mask (binding 16)
        writer.writeImage(16, heightMap->getHoleMaskView(), heightMap->getHoleMaskSampler(),
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Snow UBO (binding 17)
        if (i < snowUBOBuffers.size() && snowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(17, snowUBOBuffers[i], 0, sizeof(SnowUBO));
        }

        // Cloud shadow UBO (binding 18)
        if (i < cloudShadowUBOBuffers.size() && cloudShadowUBOBuffers[i] != VK_NULL_HANDLE) {
            writer.writeBuffer(18, cloudShadowUBOBuffers[i], 0, sizeof(CloudShadowUBO));
        }

        // LOD tile array texture (binding 19)
        if (tileCache && tileCache->getTileArrayView() != VK_NULL_HANDLE) {
            writer.writeImage(19, tileCache->getTileArrayView(), tileCache->getSampler(),
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // LOD tile info buffer (binding 20) - use per-frame buffer
        // Note: This is also updated per-frame in recordDraw for proper sync
        if (tileCache && tileCache->getTileInfoBuffer(i) != VK_NULL_HANDLE) {
            writer.writeBuffer(20, tileCache->getTileInfoBuffer(i), 0, VK_WHOLE_SIZE,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        writer.update();
    }
}

void TerrainSystem::setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(9, snowMaskView, snowMaskSampler)
            .update();
    }
}

void TerrainSystem::setVolumetricSnowCascades(VkDevice device,
                                               VkImageView cascade0View, VkImageView cascade1View, VkImageView cascade2View,
                                               VkSampler cascadeSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(10, cascade0View, cascadeSampler)
            .writeImage(11, cascade1View, cascadeSampler)
            .writeImage(12, cascade2View, cascadeSampler)
            .update();
    }
}

void TerrainSystem::setCloudShadowMap(VkDevice device, VkImageView cloudShadowView, VkSampler cloudShadowSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[i])
            .writeImage(13, cloudShadowView, cloudShadowSampler)
            .update();
    }
}

void TerrainSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                    const glm::mat4& view, const glm::mat4& proj,
                                    const std::array<glm::vec4, 3>& snowCascadeParams,
                                    bool useVolumetricSnow,
                                    float snowMaxHeight) {
    // Track camera movement for skip-frame optimization
    cameraOptimizer.update(cameraPos, view);

    // Update tile cache - stream high-res tiles based on camera position
    // Set frame index first so tile info buffer writes to the correct triple-buffered slot
    if (tileCache) {
        tileCache->setCurrentFrameIndex(frameIndex);
        tileCache->updateActiveTiles(cameraPos, config.tileLoadRadius, config.tileUnloadRadius);
    }

    TerrainUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    uniforms.terrainParams = glm::vec4(
        config.size,
        config.heightScale,
        config.targetEdgePixels,
        static_cast<float>(config.maxDepth)
    );

    uniforms.lodParams = glm::vec4(
        config.splitThreshold,
        config.mergeThreshold,
        static_cast<float>(config.minDepth),
        static_cast<float>(subdivisionFrameCount & 1)  // 0 = split phase, 1 = merge phase
    );

    uniforms.screenSize = glm::vec2(extent.width, extent.height);

    // Compute LOD factor for screen-space edge length calculation
    float fov = 2.0f * atan(1.0f / proj[1][1]);
    uniforms.lodFactor = 2.0f * log2(extent.height / (2.0f * tan(fov * 0.5f) * config.targetEdgePixels));
    uniforms.padding = config.flatnessScale;  // flatnessScale in shader

    // Extract frustum planes
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    // Volumetric snow parameters
    uniforms.snowCascade0Params = snowCascadeParams[0];
    uniforms.snowCascade1Params = snowCascadeParams[1];
    uniforms.snowCascade2Params = snowCascadeParams[2];
    uniforms.useVolumetricSnow = useVolumetricSnow ? 1.0f : 0.0f;
    uniforms.snowMaxHeight = snowMaxHeight;
    uniforms.snowPadding1 = 0.0f;
    uniforms.snowPadding2 = 0.0f;

    memcpy((*buffers)->getUniformMappedPtr(frameIndex), &uniforms, sizeof(TerrainUniforms));
}

void TerrainSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler) {
    // Update tile info buffer binding to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device, computeDescriptorSets[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    // Record pending meshlet uploads (fence-free, like virtual texture system)
    if (config.useMeshlets && meshlet && meshlet->hasPendingUpload()) {
        meshlet->recordUpload(cmd, frameIndex);
    }

    // Skip-frame optimization: skip compute when camera is stationary and terrain has converged
    if (cameraOptimizer.shouldSkipCompute()) {
        cameraOptimizer.recordComputeSkipped();

        // Still need the final barrier for rendering (CBT state unchanged but GPU needs it)
        Barriers::computeToIndirectDraw(cmd);
        return;
    }

    // Reset skip tracking
    cameraOptimizer.recordComputeExecuted();

    // 1. Dispatcher - set up indirect args
    if (profiler) profiler->beginZone(cmd, "Terrain:Dispatcher");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getDispatcherPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getDispatcherPipelineLayout(),
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletIndexCount = config.useMeshlets ? meshlet->getIndexCount() : 0;
    vkCmdPushConstants(cmd, pipelines->getDispatcherPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);

    vkCmdDispatch(cmd, 1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:Dispatcher");

    Barriers::computeToComputeReadWrite(cmd);

    // 2. Subdivision - LOD update with inline frustum culling
    // Ping-pong between split and merge to avoid race conditions
    // Even frames: split only, Odd frames: merge only
    // Note: Frustum culling is now inline in subdivision shader (no separate pass)
    uint32_t updateMode = subdivisionFrameCount & 1;  // 0 = split, 1 = merge

    if (updateMode == 0) {
        // Split phase with inline frustum culling
        // No separate frustum cull pass - culling happens inside subdivision shader
        if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSubdivisionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSubdivisionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 0;  // Split
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, pipelines->getSubdivisionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Dispatch all triangles - inline frustum culling handles early-out
        vkCmdDispatchIndirect(cmd, (*buffers)->getIndirectDispatchBuffer(), 0);

        if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
    } else {
        // Merge phase: process all triangles directly (no culling)
        if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSubdivisionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSubdivisionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 1;  // Merge
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, pipelines->getSubdivisionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Use the original indirect dispatch (all triangles)
        vkCmdDispatchIndirect(cmd, (*buffers)->getIndirectDispatchBuffer(), 0);

        if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
    }

    subdivisionFrameCount++;

    Barriers::computeToComputeReadWrite(cmd);

    // 3. Sum reduction - rebuild the sum tree
    // Choose optimized or fallback path based on subgroup support
    if (profiler) profiler->beginZone(cmd, "Terrain:SumReductionPrepass");

    TerrainSumReductionPushConstants sumPC{};
    sumPC.passID = config.maxDepth;

    int levelsFromPrepass;

    if (pipelines->getSumReductionPrepassSubgroupPipeline()) {
        // Subgroup prepass - processes 13 levels:
        // - SWAR popcount: 5 levels (32 bits -> 6-bit sum)
        // - Subgroup shuffle: 5 levels (32 threads -> 11-bit sum)
        // - Shared memory: 3 levels (8 subgroups -> 14-bit sum)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPrepassSubgroupPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, pipelines->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(sumPC), &sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmdDispatch(cmd, workgroups, 1, 1);

        Barriers::computeToComputeReadWrite(cmd);

        levelsFromPrepass = 13;  // SWAR (5) + subgroup (5) + shared memory (3)
    } else {
        // Fallback path: standard prepass handles 5 levels
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPrepassPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, pipelines->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(sumPC), &sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmdDispatch(cmd, workgroups, 1, 1);

        Barriers::computeToComputeReadWrite(cmd);

        levelsFromPrepass = 5;
    }

    if (profiler) profiler->endZone(cmd, "Terrain:SumReductionPrepass");

    // Phase 2: Standard sum reduction for remaining levels (one dispatch per level)
    // Start from level (maxDepth - levelsFromPrepass - 1) down to 0
    int startDepth = config.maxDepth - levelsFromPrepass - 1;
    if (startDepth >= 0) {
        if (profiler) profiler->beginZone(cmd, "Terrain:SumReductionLevels");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        for (int depth = startDepth; depth >= 0; --depth) {
            sumPC.passID = depth;
            vkCmdPushConstants(cmd, pipelines->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(sumPC), &sumPC);

            uint32_t workgroups = std::max(1u, (1u << depth) / SUM_REDUCTION_WORKGROUP_SIZE);
            vkCmdDispatch(cmd, workgroups, 1, 1);

            Barriers::computeToComputeReadWrite(cmd);
        }

        if (profiler) profiler->endZone(cmd, "Terrain:SumReductionLevels");
    }

    // 4. Final dispatcher pass to update draw args
    if (profiler) profiler->beginZone(cmd, "Terrain:FinalDispatch");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getDispatcherPipeline());
    vkCmdPushConstants(cmd, pipelines->getDispatcherPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);
    vkCmdDispatch(cmd, 1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:FinalDispatch");

    // Final barrier before rendering
    Barriers::computeToIndirectDraw(cmd);
}

void TerrainSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Update tile info buffer binding to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    VkPipeline pipeline;
    if (config.useMeshlets) {
        pipeline = wireframeMode ? pipelines->getMeshletWireframePipeline() : pipelines->getMeshletRenderPipeline();
    } else {
        pipeline = wireframeMode ? pipelines->getWireframePipeline() : pipelines->getRenderPipeline();
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->getRenderPipelineLayout(),
                           0, 1, &renderDescriptorSets[frameIndex], 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        VkBuffer vertexBuffers[] = {meshlet->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshlet->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // Indexed instanced draw
        vkCmdDrawIndexedIndirect(cmd, (*buffers)->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        // Direct vertex draw (no vertex buffer - vertices generated from gl_VertexIndex)
        vkCmdDrawIndirect(cmd, (*buffers)->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndirectCommand));
    }
}

void TerrainSystem::recordShadowCull(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!shadowCullingEnabled || !pipelines->hasShadowCulling()) {
        return;
    }

    // Clear the shadow visible count to 0 and barrier for compute
    Barriers::clearBufferForCompute(cmd, (*buffers)->getShadowVisibleBuffer());

    // Bind shadow cull compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getShadowCullPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->getShadowCullPipelineLayout(),
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    // Set up push constants with frustum planes
    TerrainShadowCullPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    extractFrustumPlanes(lightViewProj, pc.lightFrustumPlanes);
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = static_cast<uint32_t>(cascadeIndex);

    vkCmdPushConstants(cmd, pipelines->getShadowCullPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Use indirect dispatch - the workgroup count is computed on GPU in terrain_dispatcher
    vkCmdDispatchIndirect(cmd, (*buffers)->getIndirectDispatchBuffer(), 0);

    // Memory barrier to ensure shadow cull results are visible for draw
    Barriers::computeToIndirectDraw(cmd);
}

void TerrainSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    // Choose pipeline: culled vs non-culled, meshlet vs direct
    VkPipeline pipeline;
    bool useCulled = shadowCullingEnabled && pipelines->getShadowCulledPipeline() != VK_NULL_HANDLE;

    if (config.useMeshlets) {
        pipeline = useCulled ? pipelines->getMeshletShadowCulledPipeline() : pipelines->getMeshletShadowPipeline();
    } else {
        pipeline = useCulled ? pipelines->getShadowCulledPipeline() : pipelines->getShadowPipeline();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->getShadowPipelineLayout(),
                           0, 1, &renderDescriptorSets[frameIndex], 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

    TerrainShadowPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = cascadeIndex;
    vkCmdPushConstants(cmd, pipelines->getShadowPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        VkBuffer vertexBuffers[] = {meshlet->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshlet->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // Use shadow indirect draw buffer if culling, else main indirect buffer
        VkBuffer drawBuffer = useCulled ? (*buffers)->getShadowIndirectDrawBuffer() : (*buffers)->getIndirectDrawBuffer();
        vkCmdDrawIndexedIndirect(cmd, drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        VkBuffer drawBuffer = useCulled ? (*buffers)->getShadowIndirectDrawBuffer() : (*buffers)->getIndirectDrawBuffer();
        vkCmdDrawIndirect(cmd, drawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
    }
}

float TerrainSystem::getHeightAt(float x, float z) const {
    // First try the tile cache for high-res height data
    if (tileCache) {
        float tileHeight;
        if (tileCache->getHeightAt(x, z, tileHeight)) {
            return tileHeight;
        }
    }

    // Fall back to global heightmap (coarse LOD)
    return heightMap->getHeightAt(x, z);
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
