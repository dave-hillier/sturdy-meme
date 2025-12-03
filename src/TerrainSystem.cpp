#include "TerrainSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include "GpuProfiler.h"
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

    SDL_Log("TerrainSystem initialized with CBT max depth %d", config.maxDepth);
    return true;
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

    // Destroy pipeline layouts
    if (dispatcherPipelineLayout) vkDestroyPipelineLayout(device, dispatcherPipelineLayout, nullptr);
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (sumReductionPipelineLayout) vkDestroyPipelineLayout(device, sumReductionPipelineLayout, nullptr);
    if (sumReductionBatchedPipelineLayout) vkDestroyPipelineLayout(device, sumReductionBatchedPipelineLayout, nullptr);
    if (frustumCullPipelineLayout) vkDestroyPipelineLayout(device, frustumCullPipelineLayout, nullptr);
    if (prepareDispatchPipelineLayout) vkDestroyPipelineLayout(device, prepareDispatchPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);

    // Destroy indirect buffers
    if (indirectDispatchBuffer) vmaDestroyBuffer(allocator, indirectDispatchBuffer, indirectDispatchAllocation);
    if (indirectDrawBuffer) vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
    if (visibleIndicesBuffer) vmaDestroyBuffer(allocator, visibleIndicesBuffer, visibleIndicesAllocation);
    if (cullIndirectDispatchBuffer) vmaDestroyBuffer(allocator, cullIndirectDispatchBuffer, cullIndirectDispatchAllocation);

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
    }

    // Destroy composed subsystems
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

    // Indirect draw buffer (4 uints: vertexCount, instanceCount, firstVertex, firstInstance)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 4;
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

        // Initialize with default values (2 triangles = 6 vertices)
        uint32_t drawArgs[4] = {6, 1, 0, 0};
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

    return true;
}

uint32_t TerrainSystem::getTriangleCount() const {
    if (!indirectDrawMappedPtr) {
        return 0;
    }
    // Indirect draw buffer layout: {vertexCount, instanceCount, firstVertex, firstInstance}
    // Triangle count = vertexCount / 3
    const uint32_t* drawArgs = static_cast<const uint32_t*>(indirectDrawMappedPtr);
    return drawArgs[0] / 3;
}

bool TerrainSystem::createComputeDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
    };

    std::array<VkDescriptorSetLayoutBinding, 7> bindings = {
        makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // CBT buffer
        makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // indirect dispatch
        makeComputeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // indirect draw
        makeComputeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER), // height map
        makeComputeBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),   // terrain uniforms
        makeComputeBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),   // visible indices (stream compaction)
        makeComputeBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)};  // cull indirect dispatch

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

    std::array<VkDescriptorSetLayoutBinding, 12> bindings = {
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
        makeGraphicsBinding(13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};

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
    // Allocate compute descriptor sets
    computeDescriptorSets.resize(framesInFlight);
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, computeDescriptorSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    // Allocate render descriptor sets
    renderDescriptorSets.resize(framesInFlight);
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, renderDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, renderDescriptorSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    // Update compute descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 7> writes{};

        VkDescriptorBufferInfo cbtInfo{cbt.getBuffer(), 0, cbt.getBufferSize()};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cbtInfo;

        VkDescriptorBufferInfo dispatchInfo{indirectDispatchBuffer, 0, sizeof(uint32_t) * 3};
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &dispatchInfo;

        VkDescriptorBufferInfo drawInfo{indirectDrawBuffer, 0, sizeof(uint32_t) * 4};
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = computeDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &drawInfo;

        VkDescriptorImageInfo heightMapInfo{heightMap.getSampler(), heightMap.getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = computeDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &heightMapInfo;

        VkDescriptorBufferInfo uniformInfo{uniformBuffers[i], 0, sizeof(TerrainUniforms)};
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = computeDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &uniformInfo;

        VkDescriptorBufferInfo visibleIndicesInfo{visibleIndicesBuffer, 0, sizeof(uint32_t) * (1 + MAX_VISIBLE_TRIANGLES)};
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = computeDescriptorSets[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &visibleIndicesInfo;

        VkDescriptorBufferInfo cullDispatchInfo{cullIndirectDispatchBuffer, 0, sizeof(uint32_t) * 3};
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = computeDescriptorSets[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &cullDispatchInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &dispatcherPipelineLayout) != VK_SUCCESS) {
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
    // Frustum cull pipeline (no push constants needed)
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_frustum_cull.comp.spv");
        if (!shaderModule) return false;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 0;

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
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = renderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderPipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createWireframePipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_wireframe.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

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
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = renderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createShadowPipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainShadowPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
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
                                          VkSampler shadowSampler) {
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

        // Still need the final barrier for rendering (CBT state unchanged but GPU needs it)
        VkMemoryBarrier renderBarrier{};
        renderBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        renderBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        renderBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                            0, 1, &renderBarrier, 0, nullptr, 0, nullptr);
        return;
    }

    // Reset skip tracking
    forceNextCompute = false;
    framesSinceLastCompute = 0;

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    // 1. Dispatcher - set up indirect args
    if (profiler) profiler->beginZone(cmd, "Terrain:Dispatcher");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletVertexCount = 0;
    vkCmdPushConstants(cmd, dispatcherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);

    vkCmdDispatch(cmd, 1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:Dispatcher");

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 2. Subdivision - LOD update (with GPU culling for split phase)
    // Ping-pong between split and merge to avoid race conditions
    // Even frames: split only (with stream compaction), Odd frames: merge only
    uint32_t updateMode = subdivisionFrameCount & 1;  // 0 = split, 1 = merge

    if (updateMode == 0) {
        // Split phase
        if (gpuCullingEnabled) {
            // Use stream compaction to reduce dispatch size
            // 2a. Reset visible count to 0
            vkCmdFillBuffer(cmd, visibleIndicesBuffer, 0, sizeof(uint32_t), 0);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 1, &barrier, 0, nullptr, 0, nullptr);

            // 2b. Frustum culling pass - writes visible indices to compact buffer
            if (profiler) profiler->beginZone(cmd, "Terrain:FrustumCull");

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumCullPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, frustumCullPipelineLayout,
                                   0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

            // Dispatch based on total triangle count (from dispatcher's indirect buffer)
            vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

            if (profiler) profiler->endZone(cmd, "Terrain:FrustumCull");

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 1, &barrier, 0, nullptr, 0, nullptr);

            // 2c. Prepare cull dispatch - convert visible count to dispatch args
            if (profiler) profiler->beginZone(cmd, "Terrain:PrepareDispatch");

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prepareDispatchPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prepareDispatchPipelineLayout,
                                   0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

            TerrainPrepareCullDispatchPushConstants preparePC{};
            preparePC.workgroupSize = SUBDIVISION_WORKGROUP_SIZE;
            vkCmdPushConstants(cmd, prepareDispatchPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(preparePC), &preparePC);

            vkCmdDispatch(cmd, 1, 1, 1);

            if (profiler) profiler->endZone(cmd, "Terrain:PrepareDispatch");

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 1, &barrier, 0, nullptr, 0, nullptr);

            // 2d. Subdivision split pass - indirect dispatch based on visible count
            if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                                   0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

            TerrainSubdivisionPushConstants subdivPC{};
            subdivPC.updateMode = 0;  // Split
            subdivPC.frameIndex = subdivisionFrameCount;
            subdivPC.spreadFactor = 4;
            subdivPC.useCompactBuffer = 1;  // Use stream compaction buffer
            vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(subdivPC), &subdivPC);

            // Use the cull indirect dispatch buffer (reduced dispatch size)
            vkCmdDispatchIndirect(cmd, cullIndirectDispatchBuffer, 0);

            if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
        } else {
            // GPU culling disabled - direct split without stream compaction
            if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                                   0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

            TerrainSubdivisionPushConstants subdivPC{};
            subdivPC.updateMode = 0;  // Split
            subdivPC.frameIndex = subdivisionFrameCount;
            subdivPC.spreadFactor = 4;
            subdivPC.useCompactBuffer = 0;  // Direct indexing (no stream compaction)
            vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(subdivPC), &subdivPC);

            // Use original indirect dispatch (all triangles)
            vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

            if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
        }
    } else {
        // Merge phase: process all triangles directly (no culling)
        if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                               0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

        TerrainSubdivisionPushConstants subdivPC{};
        subdivPC.updateMode = 1;  // Merge
        subdivPC.frameIndex = subdivisionFrameCount;
        subdivPC.spreadFactor = 4;
        subdivPC.useCompactBuffer = 0;  // Direct indexing (merge always processes all)
        vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(subdivPC), &subdivPC);

        // Use the original indirect dispatch (all triangles)
        vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

        if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");
    }

    subdivisionFrameCount++;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &barrier, 0, nullptr, 0, nullptr);

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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &barrier, 0, nullptr, 0, nullptr);

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

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0, 1, &barrier, 0, nullptr, 0, nullptr);
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
    VkMemoryBarrier renderBarrier{};
    renderBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    renderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    renderBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        0, 1, &renderBarrier, 0, nullptr, 0, nullptr);
}

void TerrainSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    VkPipeline pipeline = wireframeMode ? wireframePipeline : renderPipeline;
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

    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void TerrainSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
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

    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

float TerrainSystem::getHeightAt(float x, float z) const {
    return heightMap.getHeightAt(x, z);
}
