#include "GTAOSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "CommandBufferUtils.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

std::unique_ptr<GTAOSystem> GTAOSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<GTAOSystem>(new GTAOSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<GTAOSystem> GTAOSystem::create(const InitContext& ctx) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.commandPool = ctx.commandPool;
    info.computeQueue = ctx.graphicsQueue;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.extent = ctx.extent;
    info.descriptorPool = ctx.descriptorPool;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

GTAOSystem::~GTAOSystem() {
    cleanup();
}

bool GTAOSystem::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    descriptorPool = info.descriptorPool;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GTAOSystem requires raiiDevice");
        return false;
    }

    if (!createAOBuffers()) return false;
    if (!createComputePipeline()) return false;
    if (!createFilterPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("GTAOSystem initialized: %dx%d", extent.width, extent.height);
    return true;
}

void GTAOSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    descriptorSets.clear();
    filterDescriptorSets.clear();
    descriptorPool = nullptr;

    computePipeline_.reset();
    computePipelineLayout_.reset();
    descriptorSetLayout_.reset();

    filterPipeline_.reset();
    filterPipelineLayout_.reset();
    filterDescriptorSetLayout_.reset();

    sampler_.reset();

    // Destroy intermediate buffer
    if (aoIntermediateView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, aoIntermediateView, nullptr);
        aoIntermediateView = VK_NULL_HANDLE;
    }
    if (aoIntermediate != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, aoIntermediate, aoIntermediateAllocation);
        aoIntermediate = VK_NULL_HANDLE;
        aoIntermediateAllocation = VK_NULL_HANDLE;
    }

    // Destroy AO buffers
    for (int i = 0; i < 2; i++) {
        if (aoResultView[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, aoResultView[i], nullptr);
            aoResultView[i] = VK_NULL_HANDLE;
        }
        if (aoResult[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, aoResult[i], aoAllocation[i]);
            aoResult[i] = VK_NULL_HANDLE;
            aoAllocation[i] = VK_NULL_HANDLE;
        }
    }

    device = VK_NULL_HANDLE;
}

void GTAOSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent.width && newExtent.height == extent.height) {
        return;
    }

    extent = newExtent;

    // Destroy intermediate buffer
    if (aoIntermediateView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, aoIntermediateView, nullptr);
        aoIntermediateView = VK_NULL_HANDLE;
    }
    if (aoIntermediate != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, aoIntermediate, aoIntermediateAllocation);
        aoIntermediate = VK_NULL_HANDLE;
    }

    // Destroy AO buffers
    for (int i = 0; i < 2; i++) {
        if (aoResultView[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, aoResultView[i], nullptr);
            aoResultView[i] = VK_NULL_HANDLE;
        }
        if (aoResult[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, aoResult[i], aoAllocation[i]);
            aoResult[i] = VK_NULL_HANDLE;
        }
    }

    createAOBuffers();
    createDescriptorSets();

    SDL_Log("GTAOSystem resized to %dx%d", extent.width, extent.height);
}

bool GTAOSystem::createAOBuffers() {
    // Create AO result images at half resolution for performance
    VkExtent2D aoExtent = {extent.width / 2, extent.height / 2};
    if (aoExtent.width < 1) aoExtent.width = 1;
    if (aoExtent.height < 1) aoExtent.height = 1;

    // R8_UNORM for AO value (0 = fully occluded, 1 = no occlusion)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8Unorm)
        .setExtent(vk::Extent3D{aoExtent.width, aoExtent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                           &aoResult[i], &aoAllocation[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO result image %d", i);
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(aoResult[i])
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8Unorm)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                              nullptr, &aoResultView[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO result image view %d", i);
            return false;
        }
    }

    // Create intermediate buffer for spatial filter
    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &aoIntermediate, &aoIntermediateAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO intermediate image");
        return false;
    }

    {
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(aoIntermediate)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8Unorm)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                              nullptr, &aoIntermediateView) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO intermediate image view");
            return false;
        }
    }

    // Create sampler
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxLod(0.0f);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO sampler: %s", e.what());
        return false;
    }

    // Transition images to general layout for compute
    {
        CommandScope cmdScope(device, commandPool, computeQueue);
        if (!cmdScope.begin()) return false;

        for (int i = 0; i < 2; i++) {
            Barriers::prepareImageForCompute(cmdScope.get(), aoResult[i]);
        }
        Barriers::prepareImageForCompute(cmdScope.get(), aoIntermediate);

        if (!cmdScope.end()) return false;
    }

    SDL_Log("GTAO buffers created at %dx%d (half resolution)", aoExtent.width, aoExtent.height);
    return true;
}

bool GTAOSystem::createComputePipeline() {
    // Descriptor set layout:
    // 0: Depth buffer input (sampler2D)
    // 1: Hi-Z pyramid input (sampler2D)
    // 2: AO output (storage image, write)
    // 3: Previous AO (sampler2D, for temporal)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: Depth buffer
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Hi-Z pyramid
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: AO output
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 3: Previous AO
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(GTAOPushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **descriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        computePipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO pipeline layout: %s", e.what());
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/gtao.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GTAO compute shader: %s", shaderFile.c_str());
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**computePipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO compute pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    computePipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("GTAO compute pipeline created");
    return true;
}

bool GTAOSystem::createFilterPipeline() {
    // Spatial filter descriptor set layout:
    // 0: Raw AO input (sampler2D)
    // 1: Depth buffer (sampler2D) for bilateral weight
    // 2: Filtered output (storage image, write)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: AO input
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Depth buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: Filtered output
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO filter descriptor set layout");
        return false;
    }
    filterDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(FilterPushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **filterDescriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        filterPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO filter pipeline layout: %s", e.what());
        return false;
    }

    // Load filter shader
    std::string shaderFile = shaderPath + "/gtao_spatial_filter.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GTAO filter shader: %s", shaderFile.c_str());
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**filterPipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create GTAO filter pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    filterPipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("GTAO filter pipeline created");
    return true;
}

bool GTAOSystem::createDescriptorSets() {
    if (!descriptorPool) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GTAOSystem: descriptor pool is null");
        return false;
    }

    // Allocate main GTAO descriptor sets
    descriptorSets = descriptorPool->allocate(**descriptorSetLayout_, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate GTAO descriptor sets");
        return false;
    }

    // Allocate filter descriptor sets
    filterDescriptorSets = descriptorPool->allocate(**filterDescriptorSetLayout_, framesInFlight);
    if (filterDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate GTAO filter descriptor sets");
        return false;
    }

    return true;
}

void GTAOSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                               VkImageView depthView,
                               VkImageView hiZView,
                               VkSampler depthSampler,
                               const glm::mat4& view, const glm::mat4& proj,
                               float nearPlane, float farPlane,
                               float frameTime) {
    if (!enabled || descriptorSets.empty()) {
        return;
    }

    // Swap ping-pong buffers
    int writeBuffer = 1 - currentBuffer;

    // AO extent (half resolution)
    VkExtent2D aoExtent = {extent.width / 2, extent.height / 2};
    uint32_t groupsX = (aoExtent.width + 7) / 8;
    uint32_t groupsY = (aoExtent.height + 7) / 8;

    // Determine where GTAO writes to:
    // - If spatial filter enabled: write to intermediate, filter will write to final
    // - If filter disabled: write directly to final
    VkImageView gtaoOutputView = spatialFilterEnabled ? aoIntermediateView : aoResultView[writeBuffer];
    VkImage gtaoOutputImage = spatialFilterEnabled ? aoIntermediate : aoResult[writeBuffer];

    // Update descriptor set for main GTAO pass
    DescriptorManager::SetWriter(device, descriptorSets[frameIndex])
        .writeImage(0, depthView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .writeImage(1, hiZView, depthSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeStorageImage(2, gtaoOutputView)
        .writeImage(3, aoResultView[currentBuffer], **sampler_, VK_IMAGE_LAYOUT_GENERAL)
        .update();

    // Build push constants
    GTAOPushConstants pc{};
    pc.viewMatrix = view;
    pc.projMatrix = proj;
    pc.invProjMatrix = glm::inverse(proj);
    pc.screenParams = glm::vec4(
        static_cast<float>(aoExtent.width),
        static_cast<float>(aoExtent.height),
        1.0f / static_cast<float>(aoExtent.width),
        1.0f / static_cast<float>(aoExtent.height)
    );
    pc.aoParams = glm::vec4(radius, falloff, intensity, bias);
    pc.sampleParams = glm::vec4(
        static_cast<float>(numSlices),
        static_cast<float>(numSteps),
        temporalFilterEnabled ? frameTime : 0.0f,  // Temporal jitter
        thickness
    );
    pc.nearPlane = nearPlane;
    pc.farPlane = farPlane;
    pc.frameTime = frameTime;

    // Bind pipeline and dispatch GTAO pass
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **computePipelineLayout_, 0, vk::DescriptorSet(descriptorSets[frameIndex]), {});
    vkCmd.pushConstants<GTAOPushConstants>(
        **computePipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pc);

    vkCmd.dispatch(groupsX, groupsY, 1);

    // If spatial filter is enabled, dispatch filter pass
    if (spatialFilterEnabled && filterPipeline_) {
        // Barrier: GTAO output -> filter input
        Barriers::transitionImage(cmd, gtaoOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // Update filter descriptor set
        DescriptorManager::SetWriter(device, filterDescriptorSets[frameIndex])
            .writeImage(0, aoIntermediateView, **sampler_, VK_IMAGE_LAYOUT_GENERAL)
            .writeImage(1, depthView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeStorageImage(2, aoResultView[writeBuffer])
            .update();

        // Build filter push constants
        FilterPushConstants filterPc{};
        filterPc.resolution = glm::vec2(static_cast<float>(aoExtent.width), static_cast<float>(aoExtent.height));
        filterPc.texelSize = glm::vec2(1.0f / aoExtent.width, 1.0f / aoExtent.height);
        filterPc.depthThreshold = 0.01f;
        filterPc.blurSharpness = 8.0f;

        // Dispatch filter pass
        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **filterPipeline_);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                 **filterPipelineLayout_, 0, vk::DescriptorSet(filterDescriptorSets[frameIndex]), {});
        vkCmd.pushConstants<FilterPushConstants>(
            **filterPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, filterPc);

        vkCmd.dispatch(groupsX, groupsY, 1);

        // Final barrier: filter output -> fragment shader
        Barriers::transitionImage(cmd, aoResult[writeBuffer],
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        // No filter - barrier directly to fragment shader
        Barriers::transitionImage(cmd, gtaoOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Swap buffers for next frame
    currentBuffer = writeBuffer;
}
