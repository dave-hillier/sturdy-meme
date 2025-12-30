#include "BilateralGridSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
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

    vk::Device(device).waitIdle();

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
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e3D)
        .setFormat(static_cast<vk::Format>(GRID_FORMAT))
        .setExtent(vk::Extent3D{GRID_WIDTH, GRID_HEIGHT, GRID_DEPTH})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                  vk::ImageUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                          &gridImages[i], &gridAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create grid image %d", i);
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(gridImages[i])
            .setViewType(vk::ImageViewType::e3D)
            .setFormat(static_cast<vk::Format>(GRID_FORMAT))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        auto viewResult = vk::Device(device).createImageView(viewInfo);
        if (viewResult == vk::ImageView{}) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create grid view %d", i);
            return false;
        }
        gridViews[i] = viewResult;
    }

    return true;
}

void BilateralGridSystem::destroyGridResources() {
    vk::Device vkDevice(device);
    for (int i = 0; i < 2; i++) {
        if (gridViews[i] != VK_NULL_HANDLE) {
            vkDevice.destroyImageView(gridViews[i]);
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
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_FALSE)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    try {
        auto sampler = vk::Device(device).createSampler(samplerInfo);
        gridSampler = ManagedSampler::fromRaw(device, sampler);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create sampler: %s", e.what());
        return false;
    }
    return true;
}

bool BilateralGridSystem::createDescriptorSetLayout() {
    // Build layout: HDR input (sampler) + grid output (storage image) + uniforms
    {
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // HDR input texture
        bindings[0] = vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        // Grid output (storage image)
        bindings[1] = vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        // Uniforms
        bindings[2] = vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
            .setBindings(bindings);

        try {
            auto layout = vk::Device(device).createDescriptorSetLayout(layoutInfo);
            buildDescriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, layout);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create build descriptor set layout: %s", e.what());
            return false;
        }
    }

    // Blur layout: grid src (storage image) + grid dst (storage image) + uniforms
    {
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Grid source
        bindings[0] = vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        // Grid destination
        bindings[1] = vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        // Uniforms
        bindings[2] = vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);

        auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
            .setBindings(bindings);

        try {
            auto layout = vk::Device(device).createDescriptorSetLayout(layoutInfo);
            blurDescriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, layout);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "BilateralGridSystem: Failed to create blur descriptor set layout: %s", e.what());
            return false;
        }
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
    vk::Device vkDevice(device);

    // Pipeline layout
    vk::DescriptorSetLayout setLayout(buildDescriptorSetLayout.get());
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout);

    try {
        auto pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        buildPipelineLayout = ManagedPipelineLayout::fromRaw(device, pipelineLayout);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create build pipeline layout: %s", e.what());
        return false;
    }

    // Load shader
    std::string shaderFile = shaderPath + "/bilateral_grid_build.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModuleOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to load build shader: %s", shaderFile.c_str());
        return false;
    }
    vk::ShaderModule shaderModule(*shaderModuleOpt);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(buildPipelineLayout.get());

    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(shaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create build pipeline");
        return false;
    }
    buildPipeline = ManagedPipeline::fromRaw(device, result.value);
    return true;
}

bool BilateralGridSystem::createBlurPipeline() {
    vk::Device vkDevice(device);

    // Pipeline layout
    vk::DescriptorSetLayout setLayout(blurDescriptorSetLayout.get());
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout);

    try {
        auto pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
        blurPipelineLayout = ManagedPipelineLayout::fromRaw(device, pipelineLayout);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create blur pipeline layout: %s", e.what());
        return false;
    }

    // Load shader
    std::string shaderFile = shaderPath + "/bilateral_grid_blur.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModuleOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to load blur shader: %s", shaderFile.c_str());
        return false;
    }
    vk::ShaderModule shaderModule(*shaderModuleOpt);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(blurPipelineLayout.get());

    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(shaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "BilateralGridSystem: Failed to create blur pipeline");
        return false;
    }
    blurPipeline = ManagedPipeline::fromRaw(device, result.value);
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
        auto srcInfo = vk::DescriptorImageInfo{}
            .setImageLayout(vk::ImageLayout::eGeneral);
        auto dstInfo = vk::DescriptorImageInfo{}
            .setImageLayout(vk::ImageLayout::eGeneral);

        // X: 0 -> 1
        srcInfo.setImageView(gridViews[0]);
        dstInfo.setImageView(gridViews[1]);

        auto bufferInfo = vk::DescriptorBufferInfo{}
            .setBuffer(blurUniformBuffers.buffers[i])
            .setOffset(0)
            .setRange(sizeof(BilateralBlurUniforms));

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = vk::WriteDescriptorSet{}
            .setDstSet(blurDescriptorSetsX[i])
            .setDstBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setPImageInfo(&srcInfo);

        writes[1] = vk::WriteDescriptorSet{}
            .setDstSet(blurDescriptorSetsX[i])
            .setDstBinding(1)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setPImageInfo(&dstInfo);

        writes[2] = vk::WriteDescriptorSet{}
            .setDstSet(blurDescriptorSetsX[i])
            .setDstBinding(2)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setPBufferInfo(&bufferInfo);

        vk::Device(device).updateDescriptorSets(writes, {});

        // Y: 1 -> 0
        srcInfo.setImageView(gridViews[1]);
        dstInfo.setImageView(gridViews[0]);
        writes[0].setDstSet(blurDescriptorSetsY[i]);
        writes[1].setDstSet(blurDescriptorSetsY[i]);
        writes[2].setDstSet(blurDescriptorSetsY[i]);
        vk::Device(device).updateDescriptorSets(writes, {});

        // Z: 0 -> 1 (or skip Z blur for simplicity like GOT sometimes did)
        srcInfo.setImageView(gridViews[0]);
        dstInfo.setImageView(gridViews[1]);
        writes[0].setDstSet(blurDescriptorSetsZ[i]);
        writes[1].setDstSet(blurDescriptorSetsZ[i]);
        writes[2].setDstSet(blurDescriptorSetsZ[i]);
        vk::Device(device).updateDescriptorSets(writes, {});
    }

    return true;
}

void BilateralGridSystem::resize(VkExtent2D newExtent) {
    extent = newExtent;
    // Grid size is fixed, only input extent changes
}

void BilateralGridSystem::recordClearGrid(VkCommandBuffer cmd) {
    // Transition grid[0] to TRANSFER_DST for clearing
    auto subresourceRange = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setImage(gridImages[0])
        .setSubresourceRange(subresourceRange);

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1,
                        reinterpret_cast<const VkImageMemoryBarrier*>(&barrier));

    // Clear to zero
    vk::CommandBuffer vkCmd(cmd);
    auto clearColor = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    auto range = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    vkCmd.clearColorImage(gridImages[0], vk::ImageLayout::eTransferDstOptimal, clearColor, range);

    // Transition both grids to GENERAL for compute in a single batched barrier
    // grid[0]: TRANSFER_DST → GENERAL (after clear)
    // grid[1]: UNDEFINED → GENERAL (no prior dependency)
    std::array<vk::ImageMemoryBarrier, 2> barriers{};

    // grid[0]: needs to wait for transfer (clear) to complete
    barriers[0] = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
        .setImage(gridImages[0])
        .setSubresourceRange(subresourceRange);

    // grid[1]: no prior access, just needs layout transition
    barriers[1] = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
        .setImage(gridImages[1])
        .setSubresourceRange(subresourceRange);

    // Single barrier call for both transitions (TRANSFER covers TOP_OF_PIPE)
    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr,
                        static_cast<uint32_t>(barriers.size()),
                        reinterpret_cast<const VkImageMemoryBarrier*>(barriers.data()));
}

void BilateralGridSystem::recordGridBarrier(VkCommandBuffer cmd, VkImage image,
                                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcAccessMask(static_cast<vk::AccessFlags>(srcAccess))
        .setDstAccessMask(static_cast<vk::AccessFlags>(dstAccess))
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                        reinterpret_cast<const VkImageMemoryBarrier*>(&barrier));
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
    auto hdrInfo = vk::DescriptorImageInfo{}
        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setImageView(hdrInputView)
        .setSampler(gridSampler.get());

    auto gridInfo = vk::DescriptorImageInfo{}
        .setImageLayout(vk::ImageLayout::eGeneral)
        .setImageView(gridViews[0]);

    auto bufferInfo = vk::DescriptorBufferInfo{}
        .setBuffer(buildUniformBuffers.buffers[frameIndex])
        .setOffset(0)
        .setRange(sizeof(BilateralBuildUniforms));

    std::array<vk::WriteDescriptorSet, 3> writes{};
    writes[0] = vk::WriteDescriptorSet{}
        .setDstSet(buildDescriptorSets[frameIndex])
        .setDstBinding(0)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setPImageInfo(&hdrInfo);

    writes[1] = vk::WriteDescriptorSet{}
        .setDstSet(buildDescriptorSets[frameIndex])
        .setDstBinding(1)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eStorageImage)
        .setPImageInfo(&gridInfo);

    writes[2] = vk::WriteDescriptorSet{}
        .setDstSet(buildDescriptorSets[frameIndex])
        .setDstBinding(2)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setPBufferInfo(&bufferInfo);

    vk::Device(device).updateDescriptorSets(writes, {});

    // Build pass
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, buildPipeline.get());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, buildPipelineLayout.get(),
                             0, vk::DescriptorSet(buildDescriptorSets[frameIndex]), {});

    // Dispatch one thread per input pixel
    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Barrier after build
    recordGridBarrier(cmd, gridImages[0],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Blur passes
    BilateralBlurUniforms blurUniforms{};
    blurUniforms.kernelRadius = 4;  // 9-tap kernel
    blurUniforms.sigma = 2.0f;      // Wide blur
    blurUniforms.gridDims = glm::ivec4(GRID_WIDTH, GRID_HEIGHT, GRID_DEPTH, 0);

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, blurPipeline.get());

    uint32_t blurGroupsX = (GRID_WIDTH + 7) / 8;
    uint32_t blurGroupsY = (GRID_HEIGHT + 7) / 8;
    uint32_t blurGroupsZ = (GRID_DEPTH + 7) / 8;

    // X blur: grid[0] -> grid[1]
    blurUniforms.axis = 0;
    vmaMapMemory(allocator, blurUniformBuffers.allocations[frameIndex], &data);
    memcpy(data, &blurUniforms, sizeof(blurUniforms));
    vmaUnmapMemory(allocator, blurUniformBuffers.allocations[frameIndex]);

    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, blurPipelineLayout.get(),
                             0, vk::DescriptorSet(blurDescriptorSetsX[frameIndex]), {});
    vkCmd.dispatch(blurGroupsX, blurGroupsY, blurGroupsZ);

    // Barrier
    recordGridBarrier(cmd, gridImages[1],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Y blur: grid[1] -> grid[0]
    blurUniforms.axis = 1;
    vmaMapMemory(allocator, blurUniformBuffers.allocations[frameIndex], &data);
    memcpy(data, &blurUniforms, sizeof(blurUniforms));
    vmaUnmapMemory(allocator, blurUniformBuffers.allocations[frameIndex]);

    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, blurPipelineLayout.get(),
                             0, vk::DescriptorSet(blurDescriptorSetsY[frameIndex]), {});
    vkCmd.dispatch(blurGroupsX, blurGroupsY, blurGroupsZ);

    // Final barrier for sampling
    recordGridBarrier(cmd, gridImages[0],
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Transition grid[0] to SHADER_READ_ONLY for fragment shader sampling
    auto finalBarrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setImage(gridImages[0])
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1,
                        reinterpret_cast<const VkImageMemoryBarrier*>(&finalBarrier));
}
