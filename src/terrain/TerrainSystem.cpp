#include "TerrainSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include "GpuProfiler.h"
#include "UBOs.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>

using ShaderLoader::loadShaderModule;

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

    // Initialize height map
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
    if (!heightMap.init(heightMapInfo)) return false;

    // Initialize textures
    TerrainTextures::InitInfo texturesInfo{};
    texturesInfo.device = device;
    texturesInfo.allocator = allocator;
    texturesInfo.graphicsQueue = graphicsQueue;
    texturesInfo.commandPool = commandPool;
    texturesInfo.resourcePath = texturePath;
    if (!textures.init(texturesInfo)) return false;

    // Initialize CBT
    TerrainCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.initDepth = 6;  // Start with 64 triangles
    if (!cbt.init(cbtInfo)) return false;

    // Initialize meshlet for high-resolution rendering
    if (config.useMeshlets) {
        TerrainMeshlet::InitInfo meshletInfo{};
        meshletInfo.allocator = allocator;
        meshletInfo.subdivisionLevel = static_cast<uint32_t>(config.meshletSubdivisionLevel);
        if (!meshlet.init(meshletInfo)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet, falling back to direct triangles");
            config.useMeshlets = false;
        }
    }

    // Initialize tile cache for LOD-based height streaming (if configured)
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
        if (!tileCache.init(tileCacheInfo)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize tile cache, using global heightmap only");
        } else {
            SDL_Log("Tile cache initialized: %s", config.tileCacheDir.c_str());
        }
    }

    // Query GPU subgroup capabilities for optimized compute paths
    querySubgroupCapabilities();

    // Create remaining resources
    if (!createUniformBuffers()) return false;
    if (!createIndirectBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createDispatcherPipeline()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createSumReductionPipelines()) return false;
    if (!createFrustumCullPipelines()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;
    if (!createShadowPipeline()) return false;

    // Create meshlet pipelines if enabled
    if (config.useMeshlets) {
        if (!createMeshletRenderPipeline()) return false;
        if (!createMeshletWireframePipeline()) return false;
        if (!createMeshletShadowPipeline()) return false;
    }

    // Create shadow culling pipelines
    if (!createShadowCullPipelines()) return false;

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

    // Destroy pipelines
    if (dispatcherPipeline) vkDestroyPipeline(device, dispatcherPipeline, nullptr);
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (sumReductionPrepassPipeline) vkDestroyPipeline(device, sumReductionPrepassPipeline, nullptr);
    if (sumReductionPrepassSubgroupPipeline) vkDestroyPipeline(device, sumReductionPrepassSubgroupPipeline, nullptr);
    if (sumReductionPipeline) vkDestroyPipeline(device, sumReductionPipeline, nullptr);
    if (sumReductionBatchedPipeline) vkDestroyPipeline(device, sumReductionBatchedPipeline, nullptr);
    if (frustumCullPipeline) vkDestroyPipeline(device, frustumCullPipeline, nullptr);
    if (prepareDispatchPipeline) vkDestroyPipeline(device, prepareDispatchPipeline, nullptr);
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);
    if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);
    if (meshletRenderPipeline) vkDestroyPipeline(device, meshletRenderPipeline, nullptr);
    if (meshletWireframePipeline) vkDestroyPipeline(device, meshletWireframePipeline, nullptr);
    if (meshletShadowPipeline) vkDestroyPipeline(device, meshletShadowPipeline, nullptr);

    // Shadow culling pipelines
    if (shadowCullPipeline) vkDestroyPipeline(device, shadowCullPipeline, nullptr);
    if (shadowCulledPipeline) vkDestroyPipeline(device, shadowCulledPipeline, nullptr);
    if (meshletShadowCulledPipeline) vkDestroyPipeline(device, meshletShadowCulledPipeline, nullptr);

    // Destroy pipeline layouts
    if (dispatcherPipelineLayout) vkDestroyPipelineLayout(device, dispatcherPipelineLayout, nullptr);
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (sumReductionPipelineLayout) vkDestroyPipelineLayout(device, sumReductionPipelineLayout, nullptr);
    if (sumReductionBatchedPipelineLayout) vkDestroyPipelineLayout(device, sumReductionBatchedPipelineLayout, nullptr);
    if (frustumCullPipelineLayout) vkDestroyPipelineLayout(device, frustumCullPipelineLayout, nullptr);
    if (prepareDispatchPipelineLayout) vkDestroyPipelineLayout(device, prepareDispatchPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
    if (shadowCullPipelineLayout) vkDestroyPipelineLayout(device, shadowCullPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);

    // Destroy indirect buffers
    if (indirectDispatchBuffer) vmaDestroyBuffer(allocator, indirectDispatchBuffer, indirectDispatchAllocation);
    if (indirectDrawBuffer) vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
    if (visibleIndicesBuffer) vmaDestroyBuffer(allocator, visibleIndicesBuffer, visibleIndicesAllocation);
    if (cullIndirectDispatchBuffer) vmaDestroyBuffer(allocator, cullIndirectDispatchBuffer, cullIndirectDispatchAllocation);

    // Destroy shadow culling buffers
    if (shadowVisibleBuffer) vmaDestroyBuffer(allocator, shadowVisibleBuffer, shadowVisibleAllocation);
    if (shadowIndirectDrawBuffer) vmaDestroyBuffer(allocator, shadowIndirectDrawBuffer, shadowIndirectDrawAllocation);

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
    }

    // Destroy composed subsystems
    tileCache.destroy();
    meshlet.destroy(allocator);
    cbt.destroy(allocator);
    textures.destroy(device, allocator);
    heightMap.destroy(device, allocator);
}

bool TerrainSystem::createUniformBuffers() {
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(TerrainUniforms);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                           &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool TerrainSystem::createIndirectBuffers() {
    // Indirect dispatch buffer (3 uints: x, y, z)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 3;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDispatchBuffer,
                           &indirectDispatchAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // Indirect draw buffer (5 uints for indexed draw: indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 5;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDrawBuffer,
                           &indirectDrawAllocation, &allocationInfo) != VK_SUCCESS) {
            return false;
        }

        // Store persistently mapped pointer for readback
        indirectDrawMappedPtr = allocationInfo.pMappedData;

        // Initialize with default values (2 triangles = 6 vertices/indices)
        uint32_t drawArgs[5] = {6, 1, 0, 0, 0};
        memcpy(indirectDrawMappedPtr, drawArgs, sizeof(drawArgs));
    }

    // Visible indices buffer for stream compaction: [count, index0, index1, ...]
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        // Size: 1 uint for count + MAX_VISIBLE_TRIANGLES uints for indices
        bufferInfo.size = sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES);
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &visibleIndicesBuffer,
                           &visibleIndicesAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create visible indices buffer");
            return false;
        }
    }

    // Cull indirect dispatch buffer (3 uints: x, y, z for vkCmdDispatchIndirect)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 3;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &cullIndirectDispatchBuffer,
                           &cullIndirectDispatchAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cull indirect dispatch buffer");
            return false;
        }
    }

    // Shadow visible indices buffer: [count, index0, index1, ...]
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES);
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &shadowVisibleBuffer,
                           &shadowVisibleAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow visible indices buffer");
            return false;
        }
    }

    // Shadow indirect draw buffer (5 uints for indexed draw)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 5;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &shadowIndirectDrawBuffer,
                           &shadowIndirectDrawAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow indirect draw buffer");
            return false;
        }
    }

    return true;
}

uint32_t TerrainSystem::getTriangleCount() const {
    if (!indirectDrawMappedPtr) {
        return 0;
    }
    // Indirect draw buffer layout depends on rendering mode:
    // - Meshlet mode: {indexCount, instanceCount, ...} where total = instanceCount * meshletTriangles
    // - Direct mode: {vertexCount, instanceCount, ...} where total = vertexCount / 3
    const uint32_t* drawArgs = static_cast<const uint32_t*>(indirectDrawMappedPtr);
    if (config.useMeshlets) {
        uint32_t instanceCount = drawArgs[1];  // Number of CBT leaf nodes
        return instanceCount * meshlet.getTriangleCount();
    } else {
        return drawArgs[0] / 3;
    }
}

bool TerrainSystem::createComputeDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
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
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(stageFlags)
            .build();
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
            .writeBuffer(0, cbt.getBuffer(), 0, cbt.getBufferSize(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, indirectDispatchBuffer, 0, sizeof(uint32_t) * 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, indirectDrawBuffer, 0, sizeof(uint32_t) * 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(3, heightMap.getView(), heightMap.getSampler())
            .writeBuffer(4, uniformBuffers[i], 0, sizeof(TerrainUniforms))
            .writeBuffer(5, visibleIndicesBuffer, 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(6, cullIndirectDispatchBuffer, 0, sizeof(uint32_t) * 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(14, shadowVisibleBuffer, 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(15, shadowIndirectDrawBuffer, 0, sizeof(uint32_t) * 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    return true;
}

bool TerrainSystem::createDispatcherPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainDispatcherPushConstants);

    dispatcherPipelineLayout = DescriptorManager::createPipelineLayout(device, computeDescriptorSetLayout, {pushConstantRange});
    if (dispatcherPipelineLayout == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = dispatcherPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dispatcherPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createSubdivisionPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainSubdivisionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &subdivisionPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = subdivisionPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &subdivisionPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createSumReductionPipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainSumReductionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sumReductionPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Prepass pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPrepassPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Subgroup-optimized prepass pipeline (processes 13 levels instead of 5)
    if (subgroupCaps.hasSubgroupArithmetic) {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv");
        if (shaderModule) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModule;
            stageInfo.pName = "main";

            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = sumReductionPipelineLayout;

            VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPrepassSubgroupPipeline);
            vkDestroyShaderModule(device, shaderModule, nullptr);
            if (result != VK_SUCCESS) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subgroup prepass pipeline, using fallback");
            } else {
                SDL_Log("TerrainSystem: Using subgroup-optimized sum reduction prepass");
            }
        }
    }

    // Regular sum reduction pipeline (legacy single-level per dispatch)
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Batched sum reduction pipeline (multi-level per dispatch using shared memory)
    {
        // Create pipeline layout for batched push constants
        VkPushConstantRange batchedPushConstantRange{};
        batchedPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        batchedPushConstantRange.offset = 0;
        batchedPushConstantRange.size = sizeof(TerrainSumReductionBatchedPushConstants);

        VkPipelineLayoutCreateInfo batchedLayoutInfo{};
        batchedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        batchedLayoutInfo.setLayoutCount = 1;
        batchedLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        batchedLayoutInfo.pushConstantRangeCount = 1;
        batchedLayoutInfo.pPushConstantRanges = &batchedPushConstantRange;

        if (vkCreatePipelineLayout(device, &batchedLayoutInfo, nullptr, &sumReductionBatchedPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_batched.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionBatchedPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionBatchedPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    return true;
}

bool TerrainSystem::createFrustumCullPipelines() {
    // Frustum cull pipeline (with push constants for dispatch calculation)
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_frustum_cull.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainFrustumCullPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &frustumCullPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = frustumCullPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &frustumCullPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Prepare cull dispatch pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_prepare_cull_dispatch.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainPrepareCullDispatchPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &prepareDispatchPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = prepareDispatchPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &prepareDispatchPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    return true;
}

bool TerrainSystem::createRenderPipeline() {
    // Create render pipeline layout (shared by render and wireframe pipelines)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create filled render pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::filled(renderPass), renderPipelineLayout, renderPipeline);
}

bool TerrainSystem::createWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::wireframe(renderPass), renderPipelineLayout, wireframePipeline);
}

bool TerrainSystem::createShadowPipeline() {
    // Create shadow pipeline layout with push constants
    PipelineBuilder layoutBuilder(device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainShadowPushConstants));

    if (!layoutBuilder.buildPipelineLayout({renderDescriptorSetLayout}, shadowPipelineLayout)) {
        return false;
    }

    // Create shadow pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::shadow(shadowRenderPass), shadowPipelineLayout, shadowPipeline);
}

bool TerrainSystem::createMeshletRenderPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::filled(renderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, renderPipelineLayout, meshletRenderPipeline);
}

bool TerrainSystem::createMeshletWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::wireframe(renderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, renderPipelineLayout, meshletWireframePipeline);
}

bool TerrainSystem::createMeshletShadowPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::shadow(shadowRenderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, shadowPipelineLayout, meshletShadowPipeline);
}

bool TerrainSystem::createShadowCullPipelines() {
    // Create shadow cull compute pipeline
    VkShaderModule cullShaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_cull.comp.spv");
    if (!cullShaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow cull compute shader");
        return false;
    }

    // Pipeline layout for shadow cull compute
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainShadowCullPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowCullPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, cullShaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull pipeline layout");
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = cullShaderModule;
    stageInfo.pName = "main";

    // Specialization constant for meshlet index count
    uint32_t meshletIndexCount = config.useMeshlets ? meshlet.getIndexCount() : 0;
    VkSpecializationMapEntry specEntry{};
    specEntry.constantID = 0;
    specEntry.offset = 0;
    specEntry.size = sizeof(uint32_t);

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 1;
    specInfo.pMapEntries = &specEntry;
    specInfo.dataSize = sizeof(uint32_t);
    specInfo.pData = &meshletIndexCount;
    stageInfo.pSpecializationInfo = &specInfo;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = shadowCullPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowCullPipeline);
    vkDestroyShaderModule(device, cullShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull compute pipeline");
        return false;
    }

    // Create shadow culled graphics pipeline (non-meshlet)
    VkShaderModule shadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_culled.vert.spv");
    VkShaderModule shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!shadowCulledVertModule || !shadowFragModule) {
        if (shadowCulledVertModule) vkDestroyShaderModule(device, shadowCulledVertModule, nullptr);
        if (shadowFragModule) vkDestroyShaderModule(device, shadowFragModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow culled shaders");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = shadowCulledVertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = shadowFragModule;
    shaderStages[1].pName = "main";

    // No vertex input for non-meshlet (generated in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo gfxPipelineInfo{};
    gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gfxPipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    gfxPipelineInfo.pStages = shaderStages.data();
    gfxPipelineInfo.pVertexInputState = &vertexInputInfo;
    gfxPipelineInfo.pInputAssemblyState = &inputAssembly;
    gfxPipelineInfo.pViewportState = &viewportState;
    gfxPipelineInfo.pRasterizationState = &rasterizer;
    gfxPipelineInfo.pMultisampleState = &multisampling;
    gfxPipelineInfo.pDepthStencilState = &depthStencil;
    gfxPipelineInfo.pColorBlendState = &colorBlending;
    gfxPipelineInfo.pDynamicState = &dynamicState;
    gfxPipelineInfo.layout = shadowPipelineLayout;
    gfxPipelineInfo.renderPass = shadowRenderPass;
    gfxPipelineInfo.subpass = 0;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &shadowCulledPipeline);
    vkDestroyShaderModule(device, shadowCulledVertModule, nullptr);
    vkDestroyShaderModule(device, shadowFragModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow culled graphics pipeline");
        return false;
    }

    // Create meshlet shadow culled pipeline (if meshlets enabled)
    if (config.useMeshlets) {
        VkShaderModule meshletShadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_meshlet_shadow_culled.vert.spv");
        shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
        if (!meshletShadowCulledVertModule || !shadowFragModule) {
            if (meshletShadowCulledVertModule) vkDestroyShaderModule(device, meshletShadowCulledVertModule, nullptr);
            if (shadowFragModule) vkDestroyShaderModule(device, shadowFragModule, nullptr);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load meshlet shadow culled shaders");
            return false;
        }

        shaderStages[0].module = meshletShadowCulledVertModule;
        shaderStages[1].module = shadowFragModule;

        // Meshlet vertex input: vec2 for local UV
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(glm::vec2);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrDesc{};
        attrDesc.binding = 0;
        attrDesc.location = 0;
        attrDesc.format = VK_FORMAT_R32G32_SFLOAT;
        attrDesc.offset = 0;

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = 1;
        vertexInputInfo.pVertexAttributeDescriptions = &attrDesc;

        gfxPipelineInfo.pStages = shaderStages.data();

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &meshletShadowCulledPipeline);
        vkDestroyShaderModule(device, meshletShadowCulledVertModule, nullptr);
        vkDestroyShaderModule(device, shadowFragModule, nullptr);

        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet shadow culled graphics pipeline");
            return false;
        }
    }

    SDL_Log("TerrainSystem: Shadow culling pipelines created successfully");
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
        VkDescriptorBufferInfo cbtInfo{cbt.getBuffer(), 0, cbt.getBufferSize()};
        VkWriteDescriptorSet cbtWrite{};
        cbtWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cbtWrite.dstSet = renderDescriptorSets[i];
        cbtWrite.dstBinding = 0;
        cbtWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cbtWrite.descriptorCount = 1;
        cbtWrite.pBufferInfo = &cbtInfo;
        writes.push_back(cbtWrite);

        // Height map
        VkDescriptorImageInfo heightMapInfo{heightMap.getSampler(), heightMap.getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet heightMapWrite{};
        heightMapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        heightMapWrite.dstSet = renderDescriptorSets[i];
        heightMapWrite.dstBinding = 3;
        heightMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        heightMapWrite.descriptorCount = 1;
        heightMapWrite.pImageInfo = &heightMapInfo;
        writes.push_back(heightMapWrite);

        // Terrain uniforms
        VkDescriptorBufferInfo uniformInfo{uniformBuffers[i], 0, sizeof(TerrainUniforms)};
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
        VkDescriptorImageInfo albedoInfo{textures.getAlbedoSampler(), textures.getAlbedoView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
        if (textures.getGrassFarLODView() != VK_NULL_HANDLE) {
            VkDescriptorImageInfo grassFarLODInfo{textures.getGrassFarLODSampler(), textures.getGrassFarLODView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
        if (shadowVisibleBuffer != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo shadowVisibleInfo{shadowVisibleBuffer, 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES)};
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
        VkDescriptorImageInfo holeMaskInfo{heightMap.getHoleMaskSampler(), heightMap.getHoleMaskView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
        if (tileCache.getTileArrayView() != VK_NULL_HANDLE) {
            VkDescriptorImageInfo tileArrayInfo{tileCache.getSampler(), tileCache.getTileArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
        if (tileCache.getTileInfoBuffer() != VK_NULL_HANDLE) {
            VkDescriptorBufferInfo tileInfoBufInfo{tileCache.getTileInfoBuffer(), 0, VK_WHOLE_SIZE};
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

bool TerrainSystem::cameraHasMoved(const glm::vec3& cameraPos, const glm::mat4& view) {
    // Extract forward direction from view matrix (negated row 2)
    glm::vec3 forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

    // First frame - always consider moved
    if (!previousCamera.valid) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        previousCamera.valid = true;
        return true;
    }

    // Check position delta
    float positionDelta = glm::length(cameraPos - previousCamera.position);
    if (positionDelta > POSITION_THRESHOLD) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        return true;
    }

    // Check rotation delta (using dot product of forward vectors)
    float forwardDot = glm::dot(forward, previousCamera.forward);
    if (forwardDot < (1.0f - ROTATION_THRESHOLD)) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        return true;
    }

    // No significant change
    return false;
}

void TerrainSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                    const glm::mat4& view, const glm::mat4& proj,
                                    const std::array<glm::vec4, 3>& snowCascadeParams,
                                    bool useVolumetricSnow,
                                    float snowMaxHeight) {
    // Track camera movement for skip-frame optimization
    if (cameraHasMoved(cameraPos, view)) {
        staticFrameCount = 0;
    } else {
        staticFrameCount++;
    }

    // Update tile cache - stream high-res tiles based on camera position
    if (!config.tileCacheDir.empty()) {
        tileCache.updateActiveTiles(cameraPos, config.tileLoadRadius, config.tileUnloadRadius);
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

    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(TerrainUniforms));
}

void TerrainSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler) {
    // Skip-frame optimization: skip compute when camera is stationary and terrain has converged
    bool shouldSkip = false;
    if (skipFrameOptimizationEnabled && !forceNextCompute && staticFrameCount > CONVERGENCE_FRAMES) {
        if (framesSinceLastCompute < MAX_SKIP_FRAMES) {
            shouldSkip = true;
        }
    }

    if (shouldSkip) {
        framesSinceLastCompute++;
        lastFrameWasSkipped = true;

        // Still need the final barrier for rendering (CBT state unchanged but GPU needs it)
        Barriers::computeToIndirectDraw(cmd);
        return;
    }

    // Reset skip tracking
    forceNextCompute = false;
    framesSinceLastCompute = 0;
    lastFrameWasSkipped = false;

    // 1. Dispatcher - set up indirect args
    if (profiler) profiler->beginZone(cmd, "Terrain:Dispatcher");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletIndexCount = config.useMeshlets ? meshlet.getIndexCount() : 0;
    vkCmdPushConstants(cmd, dispatcherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 0;  // Split
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Dispatch all triangles - inline frustum culling handles early-out
        vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

        if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
    } else {
        // Merge phase: process all triangles directly (no culling)
        if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 1;  // Merge
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = config.spreadFactor;
        subdivPC.reserved = 0;
        vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Use the original indirect dispatch (all triangles)
        vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

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

    if (sumReductionPrepassSubgroupPipeline) {
        // Subgroup prepass - processes 13 levels:
        // - SWAR popcount: 5 levels (32 bits -> 6-bit sum)
        // - Subgroup shuffle: 5 levels (32 threads -> 11-bit sum)
        // - Shared memory: 3 levels (8 subgroups -> 14-bit sum)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPrepassSubgroupPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, sumReductionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(sumPC), &sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmdDispatch(cmd, workgroups, 1, 1);

        Barriers::computeToComputeReadWrite(cmd);

        levelsFromPrepass = 13;  // SWAR (5) + subgroup (5) + shared memory (3)
    } else {
        // Fallback path: standard prepass handles 5 levels
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPrepassPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        vkCmdPushConstants(cmd, sumReductionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        for (int depth = startDepth; depth >= 0; --depth) {
            sumPC.passID = depth;
            vkCmdPushConstants(cmd, sumReductionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(sumPC), &sumPC);

            uint32_t workgroups = std::max(1u, (1u << depth) / SUM_REDUCTION_WORKGROUP_SIZE);
            vkCmdDispatch(cmd, workgroups, 1, 1);

            Barriers::computeToComputeReadWrite(cmd);
        }

        if (profiler) profiler->endZone(cmd, "Terrain:SumReductionLevels");
    }

    // 4. Final dispatcher pass to update draw args
    if (profiler) profiler->beginZone(cmd, "Terrain:FinalDispatch");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdPushConstants(cmd, dispatcherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);
    vkCmdDispatch(cmd, 1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:FinalDispatch");

    // Final barrier before rendering
    Barriers::computeToIndirectDraw(cmd);
}

void TerrainSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    VkPipeline pipeline;
    if (config.useMeshlets) {
        pipeline = wireframeMode ? meshletWireframePipeline : meshletRenderPipeline;
    } else {
        pipeline = wireframeMode ? wireframePipeline : renderPipeline;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout,
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
        VkBuffer vertexBuffers[] = {meshlet.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshlet.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // Indexed instanced draw
        vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        // Direct vertex draw (no vertex buffer - vertices generated from gl_VertexIndex)
        vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
    }
}

void TerrainSystem::recordShadowCull(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!shadowCullingEnabled || shadowCullPipeline == VK_NULL_HANDLE) {
        return;
    }

    // Clear the shadow visible count to 0 and barrier for compute
    Barriers::clearBufferForCompute(cmd, shadowVisibleBuffer);

    // Bind shadow cull compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadowCullPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadowCullPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    // Set up push constants with frustum planes
    TerrainShadowCullPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    extractFrustumPlanes(lightViewProj, pc.lightFrustumPlanes);
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = static_cast<uint32_t>(cascadeIndex);

    vkCmdPushConstants(cmd, shadowCullPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Use indirect dispatch - the workgroup count is computed on GPU in terrain_dispatcher
    vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

    // Memory barrier to ensure shadow cull results are visible for draw
    Barriers::computeToIndirectDraw(cmd);
}

void TerrainSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    // Choose pipeline: culled vs non-culled, meshlet vs direct
    VkPipeline pipeline;
    bool useCulled = shadowCullingEnabled && shadowCulledPipeline != VK_NULL_HANDLE;

    if (config.useMeshlets) {
        pipeline = useCulled ? meshletShadowCulledPipeline : meshletShadowPipeline;
    } else {
        pipeline = useCulled ? shadowCulledPipeline : shadowPipeline;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout,
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
    vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        VkBuffer vertexBuffers[] = {meshlet.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshlet.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        // Use shadow indirect draw buffer if culling, else main indirect buffer
        VkBuffer drawBuffer = useCulled ? shadowIndirectDrawBuffer : indirectDrawBuffer;
        vkCmdDrawIndexedIndirect(cmd, drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    } else {
        VkBuffer drawBuffer = useCulled ? shadowIndirectDrawBuffer : indirectDrawBuffer;
        vkCmdDrawIndirect(cmd, drawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
    }
}

float TerrainSystem::getHeightAt(float x, float z) const {
    // First try the tile cache for high-res height data
    if (!config.tileCacheDir.empty()) {
        float tileHeight;
        if (tileCache.getHeightAt(x, z, tileHeight)) {
            return tileHeight;
        }
    }

    // Fall back to global heightmap (coarse LOD)
    return heightMap.getHeightAt(x, z);
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

    // Destroy old meshlet and create new one
    vkDeviceWaitIdle(device);
    meshlet.destroy(allocator);

    TerrainMeshlet::InitInfo meshletInfo{};
    meshletInfo.allocator = allocator;
    meshletInfo.subdivisionLevel = static_cast<uint32_t>(level);

    if (!meshlet.init(meshletInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to reinitialize meshlet at level %d", level);
        // Try to restore previous level
        meshletInfo.subdivisionLevel = static_cast<uint32_t>(config.meshletSubdivisionLevel);
        meshlet.init(meshletInfo);
        return false;
    }

    config.meshletSubdivisionLevel = level;
    SDL_Log("Meshlet subdivision level changed to %d (%u triangles per leaf)",
            level, meshlet.getTriangleCount());
    return true;
}
