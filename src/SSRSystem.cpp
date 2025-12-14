#include "SSRSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include "VulkanBarriers.h"
#include "VulkanRAII.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

bool SSRSystem::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;

    if (!createSSRBuffers()) return false;
    if (!createComputePipeline()) return false;
    if (!createBlurPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("SSRSystem initialized: %dx%d (with bilateral blur)", extent.width, extent.height);
    return true;
}

bool SSRSystem::init(const InitContext& ctx) {
    device = ctx.device;
    physicalDevice = ctx.physicalDevice;
    allocator = ctx.allocator;
    commandPool = ctx.commandPool;
    computeQueue = ctx.graphicsQueue;  // Use graphics queue for compute
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    extent = ctx.extent;

    if (!createSSRBuffers()) return false;
    if (!createComputePipeline()) return false;
    if (!createBlurPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("SSRSystem initialized: %dx%d (with bilateral blur)", extent.width, extent.height);
    return true;
}

void SSRSystem::destroy() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Destroy main pipeline resources
    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }
    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy blur pipeline resources
    if (blurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, blurPipeline, nullptr);
        blurPipeline = VK_NULL_HANDLE;
    }
    if (blurPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr);
        blurPipelineLayout = VK_NULL_HANDLE;
    }
    if (blurDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, blurDescriptorSetLayout, nullptr);
        blurDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy sampler
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

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

    // Recreate descriptor sets with new image views
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
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

    auto colorBinding = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    auto depthBinding = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    auto outputBinding = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    auto prevBinding = BindingBuilder()
        .setBinding(3)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
        colorBinding, depthBinding, outputBinding, prevBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR descriptor set layout");
        return false;
    }

    // Push constant range for SSR parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SSRPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR pipeline layout");
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/ssr.comp.spv";
    VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR compute shader: %s", shaderFile.c_str());
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
    pipelineInfo.layout = computePipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &computePipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
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

    auto ssrInputBinding = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    auto depthBinding = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    auto outputBinding = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
        ssrInputBinding, depthBinding, outputBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &blurDescriptorSetLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur descriptor set layout");
        return false;
    }

    // Push constant range for blur parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BlurPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &blurDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &blurPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur pipeline layout");
        return false;
    }

    // Load blur compute shader
    std::string shaderFile = shaderPath + "/ssr_blur.comp.spv";
    VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SSR blur compute shader: %s", shaderFile.c_str());
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
    pipelineInfo.layout = blurPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &blurPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR blur compute pipeline");
        return false;
    }

    SDL_Log("SSR blur compute pipeline created");
    return true;
}

bool SSRSystem::createDescriptorSets() {
    // Create descriptor pool - need sets for both main SSR and blur passes
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // Main SSR: color, depth, prev (3 per frame)
    // Blur: ssr input, depth (2 per frame)
    poolSizes[0].descriptorCount = framesInFlight * 5;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    // Main SSR: output (1 per frame)
    // Blur: output (1 per frame)
    poolSizes[1].descriptorCount = framesInFlight * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight * 2;  // main SSR + blur per frame

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SSR descriptor pool");
        return false;
    }

    // Allocate main SSR descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate SSR descriptor sets");
        return false;
    }

    // Allocate blur descriptor sets
    std::vector<VkDescriptorSetLayout> blurLayouts(framesInFlight, blurDescriptorSetLayout);
    allocInfo.pSetLayouts = blurLayouts.data();

    blurDescriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, blurDescriptorSets.data()) != VK_SUCCESS) {
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

    // Update descriptor set for main SSR pass
    VkDescriptorImageInfo colorInfo{};
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorInfo.imageView = hdrColorView;
    colorInfo.sampler = sampler;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = hdrDepthView;
    depthInfo.sampler = sampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = ssrOutputView;

    VkDescriptorImageInfo prevInfo{};
    prevInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    prevInfo.imageView = ssrResultView[currentBuffer];
    prevInfo.sampler = sampler;

    std::array<VkWriteDescriptorSet, 4> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &colorInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &depthInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &outputInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSets[frameIndex];
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &prevInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(SSRPushConstants), &pc);

    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // If blur is enabled, dispatch blur pass
    if (blurEnabled && blurPipeline != VK_NULL_HANDLE) {
        // Barrier: SSR output -> blur input
        Barriers::transitionImage(cmd, ssrOutputImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // Update blur descriptor set
        VkDescriptorImageInfo blurInputInfo{};
        blurInputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blurInputInfo.imageView = ssrIntermediateView;
        blurInputInfo.sampler = sampler;

        VkDescriptorImageInfo blurDepthInfo{};
        blurDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        blurDepthInfo.imageView = hdrDepthView;
        blurDepthInfo.sampler = sampler;

        VkDescriptorImageInfo blurOutputInfo{};
        blurOutputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blurOutputInfo.imageView = ssrResultView[writeBuffer];

        std::array<VkWriteDescriptorSet, 3> blurWrites{};

        blurWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blurWrites[0].dstSet = blurDescriptorSets[frameIndex];
        blurWrites[0].dstBinding = 0;
        blurWrites[0].descriptorCount = 1;
        blurWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blurWrites[0].pImageInfo = &blurInputInfo;

        blurWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blurWrites[1].dstSet = blurDescriptorSets[frameIndex];
        blurWrites[1].dstBinding = 1;
        blurWrites[1].descriptorCount = 1;
        blurWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blurWrites[1].pImageInfo = &blurDepthInfo;

        blurWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blurWrites[2].dstSet = blurDescriptorSets[frameIndex];
        blurWrites[2].dstBinding = 2;
        blurWrites[2].descriptorCount = 1;
        blurWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        blurWrites[2].pImageInfo = &blurOutputInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(blurWrites.size()), blurWrites.data(), 0, nullptr);

        // Build blur push constants
        BlurPushConstants blurPc{};
        blurPc.resolution = glm::vec2(static_cast<float>(ssrExtent.width), static_cast<float>(ssrExtent.height));
        blurPc.texelSize = glm::vec2(1.0f / ssrExtent.width, 1.0f / ssrExtent.height);
        blurPc.depthThreshold = blurDepthThreshold;
        blurPc.blurRadius = blurRadius;

        // Dispatch blur pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                blurPipelineLayout, 0, 1, &blurDescriptorSets[frameIndex], 0, nullptr);
        vkCmdPushConstants(cmd, blurPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
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
