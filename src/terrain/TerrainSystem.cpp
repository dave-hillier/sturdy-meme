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

bool TerrainSystem::init(const InitInfo& info, const TerrainConfig& cfg) {
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
    heightMap = RAIIAdapter<TerrainHeightMap>::create(
        [&](auto& h) { return h.init(heightMapInfo); },
        [this](auto& h) { h.destroy(device, allocator); }
    );
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
        [this](auto& c) { c.destroy(allocator); }
    );
    if (!cbt) return false;

    // Initialize meshlet for high-resolution rendering with RAII wrapper
    if (config.useMeshlets) {
        TerrainMeshlet::InitInfo meshletInfo{};
        meshletInfo.allocator = allocator;
        meshletInfo.subdivisionLevel = static_cast<uint32_t>(config.meshletSubdivisionLevel);
        meshlet = RAIIAdapter<TerrainMeshlet>::create(
            [&](auto& m) { return m.init(meshletInfo); },
            [this](auto& m) { m.destroy(allocator); }
        );
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
        tileCache = RAIIAdapter<TerrainTileCache>::create(
            [&](auto& tc) { return tc.init(tileCacheInfo); },
            [](auto& tc) { tc.destroy(); }
        );
        if (!tileCache) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize tile cache, using global heightmap only");
        } else {
            SDL_Log("Tile cache initialized: %s", config.tileCacheDir.c_str());
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
    pipelineInfo.meshletIndexCount = config.useMeshlets && meshlet ? (*meshlet)->getIndexCount() : 0;
    pipelineInfo.subgroupCaps = &subgroupCaps;
    pipelines = RAIIAdapter<TerrainPipelines>::create(
        [&](auto& p) { return p.init(pipelineInfo); },
        [this](auto& p) { p.destroy(device); }
    );
    if (!pipelines) return false;

    SDL_Log("TerrainSystem initialized with CBT max depth %d, meshlets %s, shadow culling %s",
            config.maxDepth, config.useMeshlets ? "enabled" : "disabled",
            shadowCullingEnabled ? "enabled" : "disabled");
    return true;
}

bool TerrainSystem::init(const InitContext& ctx, const TerrainInitParams& params, const TerrainConfig& cfg) {
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
    return init(info, cfg);
}

void TerrainSystem::destroy(VkDevice device, VmaAllocator allocator) {
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
        return instanceCount * (*meshlet)->getTriangleCount();
    } else {
        return drawArgs[0] / 3;
    }
}

bool TerrainSystem::createComputeDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    std::array<VkDescriptorSetLayoutBinding, 9> bindings = {
        makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // CBT buffer
        makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // indirect dispatch
        makeComputeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // indirect draw
        makeComputeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER), // height map
        makeComputeBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),   // terrain uniforms
        makeComputeBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // visible indices (stream compaction)
        makeComputeBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // cull indirect dispatch
        makeComputeBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // shadow visible indices
        makeComputeBinding(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)}; // shadow indirect draw

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TerrainSystem::createRenderDescriptorSetLayout() {
    auto makeGraphicsBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = stageFlags;
        return b;
    };

    std::array<VkDescriptorSetLayoutBinding, 18> bindings = {
        makeGraphicsBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
        makeGraphicsBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // Volumetric snow cascade textures
        makeGraphicsBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // Cloud shadow map
        makeGraphicsBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // Shadow culled visible indices (for shadow culled vertex shaders)
        makeGraphicsBinding(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
        // Hole mask for caves/wells
        makeGraphicsBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // Snow UBO (binding 17) - separate from snow cascade textures
        makeGraphicsBinding(17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // Cloud shadow UBO (binding 18) - separate from cloud shadow texture
        makeGraphicsBinding(18, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        // LOD tile streaming: tile array texture (binding 19)
        makeGraphicsBinding(19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT),
        // LOD tile streaming: tile info SSBO (binding 20)
        makeGraphicsBinding(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &renderDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    return true;
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
        DescriptorManager::SetWriter(device, computeDescriptorSets[i])
            .writeBuffer(0, (*cbt)->getBuffer(), 0, (*cbt)->getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, (*buffers)->getIndirectDispatchBuffer(), 0, sizeof(uint32_t) * 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, (*buffers)->getIndirectDrawBuffer(), 0, sizeof(uint32_t) * 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(3, (*heightMap)->getView(), (*heightMap)->getSampler())
            .writeBuffer(4, (*buffers)->getUniformBuffer(i), 0, sizeof(TerrainUniforms))
            .writeBuffer(5, (*buffers)->getVisibleIndicesBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(6, (*buffers)->getCullIndirectDispatchBuffer(), 0, sizeof(uint32_t) * 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(14, (*buffers)->getShadowVisibleBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(15, (*buffers)->getShadowIndirectDrawBuffer(), 0, sizeof(uint32_t) * 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
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
        std::vector<VkWriteDescriptorSet> writes;

        // CBT buffer
        VkDescriptorBufferInfo cbtInfo{(*cbt)->getBuffer(), 0, (*cbt)->getBufferSize()};
        VkWriteDescriptorSet cbtWrite{};
        cbtWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cbtWrite.dstSet = renderDescriptorSets[i];
        cbtWrite.dstBinding = 0;
        cbtWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cbtWrite.descriptorCount = 1;
        cbtWrite.pBufferInfo = &cbtInfo;
        writes.push_back(cbtWrite);

        // Height map
        VkDescriptorImageInfo heightMapInfo{(*heightMap)->getSampler(), (*heightMap)->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet heightMapWrite{};
        heightMapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        heightMapWrite.dstSet = renderDescriptorSets[i];
        heightMapWrite.dstBinding = 3;
        heightMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        heightMapWrite.descriptorCount = 1;
        heightMapWrite.pImageInfo = &heightMapInfo;
        writes.push_back(heightMapWrite);

        // Terrain uniforms
        VkDescriptorBufferInfo uniformInfo{(*buffers)->getUniformBuffer(i), 0, sizeof(TerrainUniforms)};
        VkWriteDescriptorSet uniformWrite{};
        uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWrite.dstSet = renderDescriptorSets[i];
        uniformWrite.dstBinding = 4;
        uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.descriptorCount = 1;
        uniformWrite.pBufferInfo = &uniformInfo;
        writes.push_back(uniformWrite);

        // Scene UBO
        if (i < sceneUniformBuffers.size()) {
            VkDescriptorBufferInfo sceneInfo{sceneUniformBuffers[i], 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet sceneWrite{};
            sceneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            sceneWrite.dstSet = renderDescriptorSets[i];
            sceneWrite.dstBinding = 5;
            sceneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneWrite.descriptorCount = 1;
            sceneWrite.pBufferInfo = &sceneInfo;
            writes.push_back(sceneWrite);
        }

        // Terrain albedo
        VkDescriptorImageInfo albedoInfo{(*textures)->getAlbedoSampler(), (*textures)->getAlbedoView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet albedoWrite{};
        albedoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        albedoWrite.dstSet = renderDescriptorSets[i];
        albedoWrite.dstBinding = 6;
        albedoWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        albedoWrite.descriptorCount = 1;
        albedoWrite.pImageInfo = &albedoInfo;
        writes.push_back(albedoWrite);

        // Shadow map
        if (shadowMapView != VK_NULL_HANDLE) {
            VkDescriptorImageInfo shadowInfo{shadowSampler, shadowMapView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet shadowWrite{};
            shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowWrite.dstSet = renderDescriptorSets[i];
            shadowWrite.dstBinding = 7;
            shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            shadowWrite.descriptorCount = 1;
            shadowWrite.pImageInfo = &shadowInfo;
            writes.push_back(shadowWrite);
        }

        // Grass far LOD texture
        if ((*textures)->getGrassFarLODView() != VK_NULL_HANDLE) {
            VkDescriptorImageInfo grassFarLODInfo{(*textures)->getGrassFarLODSampler(), (*textures)->getGrassFarLODView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet grassFarLODWrite{};
            grassFarLODWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            grassFarLODWrite.dstSet = renderDescriptorSets[i];
            grassFarLODWrite.dstBinding = 8;
            grassFarLODWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            grassFarLODWrite.descriptorCount = 1;
            grassFarLODWrite.pImageInfo = &grassFarLODInfo;
            writes.push_back(grassFarLODWrite);
        }

        // Shadow visible indices (for shadow culled vertex shaders)
        if ((*buffers)->getShadowVisibleBuffer() != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo shadowVisibleInfo{(*buffers)->getShadowVisibleBuffer(), 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES)};
            VkWriteDescriptorSet shadowVisibleWrite{};
            shadowVisibleWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowVisibleWrite.dstSet = renderDescriptorSets[i];
            shadowVisibleWrite.dstBinding = 14;
            shadowVisibleWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            shadowVisibleWrite.descriptorCount = 1;
            shadowVisibleWrite.pBufferInfo = &shadowVisibleInfo;
            writes.push_back(shadowVisibleWrite);
        }

        // Hole mask (for cave/well rendering)
        VkDescriptorImageInfo holeMaskInfo{(*heightMap)->getHoleMaskSampler(), (*heightMap)->getHoleMaskView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet holeMaskWrite{};
        holeMaskWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        holeMaskWrite.dstSet = renderDescriptorSets[i];
        holeMaskWrite.dstBinding = 16;  // BINDING_TERRAIN_HOLE_MASK
        holeMaskWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        holeMaskWrite.descriptorCount = 1;
        holeMaskWrite.pImageInfo = &holeMaskInfo;
        writes.push_back(holeMaskWrite);

        // Snow UBO (binding 17)
        if (i < snowUBOBuffers.size() && snowUBOBuffers[i] != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo snowUBOInfo{snowUBOBuffers[i], 0, sizeof(SnowUBO)};
            VkWriteDescriptorSet snowUBOWrite{};
            snowUBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            snowUBOWrite.dstSet = renderDescriptorSets[i];
            snowUBOWrite.dstBinding = 17;  // BINDING_TERRAIN_SNOW_UBO
            snowUBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            snowUBOWrite.descriptorCount = 1;
            snowUBOWrite.pBufferInfo = &snowUBOInfo;
            writes.push_back(snowUBOWrite);
        }

        // Cloud shadow UBO (binding 18)
        if (i < cloudShadowUBOBuffers.size() && cloudShadowUBOBuffers[i] != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo cloudShadowUBOInfo{cloudShadowUBOBuffers[i], 0, sizeof(CloudShadowUBO)};
            VkWriteDescriptorSet cloudShadowUBOWrite{};
            cloudShadowUBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cloudShadowUBOWrite.dstSet = renderDescriptorSets[i];
            cloudShadowUBOWrite.dstBinding = 18;  // BINDING_TERRAIN_CLOUD_SHADOW_UBO
            cloudShadowUBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cloudShadowUBOWrite.descriptorCount = 1;
            cloudShadowUBOWrite.pBufferInfo = &cloudShadowUBOInfo;
            writes.push_back(cloudShadowUBOWrite);
        }

        // LOD tile array texture (binding 19)
        if (tileCache && (*tileCache)->getTileArrayView() != VK_NULL_HANDLE) {
            VkDescriptorImageInfo tileArrayInfo{(*tileCache)->getSampler(), (*tileCache)->getTileArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet tileArrayWrite{};
            tileArrayWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            tileArrayWrite.dstSet = renderDescriptorSets[i];
            tileArrayWrite.dstBinding = 19;  // BINDING_TERRAIN_TILE_ARRAY
            tileArrayWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tileArrayWrite.descriptorCount = 1;
            tileArrayWrite.pImageInfo = &tileArrayInfo;
            writes.push_back(tileArrayWrite);
        }

        // LOD tile info buffer (binding 20)
        if (tileCache && (*tileCache)->getTileInfoBuffer() != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo tileInfoBufInfo{(*tileCache)->getTileInfoBuffer(), 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet tileInfoWrite{};
            tileInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            tileInfoWrite.dstSet = renderDescriptorSets[i];
            tileInfoWrite.dstBinding = 20;  // BINDING_TERRAIN_TILE_INFO
            tileInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            tileInfoWrite.descriptorCount = 1;
            tileInfoWrite.pBufferInfo = &tileInfoBufInfo;
            writes.push_back(tileInfoWrite);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void TerrainSystem::setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo snowMaskInfo{snowMaskSampler, snowMaskView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet snowMaskWrite{};
        snowMaskWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        snowMaskWrite.dstSet = renderDescriptorSets[i];
        snowMaskWrite.dstBinding = 9;
        snowMaskWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        snowMaskWrite.descriptorCount = 1;
        snowMaskWrite.pImageInfo = &snowMaskInfo;

        vkUpdateDescriptorSets(device, 1, &snowMaskWrite, 0, nullptr);
    }
}

void TerrainSystem::setVolumetricSnowCascades(VkDevice device,
                                               VkImageView cascade0View, VkImageView cascade1View, VkImageView cascade2View,
                                               VkSampler cascadeSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo cascade0Info{cascadeSampler, cascade0View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo cascade1Info{cascadeSampler, cascade1View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo cascade2Info{cascadeSampler, cascade2View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 3> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = renderDescriptorSets[i];
        writes[0].dstBinding = 10;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &cascade0Info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = renderDescriptorSets[i];
        writes[1].dstBinding = 11;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &cascade1Info;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = renderDescriptorSets[i];
        writes[2].dstBinding = 12;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &cascade2Info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void TerrainSystem::setCloudShadowMap(VkDevice device, VkImageView cloudShadowView, VkSampler cloudShadowSampler) {
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo cloudShadowInfo{cloudShadowSampler, cloudShadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = renderDescriptorSets[i];
        write.dstBinding = 13;  // Binding 13 for cloud shadow map
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &cloudShadowInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
    if (tileCache) {
        (*tileCache)->updateActiveTiles(cameraPos, config.tileLoadRadius, config.tileUnloadRadius);
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getDispatcherPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getDispatcherPipelineLayout(),
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletIndexCount = config.useMeshlets ? (*meshlet)->getIndexCount() : 0;
    vkCmdPushConstants(cmd, (*pipelines)->getDispatcherPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSubdivisionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSubdivisionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 0;  // Split
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, (*pipelines)->getSubdivisionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Dispatch all triangles - inline frustum culling handles early-out
        vkCmdDispatchIndirect(cmd, (*buffers)->getIndirectDispatchBuffer(), 0);

        if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
    } else {
        // Merge phase: process all triangles directly (no culling)
        if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSubdivisionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSubdivisionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 1;  // Merge
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, (*pipelines)->getSubdivisionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
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

    if ((*pipelines)->getSumReductionPrepassSubgroupPipeline()) {
        // Subgroup prepass - processes 13 levels:
        // - SWAR popcount: 5 levels (32 bits -> 6-bit sum)
        // - Subgroup shuffle: 5 levels (32 threads -> 11-bit sum)
        // - Shared memory: 3 levels (8 subgroups -> 14-bit sum)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPrepassSubgroupPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, (*pipelines)->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(sumPC), &sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmdDispatch(cmd, workgroups, 1, 1);

        Barriers::computeToComputeReadWrite(cmd);

        levelsFromPrepass = 13;  // SWAR (5) + subgroup (5) + shared memory (3)
    } else {
        // Fallback path: standard prepass handles 5 levels
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPrepassPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, (*pipelines)->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getSumReductionPipelineLayout(),
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        for (int depth = startDepth; depth >= 0; --depth) {
            sumPC.passID = depth;
            vkCmdPushConstants(cmd, (*pipelines)->getSumReductionPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(sumPC), &sumPC);

            uint32_t workgroups = std::max(1u, (1u << depth) / SUM_REDUCTION_WORKGROUP_SIZE);
            vkCmdDispatch(cmd, workgroups, 1, 1);

            Barriers::computeToComputeReadWrite(cmd);
        }

        if (profiler) profiler->endZone(cmd, "Terrain:SumReductionLevels");
    }

    // 4. Final dispatcher pass to update draw args
    if (profiler) profiler->beginZone(cmd, "Terrain:FinalDispatch");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getDispatcherPipeline());
    vkCmdPushConstants(cmd, (*pipelines)->getDispatcherPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);
    vkCmdDispatch(cmd, 1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:FinalDispatch");

    // Final barrier before rendering
    Barriers::computeToIndirectDraw(cmd);
}

void TerrainSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    VkPipeline pipeline;
    if (config.useMeshlets) {
        pipeline = wireframeMode ? (*pipelines)->getMeshletWireframePipeline() : (*pipelines)->getMeshletRenderPipeline();
    } else {
        pipeline = wireframeMode ? (*pipelines)->getWireframePipeline() : (*pipelines)->getRenderPipeline();
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, (*pipelines)->getRenderPipelineLayout(),
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
        VkBuffer vertexBuffers[] = {(*meshlet)->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, (*meshlet)->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // Indexed instanced draw
        vkCmdDrawIndexedIndirect(cmd, (*buffers)->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        // Direct vertex draw (no vertex buffer - vertices generated from gl_VertexIndex)
        vkCmdDrawIndirect(cmd, (*buffers)->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndirectCommand));
    }
}

void TerrainSystem::recordShadowCull(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!shadowCullingEnabled || !(*pipelines)->hasShadowCulling()) {
        return;
    }

    // Clear the shadow visible count to 0 and barrier for compute
    Barriers::clearBufferForCompute(cmd, (*buffers)->getShadowVisibleBuffer());

    // Bind shadow cull compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getShadowCullPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (*pipelines)->getShadowCullPipelineLayout(),
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    // Set up push constants with frustum planes
    TerrainShadowCullPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    extractFrustumPlanes(lightViewProj, pc.lightFrustumPlanes);
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = static_cast<uint32_t>(cascadeIndex);

    vkCmdPushConstants(cmd, (*pipelines)->getShadowCullPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
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
    bool useCulled = shadowCullingEnabled && (*pipelines)->getShadowCulledPipeline() != VK_NULL_HANDLE;

    if (config.useMeshlets) {
        pipeline = useCulled ? (*pipelines)->getMeshletShadowCulledPipeline() : (*pipelines)->getMeshletShadowPipeline();
    } else {
        pipeline = useCulled ? (*pipelines)->getShadowCulledPipeline() : (*pipelines)->getShadowPipeline();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, (*pipelines)->getShadowPipelineLayout(),
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
    vkCmdPushConstants(cmd, (*pipelines)->getShadowPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        VkBuffer vertexBuffers[] = {(*meshlet)->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, (*meshlet)->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

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
        if ((*tileCache)->getHeightAt(x, z, tileHeight)) {
            return tileHeight;
        }
    }

    // Fall back to global heightmap (coarse LOD)
    return (*heightMap)->getHeightAt(x, z);
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

    // Destroy old meshlet and create new one with RAII
    vkDeviceWaitIdle(device);
    meshlet.reset();

    TerrainMeshlet::InitInfo meshletInfo{};
    meshletInfo.allocator = allocator;
    meshletInfo.subdivisionLevel = static_cast<uint32_t>(level);

    meshlet = RAIIAdapter<TerrainMeshlet>::create(
        [&](auto& m) { return m.init(meshletInfo); },
        [this](auto& m) { m.destroy(allocator); }
    );
    if (!meshlet) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to reinitialize meshlet at level %d", level);
        // Try to restore previous level
        meshletInfo.subdivisionLevel = static_cast<uint32_t>(config.meshletSubdivisionLevel);
        meshlet = RAIIAdapter<TerrainMeshlet>::create(
            [&](auto& m) { return m.init(meshletInfo); },
            [this](auto& m) { m.destroy(allocator); }
        );
        return false;
    }

    config.meshletSubdivisionLevel = level;
    SDL_Log("Meshlet subdivision level changed to %d (%u triangles per leaf)",
            level, (*meshlet)->getTriangleCount());
    return true;
}
