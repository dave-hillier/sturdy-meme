#include "SSRSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "VulkanHelpers.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

std::unique_ptr<SSRSystem> SSRSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<SSRSystem>(new SSRSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<SSRSystem> SSRSystem::create(const InitContext& ctx) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.commandPool = ctx.commandPool;
    info.computeQueue = ctx.graphicsQueue;  // Use graphics queue for compute
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.extent = ctx.extent;
    info.descriptorPool = ctx.descriptorPool;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

SSRSystem::~SSRSystem() {
    cleanup();
}

bool SSRSystem::initInternal(const InitInfo& info) {
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SSRSystem requires raiiDevice");
        return false;
    }

    if (!createSSRBuffers()) return false;
    if (!createComputePipeline()) return false;
    if (!createBlurPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("SSRSystem initialized: %dx%d (with bilateral blur)", extent.width, extent.height);
    return true;
}

void SSRSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Clear descriptor sets (pool is shared and managed externally)
    descriptorSets.clear();
    blurDescriptorSets.clear();
    descriptorPool = nullptr;

    // RAII wrappers handle cleanup automatically
    computePipeline_.reset();
    computePipelineLayout_.reset();
    descriptorSetLayout_.reset();

    blurPipeline_.reset();
    blurPipelineLayout_.reset();
    blurDescriptorSetLayout_.reset();

    sampler_.reset();

    // Destroy intermediate buffer
    if (ssrIntermediateView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, ssrIntermediateView, nullptr);
        ssrIntermediateView = VK_NULL_HANDLE;
    }
    if (ssrIntermediate != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, ssrIntermediate, ssrIntermediateAllocation);
        ssrIntermediate = VK_NULL_HANDLE;
        ssrIntermediateAllocation = VK_NULL_HANDLE;
    }

    // Destroy SSR buffers
    for (int i = 0; i < 2; i++) {
        if (ssrResultView[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, ssrResultView[i], nullptr);
            ssrResultView[i] = VK_NULL_HANDLE;
        }
        if (ssrResult[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, ssrResult[i], ssrAllocation[i]);
            ssrResult[i] = VK_NULL_HANDLE;
            ssrAllocation[i] = VK_NULL_HANDLE;
        }
    }

    device = VK_NULL_HANDLE;
}

void SSRSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent.width && newExtent.height == extent.height) {
        return;
    }

    extent = newExtent;

    // Recreate intermediate buffer
    if (ssrIntermediateView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, ssrIntermediateView, nullptr);
        ssrIntermediateView = VK_NULL_HANDLE;
    }
    if (ssrIntermediate != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, ssrIntermediate, ssrIntermediateAllocation);
        ssrIntermediate = VK_NULL_HANDLE;
    }

    // Recreate SSR buffers at new size
    for (int i = 0; i < 2; i++) {
        if (ssrResultView[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, ssrResultView[i], nullptr);
            ssrResultView[i] = VK_NULL_HANDLE;
        }
        if (ssrResult[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, ssrResult[i], ssrAllocation[i]);
            ssrResult[i] = VK_NULL_HANDLE;
        }
    }

    createSSRBuffers();

    // Allocate new descriptor sets from shared pool (old sets will be reused when pool is reset)
    createDescriptorSets();

    SDL_Log("SSRSystem resized to %dx%d", extent.width, extent.height);
}

bool SSRSystem::createSSRBuffers() {
    // Create SSR result images at half resolution for performance
    VkExtent2D ssrExtent = {extent.width / 2, extent.height / 2};
    if (ssrExtent.width < 1) ssrExtent.width = 1;
    if (ssrExtent.height < 1) ssrExtent.height = 1;

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{ssrExtent.width, ssrExtent.height, 1})
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
                           &ssrResult[i], &ssrAllocation[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR result image %d", i);
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(ssrResult[i])
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                              nullptr, &ssrResultView[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR result image view %d", i);
            return false;
        }
    }

    // Create intermediate buffer for blur pass
    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &ssrIntermediate, &ssrIntermediateAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR intermediate image");
        return false;
    }

    {
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(ssrIntermediate)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                              nullptr, &ssrIntermediateView) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR intermediate image view");
            return false;
        }
    }

    // Create sampler
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxLod(1.0f);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR sampler: %s", e.what());
        return false;
    }

    // Transition images to general layout for compute
    {
        CommandScope cmdScope(device, commandPool, computeQueue);
        if (!cmdScope.begin()) return false;

        // Transition both result buffers and intermediate buffer to GENERAL for compute
        for (int i = 0; i < 2; i++) {
            Barriers::prepareImageForCompute(cmdScope.get(), ssrResult[i]);
        }
        Barriers::prepareImageForCompute(cmdScope.get(), ssrIntermediate);

        if (!cmdScope.end()) return false;
    }

    SDL_Log("SSR buffers created at %dx%d (half resolution)", ssrExtent.width, ssrExtent.height);
    return true;
}

bool SSRSystem::createComputePipeline() {
    // Descriptor set layout:
    // 0: HDR color input (sampler2D)
    // 1: Depth buffer input (sampler2D)
    // 2: SSR output (storage image, write)
    // 3: Previous SSR (sampler2D, for temporal)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: HDR color input
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Depth buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: SSR output
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 3: Previous SSR
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Push constant range for SSR parameters
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(SSRPushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **descriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        computePipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR pipeline layout: %s", e.what());
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/ssr.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR compute shader: %s", shaderFile.c_str());
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR compute pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    computePipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("SSR compute pipeline created");
    return true;
}

bool SSRSystem::createBlurPipeline() {
    // Blur descriptor set layout:
    // 0: SSR input (sampler2D)
    // 1: Depth buffer (sampler2D) for bilateral weights
    // 2: Blurred output (storage image, write)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: SSR input
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Depth buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: Blurred output
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur descriptor set layout");
        return false;
    }
    blurDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Push constant range for blur parameters
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(BlurPushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **blurDescriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        blurPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur pipeline layout: %s", e.what());
        return false;
    }

    // Load blur compute shader
    std::string shaderFile = shaderPath + "/ssr_blur.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR blur compute shader: %s", shaderFile.c_str());
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**blurPipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur compute pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    blurPipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("SSR blur compute pipeline created");
    return true;
}

bool SSRSystem::createDescriptorSets() {
    if (!descriptorPool) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SSRSystem: descriptor pool is null");
        return false;
    }

    // Allocate main SSR descriptor sets using shared pool
    descriptorSets = descriptorPool->allocate(**descriptorSetLayout_, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate SSR descriptor sets");
        return false;
    }

    // Allocate blur descriptor sets using shared pool
    blurDescriptorSets = descriptorPool->allocate(**blurDescriptorSetLayout_, framesInFlight);
    if (blurDescriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate SSR blur descriptor sets");
        return false;
    }

    // Note: Descriptor sets will be updated in recordCompute() with the current frame's resources
    return true;
}

void SSRSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                              VkImageView hdrColorView, VkImageView hdrDepthView,
                              const glm::mat4& view, const glm::mat4& proj,
                              const glm::vec3& cameraPos) {
    if (!enabled || descriptorSets.empty()) {
        return;
    }

    // Cache depth view for blur pass
    cachedDepthView = hdrDepthView;

    // Swap ping-pong buffers
    int writeBuffer = 1 - currentBuffer;

    // SSR extent (half resolution)
    VkExtent2D ssrExtent = {extent.width / 2, extent.height / 2};
    uint32_t groupsX = (ssrExtent.width + 7) / 8;
    uint32_t groupsY = (ssrExtent.height + 7) / 8;

    // Determine where SSR writes to:
    // - If blur enabled: write to intermediate, blur will write to final
    // - If blur disabled: write directly to final
    VkImageView ssrOutputView = blurEnabled ? ssrIntermediateView : ssrResultView[writeBuffer];
    VkImage ssrOutputImage = blurEnabled ? ssrIntermediate : ssrResult[writeBuffer];

    // Update descriptor set for main SSR pass using SetWriter
    DescriptorManager::SetWriter(device, descriptorSets[frameIndex])
        .writeImage(0, hdrColorView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeImage(1, hdrDepthView, **sampler_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .writeStorageImage(2, ssrOutputView)
        .writeImage(3, ssrResultView[currentBuffer], **sampler_, VK_IMAGE_LAYOUT_GENERAL)
        .update();

    // Build push constants for main SSR
    SSRPushConstants pc{};
    pc.viewMatrix = view;
    pc.projMatrix = proj;
    pc.invViewMatrix = glm::inverse(view);
    pc.invProjMatrix = glm::inverse(proj);
    pc.cameraPos = glm::vec4(cameraPos, 1.0f);
    pc.screenParams = glm::vec4(
        static_cast<float>(ssrExtent.width),
        static_cast<float>(ssrExtent.height),
        2.0f / static_cast<float>(extent.width),
        2.0f / static_cast<float>(extent.height)
    );
    pc.maxDistance = maxDistance;
    pc.thickness = thickness;
    pc.stride = stride;
    pc.maxSteps = maxSteps;
    pc.fadeStart = fadeStart;
    pc.fadeEnd = fadeEnd;
    pc.temporalBlend = temporalBlend;

    // Bind pipeline and dispatch main SSR pass
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **computePipelineLayout_, 0, vk::DescriptorSet(descriptorSets[frameIndex]), {});
    vkCmd.pushConstants<SSRPushConstants>(
        **computePipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pc);

    vkCmd.dispatch(groupsX, groupsY, 1);

    // If blur is enabled, dispatch blur pass
    if (blurEnabled && blurPipeline_) {
        // Barrier: SSR output -> blur input
        Barriers::transitionImage(cmd, ssrOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // Update blur descriptor set using SetWriter
        DescriptorManager::SetWriter(device, blurDescriptorSets[frameIndex])
            .writeImage(0, ssrIntermediateView, **sampler_, VK_IMAGE_LAYOUT_GENERAL)
            .writeImage(1, hdrDepthView, **sampler_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeStorageImage(2, ssrResultView[writeBuffer])
            .update();

        // Build blur push constants
        BlurPushConstants blurPc{};
        blurPc.resolution = glm::vec2(static_cast<float>(ssrExtent.width), static_cast<float>(ssrExtent.height));
        blurPc.texelSize = glm::vec2(1.0f / ssrExtent.width, 1.0f / ssrExtent.height);
        blurPc.depthThreshold = blurDepthThreshold;
        blurPc.blurRadius = blurRadius;

        // Dispatch blur pass
        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **blurPipeline_);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                 **blurPipelineLayout_, 0, vk::DescriptorSet(blurDescriptorSets[frameIndex]), {});
        vkCmd.pushConstants<BlurPushConstants>(
            **blurPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, blurPc);

        vkCmd.dispatch(groupsX, groupsY, 1);

        // Final barrier: blur output -> fragment shader
        Barriers::transitionImage(cmd, ssrResult[writeBuffer],
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        // No blur - barrier directly to fragment shader
        Barriers::transitionImage(cmd, ssrOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Swap buffers for next frame
    currentBuffer = writeBuffer;
}
