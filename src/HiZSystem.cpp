#include "HiZSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

bool HiZSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    depthFormat = info.depthFormat;

    if (!createHiZPyramid()) {
        SDL_Log("HiZSystem: Failed to create Hi-Z pyramid");
        return false;
    }

    if (!createPyramidPipeline()) {
        SDL_Log("HiZSystem: Failed to create pyramid pipeline");
        return false;
    }

    if (!createCullingPipeline()) {
        SDL_Log("HiZSystem: Failed to create culling pipeline");
        return false;
    }

    if (!createBuffers()) {
        SDL_Log("HiZSystem: Failed to create buffers");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_Log("HiZSystem: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("HiZSystem: Initialized with %u mip levels", mipLevelCount);
    return true;
}

void HiZSystem::destroy() {
    destroyDescriptorSets();
    destroyBuffers();
    destroyPipelines();
    destroyHiZPyramid();
}

void HiZSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent.width && newExtent.height == extent.height) {
        return;
    }

    vkDeviceWaitIdle(device);

    extent = newExtent;

    // Recreate Hi-Z pyramid with new size
    destroyHiZPyramid();
    createHiZPyramid();

    // Recreate descriptor sets (they reference the pyramid)
    destroyDescriptorSets();
    createDescriptorSets();
}

bool HiZSystem::createHiZPyramid() {
    mipLevelCount = calculateMipLevels(extent);

    // Create Hi-Z pyramid image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = HIZ_FORMAT;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = mipLevelCount;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &hiZPyramidImage,
                       &hiZPyramidAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create Hi-Z pyramid image");
        return false;
    }

    // Create full image view (all mip levels)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = hiZPyramidImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = HIZ_FORMAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &hiZPyramidView) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create Hi-Z pyramid view");
        return false;
    }

    // Create per-mip-level views for compute writes
    hiZMipViews.resize(mipLevelCount);
    for (uint32_t i = 0; i < mipLevelCount; ++i) {
        viewInfo.subresourceRange.baseMipLevel = i;
        viewInfo.subresourceRange.levelCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &hiZMipViews[i]) != VK_SUCCESS) {
            SDL_Log("HiZSystem: Failed to create Hi-Z mip view %u", i);
            return false;
        }
    }

    // Create sampler for Hi-Z reads
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevelCount);
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &hiZSampler) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create Hi-Z sampler");
        return false;
    }

    return true;
}

void HiZSystem::destroyHiZPyramid() {
    if (hiZSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, hiZSampler, nullptr);
        hiZSampler = VK_NULL_HANDLE;
    }

    for (auto view : hiZMipViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    hiZMipViews.clear();

    if (hiZPyramidView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hiZPyramidView, nullptr);
        hiZPyramidView = VK_NULL_HANDLE;
    }

    if (hiZPyramidImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hiZPyramidImage, hiZPyramidAllocation);
        hiZPyramidImage = VK_NULL_HANDLE;
        hiZPyramidAllocation = VK_NULL_HANDLE;
    }
}

bool HiZSystem::createPyramidPipeline() {
    // Descriptor set layout for pyramid generation
    // Binding 0: Source depth buffer (sampler2D)
    // Binding 1: Source Hi-Z mip (sampler2D) - for subsequent passes
    // Binding 2: Destination Hi-Z mip (storage image)
    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)   // 0: src depth
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)   // 1: src Hi-Z mip
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT);          // 2: dst Hi-Z mip

    pyramidDescSetLayout = layoutBuilder.build();
    if (pyramidDescSetLayout == VK_NULL_HANDLE) {
        SDL_Log("HiZSystem: Failed to create pyramid descriptor set layout");
        return false;
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(uint32_t) * 6;  // srcSize, dstSize, srcMipLevel, isFirstPass

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pyramidDescSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pyramidPipelineLayout) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create pyramid pipeline layout");
        return false;
    }

    // Load compute shader
    VkShaderModule shaderModule = ShaderLoader::loadShaderModule(
        device, shaderPath + "/hiz_downsample.comp.spv");
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_Log("HiZSystem: Failed to load hiz_downsample.comp.spv");
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
    pipelineInfo.layout = pyramidPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                nullptr, &pyramidPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create pyramid compute pipeline");
        return false;
    }

    return true;
}

bool HiZSystem::createCullingPipeline() {
    // Descriptor set layout for culling
    // Binding 0: Uniforms (UBO)
    // Binding 1: Object data (SSBO, read-only)
    // Binding 2: Indirect draw buffer (SSBO, write)
    // Binding 3: Draw count buffer (SSBO, atomic)
    // Binding 4: Hi-Z pyramid (sampler2D)
    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)          // 0: uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)          // 1: objects
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: indirect draw
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)          // 3: draw count
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT);  // 4: Hi-Z pyramid

    cullingDescSetLayout = layoutBuilder.build();
    if (cullingDescSetLayout == VK_NULL_HANDLE) {
        SDL_Log("HiZSystem: Failed to create culling descriptor set layout");
        return false;
    }

    // Pipeline layout (no push constants needed)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &cullingDescSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &cullingPipelineLayout) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create culling pipeline layout");
        return false;
    }

    // Load compute shader
    VkShaderModule shaderModule = ShaderLoader::loadShaderModule(
        device, shaderPath + "/hiz_culling.comp.spv");
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_Log("HiZSystem: Failed to load hiz_culling.comp.spv");
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
    pipelineInfo.layout = cullingPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                nullptr, &cullingPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create culling compute pipeline");
        return false;
    }

    return true;
}

void HiZSystem::destroyPipelines() {
    if (cullingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, cullingPipeline, nullptr);
        cullingPipeline = VK_NULL_HANDLE;
    }
    if (cullingPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, cullingPipelineLayout, nullptr);
        cullingPipelineLayout = VK_NULL_HANDLE;
    }
    if (cullingDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, cullingDescSetLayout, nullptr);
        cullingDescSetLayout = VK_NULL_HANDLE;
    }

    if (pyramidPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pyramidPipeline, nullptr);
        pyramidPipeline = VK_NULL_HANDLE;
    }
    if (pyramidPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pyramidPipelineLayout, nullptr);
        pyramidPipelineLayout = VK_NULL_HANDLE;
    }
    if (pyramidDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, pyramidDescSetLayout, nullptr);
        pyramidDescSetLayout = VK_NULL_HANDLE;
    }
}

bool HiZSystem::createBuffers() {
    VkDeviceSize objectBufferSize = sizeof(CullObjectData) * MAX_OBJECTS;

    // Create object data buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = objectBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &objectDataBuffer,
                        &objectDataAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("HiZSystem: Failed to create object data buffer");
        return false;
    }
    objectBufferCapacity = MAX_OBJECTS;

    // Create indirect draw buffers (per frame)
    VkDeviceSize indirectBufferSize = sizeof(DrawIndexedIndirectCommand) * MAX_OBJECTS;
    bool success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(indirectBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        .setAllocationFlags(0)  // GPU-only
        .build(indirectDrawBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create indirect draw buffers");
        return false;
    }

    // Create draw count buffers (per frame)
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(sizeof(uint32_t))
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(drawCountBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create draw count buffers");
        return false;
    }

    // Create uniform buffers (per frame)
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(sizeof(HiZCullUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create uniform buffers");
        return false;
    }

    return true;
}

void HiZSystem::destroyBuffers() {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, drawCountBuffers);
    BufferUtils::destroyBuffers(allocator, indirectDrawBuffers);

    if (objectDataBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, objectDataBuffer, objectDataAllocation);
        objectDataBuffer = VK_NULL_HANDLE;
    }
}

bool HiZSystem::createDescriptorSets() {
    // Allocate pyramid descriptor sets (one per mip level) using managed pool
    pyramidDescSets = descriptorPool->allocate(pyramidDescSetLayout, mipLevelCount);
    if (pyramidDescSets.size() != mipLevelCount) {
        SDL_Log("HiZSystem: Failed to allocate pyramid descriptor sets");
        return false;
    }

    // Allocate culling descriptor sets (one per frame) using managed pool
    cullingDescSets = descriptorPool->allocate(cullingDescSetLayout, framesInFlight);
    if (cullingDescSets.size() != framesInFlight) {
        SDL_Log("HiZSystem: Failed to allocate culling descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; ++i) {

        // Update culling descriptor set
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = uniformBuffers.buffers[i];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(HiZCullUniforms);

        VkDescriptorBufferInfo objectInfo{};
        objectInfo.buffer = objectDataBuffer;
        objectInfo.offset = 0;
        objectInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indirectInfo{};
        indirectInfo.buffer = indirectDrawBuffers.buffers[i];
        indirectInfo.offset = 0;
        indirectInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo countInfo{};
        countInfo.buffer = drawCountBuffers.buffers[i];
        countInfo.offset = 0;
        countInfo.range = sizeof(uint32_t);

        VkDescriptorImageInfo hiZInfo{};
        hiZInfo.sampler = hiZSampler;
        hiZInfo.imageView = hiZPyramidView;
        hiZInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 5> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cullingDescSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uniformInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cullingDescSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &objectInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = cullingDescSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &indirectInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = cullingDescSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &countInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = cullingDescSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &hiZInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

void HiZSystem::destroyDescriptorSets() {
    // Descriptor sets are freed when the pool is destroyed/reset
    pyramidDescSets.clear();
    cullingDescSets.clear();
}

void HiZSystem::setDepthBuffer(VkImageView depthView, VkSampler depthSampler) {
    sourceDepthView = depthView;
    sourceDepthSampler = depthSampler;

    // Update pyramid descriptor sets with the depth buffer
    if (pyramidDescSets.empty() || hiZMipViews.empty()) {
        return;
    }

    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        VkDescriptorImageInfo srcDepthInfo{};
        srcDepthInfo.sampler = sourceDepthSampler;
        srcDepthInfo.imageView = sourceDepthView;
        srcDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo srcMipInfo{};
        srcMipInfo.sampler = hiZSampler;
        srcMipInfo.imageView = mip > 0 ? hiZMipViews[mip - 1] : hiZMipViews[0];
        srcMipInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo dstMipInfo{};
        dstMipInfo.imageView = hiZMipViews[mip];
        dstMipInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 3> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = pyramidDescSets[mip];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &srcDepthInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = pyramidDescSets[mip];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &srcMipInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = pyramidDescSets[mip];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo = &dstMipInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void HiZSystem::updateUniforms(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj,
                                const glm::vec3& cameraPos, float nearPlane, float farPlane) {
    HiZCullUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        1.0f / static_cast<float>(extent.width),
        1.0f / static_cast<float>(extent.height)
    );
    uniforms.depthParams = glm::vec4(nearPlane, farPlane, static_cast<float>(mipLevelCount), 0.0f);
    uniforms.objectCount = objectCount;
    uniforms.enableHiZ = hiZEnabled ? 1 : 0;

    // Extract frustum planes
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    // Copy to GPU
    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(HiZCullUniforms));
}

void HiZSystem::updateObjectData(const std::vector<CullObjectData>& objects) {
    objectCount = static_cast<uint32_t>(objects.size());

    if (objectCount == 0) {
        return;
    }

    // Ensure buffer is large enough
    if (objectCount > objectBufferCapacity) {
        SDL_Log("HiZSystem: Object count %u exceeds capacity %u", objectCount, objectBufferCapacity);
        objectCount = objectBufferCapacity;
    }

    // Map and copy data
    void* mappedData;
    vmaMapMemory(allocator, objectDataAllocation, &mappedData);
    memcpy(mappedData, objects.data(), sizeof(CullObjectData) * objectCount);
    vmaUnmapMemory(allocator, objectDataAllocation);
}

void HiZSystem::recordPyramidGeneration(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (sourceDepthView == VK_NULL_HANDLE) {
        return;
    }

    // Transition Hi-Z pyramid to general for writing
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = hiZPyramidImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pyramidPipeline);

    // Generate each mip level
    uint32_t srcWidth = extent.width;
    uint32_t srcHeight = extent.height;

    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        uint32_t dstWidth = std::max(1u, srcWidth >> 1);
        uint32_t dstHeight = std::max(1u, srcHeight >> 1);

        // First mip reads from depth buffer, subsequent mips read from previous level
        if (mip == 0) {
            dstWidth = srcWidth;
            dstHeight = srcHeight;
        }

        // Push constants
        struct {
            uint32_t srcWidth, srcHeight;
            uint32_t dstWidth, dstHeight;
            uint32_t srcMipLevel;
            uint32_t isFirstPass;
        } pushConstants = {
            srcWidth, srcHeight,
            dstWidth, dstHeight,
            mip > 0 ? mip - 1 : 0,
            mip == 0 ? 1u : 0u
        };

        vkCmdPushConstants(cmd, pyramidPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushConstants), &pushConstants);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pyramidPipelineLayout, 0, 1, &pyramidDescSets[mip], 0, nullptr);

        // Dispatch
        uint32_t groupsX = (dstWidth + 7) / 8;
        uint32_t groupsY = (dstHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // Barrier between mip levels
        if (mip < mipLevelCount - 1) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.subresourceRange.baseMipLevel = mip;
            barrier.subresourceRange.levelCount = 1;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        srcWidth = dstWidth;
        srcHeight = dstHeight;
    }

    // Transition entire pyramid to shader read for culling
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevelCount;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void HiZSystem::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (objectCount == 0) {
        return;
    }

    // Reset draw count to zero
    vkCmdFillBuffer(cmd, drawCountBuffers.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    // Barrier to ensure fill is complete before compute
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    // Bind culling pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullingPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            cullingPipelineLayout, 0, 1, &cullingDescSets[frameIndex], 0, nullptr);

    // Dispatch
    uint32_t groupCount = (objectCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Barrier before indirect draw
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

VkBuffer HiZSystem::getIndirectDrawBuffer(uint32_t frameIndex) const {
    return indirectDrawBuffers.buffers[frameIndex];
}

VkBuffer HiZSystem::getDrawCountBuffer(uint32_t frameIndex) const {
    return drawCountBuffers.buffers[frameIndex];
}

uint32_t HiZSystem::getVisibleCount(uint32_t frameIndex) const {
    if (drawCountBuffers.mappedPointers.empty()) {
        return 0;
    }
    return *static_cast<uint32_t*>(drawCountBuffers.mappedPointers[frameIndex]);
}

VkImageView HiZSystem::getHiZMipView(uint32_t mipLevel) const {
    if (mipLevel < hiZMipViews.size()) {
        return hiZMipViews[mipLevel];
    }
    return VK_NULL_HANDLE;
}

void HiZSystem::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
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
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0001f) {
            planes[i] /= len;
        }
    }
}
