#include "SSRSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "VulkanRAII.h"
#include <SDL3/SDL.h>
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
    computePipeline = ManagedPipeline();
    computePipelineLayout = ManagedPipelineLayout();
    descriptorSetLayout = ManagedDescriptorSetLayout();

    blurPipeline = ManagedPipeline();
    blurPipelineLayout = ManagedPipelineLayout();
    blurDescriptorSetLayout = ManagedDescriptorSetLayout();

    sampler = ManagedSampler();

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

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {ssrExtent.width, ssrExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                           &ssrResult[i], &ssrAllocation[i], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR result image %d", i);
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = ssrResult[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &ssrResultView[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR result image view %d", i);
            return false;
        }
    }

    // Create intermediate buffer for blur pass
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &ssrIntermediate, &ssrIntermediateAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR intermediate image");
        return false;
    }

    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = ssrIntermediate;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &ssrIntermediateView) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR intermediate image view");
            return false;
        }
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;

    if (!ManagedSampler::create(device, samplerInfo, sampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR sampler");
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

    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: HDR color input
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Depth buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 2: SSR output
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT); // 3: Previous SSR

    if (!layoutBuilder.buildManaged(descriptorSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR descriptor set layout");
        return false;
    }

    // Push constant range for SSR parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SSRPushConstants);

    if (!DescriptorManager::createManagedPipelineLayout(
            device, descriptorSetLayout.get(), computePipelineLayout, {pushConstantRange})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR pipeline layout");
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/ssr.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR compute shader: %s", shaderFile.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = computePipelineLayout.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, computePipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR compute pipeline");
        return false;
    }

    SDL_Log("SSR compute pipeline created");
    return true;
}

bool SSRSystem::createBlurPipeline() {
    // Blur descriptor set layout:
    // 0: SSR input (sampler2D)
    // 1: Depth buffer (sampler2D) for bilateral weights
    // 2: Blurred output (storage image, write)

    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: SSR input
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Depth buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT);         // 2: Blurred output

    if (!layoutBuilder.buildManaged(blurDescriptorSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur descriptor set layout");
        return false;
    }

    // Push constant range for blur parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BlurPushConstants);

    if (!DescriptorManager::createManagedPipelineLayout(
            device, blurDescriptorSetLayout.get(), blurPipelineLayout, {pushConstantRange})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur pipeline layout");
        return false;
    }

    // Load blur compute shader
    std::string shaderFile = shaderPath + "/ssr_blur.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR blur compute shader: %s", shaderFile.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = blurPipelineLayout.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, blurPipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur compute pipeline");
        return false;
    }

    SDL_Log("SSR blur compute pipeline created");
    return true;
}

bool SSRSystem::createDescriptorSets() {
    if (!descriptorPool) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SSRSystem: descriptor pool is null");
        return false;
    }

    // Allocate main SSR descriptor sets using shared pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout.get(), framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate SSR descriptor sets");
        return false;
    }

    // Allocate blur descriptor sets using shared pool
    blurDescriptorSets = descriptorPool->allocate(blurDescriptorSetLayout.get(), framesInFlight);
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
        .writeImage(0, hdrColorView, sampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeImage(1, hdrDepthView, sampler.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .writeStorageImage(2, ssrOutputView)
        .writeImage(3, ssrResultView[currentBuffer], sampler.get(), VK_IMAGE_LAYOUT_GENERAL)
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout.get(), 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdPushConstants(cmd, computePipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(SSRPushConstants), &pc);

    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // If blur is enabled, dispatch blur pass
    if (blurEnabled && blurPipeline) {
        // Barrier: SSR output -> blur input
        Barriers::transitionImage(cmd, ssrOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // Update blur descriptor set using SetWriter
        DescriptorManager::SetWriter(device, blurDescriptorSets[frameIndex])
            .writeImage(0, ssrIntermediateView, sampler.get(), VK_IMAGE_LAYOUT_GENERAL)
            .writeImage(1, hdrDepthView, sampler.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeStorageImage(2, ssrResultView[writeBuffer])
            .update();

        // Build blur push constants
        BlurPushConstants blurPc{};
        blurPc.resolution = glm::vec2(static_cast<float>(ssrExtent.width), static_cast<float>(ssrExtent.height));
        blurPc.texelSize = glm::vec2(1.0f / ssrExtent.width, 1.0f / ssrExtent.height);
        blurPc.depthThreshold = blurDepthThreshold;
        blurPc.blurRadius = blurRadius;

        // Dispatch blur pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipeline.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                blurPipelineLayout.get(), 0, 1, &blurDescriptorSets[frameIndex], 0, nullptr);
        vkCmdPushConstants(cmd, blurPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(BlurPushConstants), &blurPc);

        vkCmdDispatch(cmd, groupsX, groupsY, 1);

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
