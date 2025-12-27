#include "BilateralGridSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

// UBO structures matching shader layouts
struct BilateralBuildUniforms {
    glm::vec2 inputSize;
    glm::vec2 invInputSize;
    float minLogLum;
    float maxLogLum;
    float invLogLumRange;
    float gridDepth;
    glm::ivec2 gridSize;
    float sigmaRange;
    float pad1;
};

struct BilateralBlurUniforms {
    int32_t axis;
    int32_t kernelRadius;
    float sigma;
    float pad;
    glm::ivec4 gridDims;
};

std::unique_ptr<BilateralGridSystem> BilateralGridSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<BilateralGridSystem>(new BilateralGridSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<BilateralGridSystem> BilateralGridSystem::create(const InitContext& ctx) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    return create(info);
}

BilateralGridSystem::~BilateralGridSystem() {
    cleanup();
}

bool BilateralGridSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createGridTextures()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createUniformBuffers()) return false;
    if (!createBuildPipeline()) return false;
    if (!createBlurPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("BilateralGridSystem: Initialized %ux%ux%u grid",
            GRID_WIDTH, GRID_HEIGHT, GRID_DEPTH);
    return true;
}

void BilateralGridSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    destroyGridResources();

    BufferUtils::destroyBuffers(allocator, buildUniformBuffers);
    BufferUtils::destroyBuffers(allocator, blurUniformBuffers);

    buildDescriptorSetLayout.reset();
    buildPipelineLayout.reset();
    buildPipeline.reset();

    blurDescriptorSetLayout.reset();
    blurPipelineLayout.reset();
    blurPipeline.reset();

    gridSampler.reset();

    device = VK_NULL_HANDLE;
}

bool BilateralGridSystem::createGridTextures() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = GRID_FORMAT;
    imageInfo.extent = {GRID_WIDTH, GRID_HEIGHT, GRID_DEPTH};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                          &gridImages[i], &gridAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create grid image %d", i);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = gridImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = GRID_FORMAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &gridViews[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create grid view %d", i);
            return false;
        }
    }

    return true;
}

void BilateralGridSystem::destroyGridResources() {
    for (int i = 0; i < 2; i++) {
        if (gridViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, gridViews[i], nullptr);
            gridViews[i] = VK_NULL_HANDLE;
        }
        if (gridImages[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, gridImages[i], gridAllocations[i]);
            gridImages[i] = VK_NULL_HANDLE;
            gridAllocations[i] = VK_NULL_HANDLE;
        }
    }
}

bool BilateralGridSystem::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSampler sampler;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create sampler");
        return false;
    }
    gridSampler = ManagedSampler::fromRaw(device, sampler);
    return true;
}

bool BilateralGridSystem::createDescriptorSetLayout() {
    // Build layout: HDR input (sampler) + grid output (storage image) + uniforms
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        // HDR input texture
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Grid output (storage image)
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Uniforms
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create build descriptor set layout");
            return false;
        }
        buildDescriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, layout);
    }

    // Blur layout: grid src (storage image) + grid dst (storage image) + uniforms
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        // Grid source
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Grid destination
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Uniforms
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create blur descriptor set layout");
            return false;
        }
        blurDescriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, layout);
    }

    return true;
}

bool BilateralGridSystem::createUniformBuffers() {
    VkDeviceSize buildSize = sizeof(BilateralBuildUniforms);
    VkDeviceSize blurSize = sizeof(BilateralBlurUniforms);

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(buildSize)
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .build(buildUniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create build uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(blurSize)
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .build(blurUniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create blur uniform buffers");
        return false;
    }

    return true;
}

bool BilateralGridSystem::createBuildPipeline() {
    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout layouts[] = {buildDescriptorSetLayout.get()};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = layouts;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create build pipeline layout");
        return false;
    }
    buildPipelineLayout = ManagedPipelineLayout::fromRaw(device, pipelineLayout);

    // Load shader
    std::string shaderFile = shaderPath + "/bilateral_grid_build.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModuleOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to load build shader: %s", shaderFile.c_str());
        return false;
    }
    VkShaderModule shaderModule = *shaderModuleOpt;

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = buildPipelineLayout.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                               nullptr, &pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create build pipeline");
        return false;
    }
    buildPipeline = ManagedPipeline::fromRaw(device, pipeline);
    return true;
}

bool BilateralGridSystem::createBlurPipeline() {
    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout layouts[] = {blurDescriptorSetLayout.get()};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = layouts;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create blur pipeline layout");
        return false;
    }
    blurPipelineLayout = ManagedPipelineLayout::fromRaw(device, pipelineLayout);

    // Load shader
    std::string shaderFile = shaderPath + "/bilateral_grid_blur.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModuleOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to load blur shader: %s", shaderFile.c_str());
        return false;
    }
    VkShaderModule shaderModule = *shaderModuleOpt;

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = blurPipelineLayout.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                               nullptr, &pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create blur pipeline");
        return false;
    }
    blurPipeline = ManagedPipeline::fromRaw(device, pipeline);
    return true;
}

bool BilateralGridSystem::createDescriptorSets() {
    // Build descriptor sets (one per frame)
    buildDescriptorSets.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        buildDescriptorSets[i] = descriptorPool->allocateSingle(buildDescriptorSetLayout.get());
        if (buildDescriptorSets[i] == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to allocate build descriptor set %u", i);
            return false;
        }
    }

    // Blur descriptor sets (X, Y, Z for each frame)
    blurDescriptorSetsX.resize(framesInFlight);
    blurDescriptorSetsY.resize(framesInFlight);
    blurDescriptorSetsZ.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        blurDescriptorSetsX[i] = descriptorPool->allocateSingle(blurDescriptorSetLayout.get());
        blurDescriptorSetsY[i] = descriptorPool->allocateSingle(blurDescriptorSetLayout.get());
        blurDescriptorSetsZ[i] = descriptorPool->allocateSingle(blurDescriptorSetLayout.get());

        if (blurDescriptorSetsX[i] == VK_NULL_HANDLE ||
            blurDescriptorSetsY[i] == VK_NULL_HANDLE ||
            blurDescriptorSetsZ[i] == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to allocate blur descriptor sets %u", i);
            return false;
        }

        // Update blur descriptor sets with grid images
        // X blur: grid[0] -> grid[1]
        // Y blur: grid[1] -> grid[0]
        // Z blur: grid[0] -> grid[1] (final output in grid[1], but we'll copy back)
        VkDescriptorImageInfo srcInfo{}, dstInfo{};
        srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // X: 0 -> 1
        srcInfo.imageView = gridViews[0];
        dstInfo.imageView = gridViews[1];

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = blurUniformBuffers.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(BilateralBlurUniforms);

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = blurDescriptorSetsX[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &srcInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = blurDescriptorSetsX[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &dstInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = blurDescriptorSetsX[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Y: 1 -> 0
        srcInfo.imageView = gridViews[1];
        dstInfo.imageView = gridViews[0];
        writes[0].dstSet = blurDescriptorSetsY[i];
        writes[1].dstSet = blurDescriptorSetsY[i];
        writes[2].dstSet = blurDescriptorSetsY[i];
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Z: 0 -> 1 (or skip Z blur for simplicity like GOT sometimes did)
        srcInfo.imageView = gridViews[0];
        dstInfo.imageView = gridViews[1];
        writes[0].dstSet = blurDescriptorSetsZ[i];
        writes[1].dstSet = blurDescriptorSetsZ[i];
        writes[2].dstSet = blurDescriptorSetsZ[i];
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

void BilateralGridSystem::resize(VkExtent2D newExtent) {
    extent = newExtent;
    // Grid size is fixed, only input extent changes
}

void BilateralGridSystem::recordClearGrid(VkCommandBuffer cmd) {
    // Transition grid[0] to TRANSFER_DST for clearing
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = gridImages[0];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Clear to zero
    VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(cmd, gridImages[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        &clearColor, 1, &range);

    // Transition both grids to GENERAL for compute in a single batched barrier
    // grid[0]: TRANSFER_DST → GENERAL (after clear)
    // grid[1]: UNDEFINED → GENERAL (no prior dependency)
    std::array<VkImageMemoryBarrier, 2> barriers{};

    // grid[0]: needs to wait for transfer (clear) to complete
    barriers[0] = barrier;  // Copy common fields
    barriers[0].image = gridImages[0];
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    // grid[1]: no prior access, just needs layout transition
    barriers[1] = barrier;  // Copy common fields
    barriers[1].image = gridImages[1];
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    // Single barrier call for both transitions (TRANSFER covers TOP_OF_PIPE)
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr,
                        static_cast<uint32_t>(barriers.size()), barriers.data());
}

void BilateralGridSystem::recordGridBarrier(VkCommandBuffer cmd, VkImage image,
                                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void BilateralGridSystem::recordBilateralGrid(VkCommandBuffer cmd, uint32_t frameIndex,
                                              VkImageView hdrInputView) {
    if (!enabled) return;

    // Clear grid
    recordClearGrid(cmd);

    // Update build uniforms
    BilateralBuildUniforms buildUniforms{};
    buildUniforms.inputSize = glm::vec2(extent.width, extent.height);
    buildUniforms.invInputSize = glm::vec2(1.0f / extent.width, 1.0f / extent.height);
    buildUniforms.minLogLum = MIN_LOG_LUMINANCE;
    buildUniforms.maxLogLum = MAX_LOG_LUMINANCE;
    buildUniforms.invLogLumRange = 1.0f / (MAX_LOG_LUMINANCE - MIN_LOG_LUMINANCE);
    buildUniforms.gridDepth = static_cast<float>(GRID_DEPTH);
    buildUniforms.gridSize = glm::ivec2(GRID_WIDTH, GRID_HEIGHT);
    buildUniforms.sigmaRange = 0.5f;

    void* data;
    vmaMapMemory(allocator, buildUniformBuffers.allocations[frameIndex], &data);
    memcpy(data, &buildUniforms, sizeof(buildUniforms));
    vmaUnmapMemory(allocator, buildUniformBuffers.allocations[frameIndex]);

    // Update build descriptor set with HDR input
    VkDescriptorImageInfo hdrInfo{};
    hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrInfo.imageView = hdrInputView;
    hdrInfo.sampler = gridSampler.get();  // Reuse sampler

    VkDescriptorImageInfo gridInfo{};
    gridInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    gridInfo.imageView = gridViews[0];

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buildUniformBuffers.buffers[frameIndex];
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(BilateralBuildUniforms);

    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = buildDescriptorSets[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &hdrInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = buildDescriptorSets[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &gridInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = buildDescriptorSets[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Build pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildPipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, buildPipelineLayout.get(),
                           0, 1, &buildDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch one thread per input pixel
    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Barrier after build
    recordGridBarrier(cmd, gridImages[0],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Blur passes
    BilateralBlurUniforms blurUniforms{};
    blurUniforms.kernelRadius = 4;  // 9-tap kernel
    blurUniforms.sigma = 2.0f;      // Wide blur
    blurUniforms.gridDims = glm::ivec4(GRID_WIDTH, GRID_HEIGHT, GRID_DEPTH, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipeline.get());

    uint32_t blurGroupsX = (GRID_WIDTH + 7) / 8;
    uint32_t blurGroupsY = (GRID_HEIGHT + 7) / 8;
    uint32_t blurGroupsZ = (GRID_DEPTH + 7) / 8;

    // X blur: grid[0] -> grid[1]
    blurUniforms.axis = 0;
    vmaMapMemory(allocator, blurUniformBuffers.allocations[frameIndex], &data);
    memcpy(data, &blurUniforms, sizeof(blurUniforms));
    vmaUnmapMemory(allocator, blurUniformBuffers.allocations[frameIndex]);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipelineLayout.get(),
                           0, 1, &blurDescriptorSetsX[frameIndex], 0, nullptr);
    vkCmdDispatch(cmd, blurGroupsX, blurGroupsY, blurGroupsZ);

    // Barrier
    recordGridBarrier(cmd, gridImages[1],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Y blur: grid[1] -> grid[0]
    blurUniforms.axis = 1;
    vmaMapMemory(allocator, blurUniformBuffers.allocations[frameIndex], &data);
    memcpy(data, &blurUniforms, sizeof(blurUniforms));
    vmaUnmapMemory(allocator, blurUniformBuffers.allocations[frameIndex]);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipelineLayout.get(),
                           0, 1, &blurDescriptorSetsY[frameIndex], 0, nullptr);
    vkCmdDispatch(cmd, blurGroupsX, blurGroupsY, blurGroupsZ);

    // Final barrier for sampling
    recordGridBarrier(cmd, gridImages[0],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Transition grid[0] to SHADER_READ_ONLY for fragment shader sampling
    VkImageMemoryBarrier finalBarrier{};
    finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalBarrier.image = gridImages[0];
    finalBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    finalBarrier.subresourceRange.baseMipLevel = 0;
    finalBarrier.subresourceRange.levelCount = 1;
    finalBarrier.subresourceRange.baseArrayLayer = 0;
    finalBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &finalBarrier);
}
