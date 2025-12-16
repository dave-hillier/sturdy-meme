#include "PostProcessSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <array>

bool PostProcessSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    outputRenderPass = info.outputRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    swapchainFormat = info.swapchainFormat;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createHDRRenderTarget()) return false;
    if (!createHDRRenderPass()) return false;
    if (!createHDRFramebuffer()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSets()) return false;
    if (!createCompositePipeline()) return false;

    // Histogram-based auto-exposure
    if (!createHistogramResources()) return false;
    if (!createHistogramPipelines()) return false;
    if (!createHistogramDescriptorSets()) return false;

    return true;
}

bool PostProcessSystem::init(const InitContext& ctx, VkRenderPass outputRenderPass_, VkFormat swapchainFormat_) {
    device = ctx.device;
    allocator = ctx.allocator;
    outputRenderPass = outputRenderPass_;
    descriptorPool = ctx.descriptorPool;
    extent = ctx.extent;
    swapchainFormat = swapchainFormat_;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;

    if (!createHDRRenderTarget()) return false;
    if (!createHDRRenderPass()) return false;
    if (!createHDRFramebuffer()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSets()) return false;
    if (!createCompositePipeline()) return false;

    // Histogram-based auto-exposure
    if (!createHistogramResources()) return false;
    if (!createHistogramPipelines()) return false;
    if (!createHistogramDescriptorSets()) return false;

    return true;
}

void PostProcessSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyHDRResources();
    destroyHistogramResources();

    BufferUtils::destroyBuffers(allocator, uniformBuffers);

    for (auto& pipeline : compositePipelines) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (compositePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
        compositePipelineLayout = VK_NULL_HANDLE;
    }
    if (compositeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, compositeDescriptorSetLayout, nullptr);
        compositeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    hdrSampler.destroy();
    if (hdrRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, hdrRenderPass, nullptr);
        hdrRenderPass = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::destroyHDRResources() {
    if (hdrFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, hdrFramebuffer, nullptr);
        hdrFramebuffer = VK_NULL_HANDLE;
    }
    if (hdrColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hdrColorView, nullptr);
        hdrColorView = VK_NULL_HANDLE;
    }
    if (hdrColorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hdrColorImage, hdrColorAllocation);
        hdrColorImage = VK_NULL_HANDLE;
        hdrColorAllocation = VK_NULL_HANDLE;
    }
    if (hdrDepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hdrDepthView, nullptr);
        hdrDepthView = VK_NULL_HANDLE;
    }
    if (hdrDepthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hdrDepthImage, hdrDepthAllocation);
        hdrDepthImage = VK_NULL_HANDLE;
        hdrDepthAllocation = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    extent = newExtent;
    destroyHDRResources();
    createHDRRenderTarget();
    createHDRFramebuffer();

    // Update descriptor sets with new image view
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, compositeDescriptorSets[i])
            .writeImage(0, hdrColorView, hdrSampler.get())
            .update();
    }
}

bool PostProcessSystem::createHDRRenderTarget() {
    // Create HDR color image
    VkImageCreateInfo colorImageInfo{};
    colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
    colorImageInfo.extent.width = extent.width;
    colorImageInfo.extent.height = extent.height;
    colorImageInfo.extent.depth = 1;
    colorImageInfo.mipLevels = 1;
    colorImageInfo.arrayLayers = 1;
    colorImageInfo.format = HDR_FORMAT;
    colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_STORAGE_BIT;
    colorImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo colorAllocInfo{};
    colorAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &colorImageInfo, &colorAllocInfo,
                       &hdrColorImage, &hdrColorAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image");
        return false;
    }

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = hdrColorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = HDR_FORMAT;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.baseMipLevel = 0;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.baseArrayLayer = 0;
    colorViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &colorViewInfo, nullptr, &hdrColorView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image view");
        return false;
    }

    // Create HDR depth image
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = extent.width;
    depthImageInfo.extent.height = extent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = DEPTH_FORMAT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &depthImageInfo, &depthAllocInfo,
                       &hdrDepthImage, &hdrDepthAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image");
        return false;
    }

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = hdrDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = DEPTH_FORMAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &depthViewInfo, nullptr, &hdrDepthView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image view");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = HDR_FORMAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = DEPTH_FORMAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store for sampling in post-process
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;  // For sampling

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &hdrRenderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR render pass");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRFramebuffer() {
    std::array<VkImageView, 2> attachments = {hdrColorView, hdrDepthView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = hdrRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &hdrFramebuffer) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR framebuffer");
        return false;
    }

    return true;
}

bool PostProcessSystem::createSampler() {
    if (!ManagedSampler::createLinearClamp(device, hdrSampler)) {
        SDL_Log("Failed to create HDR sampler");
        return false;
    }

    return true;
}

bool PostProcessSystem::createDescriptorSetLayout() {
    compositeDescriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: HDR color
        .addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 1: uniforms
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: depth
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: froxel
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 4: bloom
        .build();

    if (compositeDescriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create composite descriptor set layout");
        return false;
    }

    return true;
}

bool PostProcessSystem::createUniformBuffers() {
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(sizeof(PostProcessUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(uniformBuffers)) {
        SDL_Log("Failed to create post-process uniform buffers");
        return false;
    }

    // Initialize with defaults
    for (size_t i = 0; i < framesInFlight; i++) {
        PostProcessUniforms* ubo = static_cast<PostProcessUniforms*>(uniformBuffers.mappedPointers[i]);
        ubo->exposure = 0.0f;
        ubo->bloomThreshold = 1.0f;
        ubo->bloomIntensity = 0.5f;
        ubo->autoExposure = 1.0f;  // Enable by default
    }

    return true;
}

bool PostProcessSystem::createDescriptorSets() {
    // Allocate composite descriptor sets using managed pool
    compositeDescriptorSets = descriptorPool->allocate(compositeDescriptorSetLayout, framesInFlight);
    if (compositeDescriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate composite descriptor sets");
        return false;
    }

    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter writer(device, compositeDescriptorSets[i]);
        writer
            .writeImage(0, hdrColorView, hdrSampler.get())
            .writeBuffer(1, uniformBuffers.buffers[i], 0, sizeof(PostProcessUniforms))
            .writeImage(2, hdrDepthView, hdrSampler.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        if (froxelVolumeView != VK_NULL_HANDLE && froxelSampler != VK_NULL_HANDLE) {
            writer.writeImage(3, froxelVolumeView, froxelSampler);
        }
        if (bloomView != VK_NULL_HANDLE && bloomSampler != VK_NULL_HANDLE) {
            writer.writeImage(4, bloomView, bloomSampler);
        }

        writer.update();
    }

    return true;
}

bool PostProcessSystem::createCompositePipeline() {
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/postprocess.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/postprocess.frag.spv");

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        SDL_Log("Failed to load post-process shaders");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    // Specialization constant for god ray sample count
    // constant_id = 0 maps to GOD_RAY_SAMPLES in shader
    VkSpecializationMapEntry specMapEntry{};
    specMapEntry.constantID = 0;
    specMapEntry.offset = 0;
    specMapEntry.size = sizeof(int32_t);

    // Sample counts for each quality level: Low=16, Medium=32, High=64
    const int32_t sampleCounts[3] = {16, 32, 64};

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // No vertex input (fullscreen triangle generated in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    compositePipelineLayout = DescriptorManager::createPipelineLayout(device, compositeDescriptorSetLayout);
    if (compositePipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create composite pipeline layout");
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = compositePipelineLayout;
    pipelineInfo.renderPass = outputRenderPass;
    pipelineInfo.subpass = 0;

    // Create pipeline variants for each god ray quality level
    for (int i = 0; i < 3; i++) {
        VkSpecializationInfo specInfo{};
        specInfo.mapEntryCount = 1;
        specInfo.pMapEntries = &specMapEntry;
        specInfo.dataSize = sizeof(int32_t);
        specInfo.pData = &sampleCounts[i];

        // Update fragment shader stage with specialization info
        shaderStages[1].pSpecializationInfo = &specInfo;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipelines[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create composite graphics pipeline variant %d", i);
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
            vkDestroyShaderModule(device, fragShaderModule, nullptr);
            return false;
        }
        SDL_Log("Created post-process pipeline variant %d (god ray samples: %d)", i, sampleCounts[i]);
    }

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);

    return true;
}

void PostProcessSystem::recordPostProcess(VkCommandBuffer cmd, uint32_t frameIndex,
                                          VkFramebuffer swapchainFB, float deltaTime,
                                          PreEndCallback preEndCallback) {
    // Run histogram compute pass for auto-exposure (if enabled)
    recordHistogramCompute(cmd, frameIndex, deltaTime);

    // Read computed exposure from previous frame's buffer (to avoid GPU stall)
    // Use a different frame index for reading to ensure the data is ready
    uint32_t readFrameIndex = (frameIndex + framesInFlight - 1) % framesInFlight;
    float computedExposure = manualExposure;

    if (autoExposureEnabled && exposureBuffers.mappedPointers.size() > readFrameIndex) {
        // Invalidate to ensure CPU sees GPU writes
        vmaInvalidateAllocation(allocator, exposureBuffers.allocations[readFrameIndex], 0, sizeof(ExposureData));

        ExposureData* exposureData = static_cast<ExposureData*>(exposureBuffers.mappedPointers[readFrameIndex]);
        computedExposure = exposureData->adaptedExposure;
        currentExposure = computedExposure;
        adaptedLuminance = exposureData->averageLuminance;
    }

    // Update uniform buffer
    PostProcessUniforms* ubo = static_cast<PostProcessUniforms*>(uniformBuffers.mappedPointers[frameIndex]);
    ubo->exposure = autoExposureEnabled ? computedExposure : manualExposure;
    ubo->autoExposure = 0.0f;  // Disable fragment shader auto-exposure (now using compute)
    ubo->previousExposure = lastAutoExposure;
    ubo->deltaTime = deltaTime;
    ubo->adaptationSpeed = 2.0f;  // Smooth adaptation over ~0.5 seconds
    ubo->bloomThreshold = bloomThreshold;
    ubo->bloomIntensity = bloomIntensity;
    ubo->bloomRadius = bloomRadius;
    // God rays (Phase 4.4)
    ubo->sunScreenPos = sunScreenPos;
    ubo->godRayIntensity = godRayIntensity;
    ubo->godRayDecay = godRayDecay;
    // Froxel volumetrics (Phase 4.3)
    ubo->froxelEnabled = froxelEnabled ? 1.0f : 0.0f;
    ubo->froxelFarPlane = froxelFarPlane;
    ubo->froxelDepthDist = froxelDepthDist;
    ubo->nearPlane = nearPlane;
    ubo->farPlane = farPlane;
    // Purkinje effect (Phase 5.6)
    // Convert adapted luminance to approximate scene illuminance in lux
    // Mapping: adaptedLuminance * 200 gives reasonable lux-like values
    // where target luminance 0.05 → 10 lux (Purkinje activation threshold)
    ubo->sceneIlluminance = adaptedLuminance * 200.0f;
    // HDR tonemapping bypass toggle
    ubo->hdrEnabled = hdrEnabled ? 1.0f : 0.0f;
    // Quality settings
    ubo->godRaysEnabled = godRaysEnabled ? 1.0f : 0.0f;
    ubo->froxelFilterQuality = froxelFilterHighQuality ? 1.0f : 0.0f;
    ubo->bloomEnabled = bloomEnabled ? 1.0f : 0.0f;
    ubo->autoExposureEnabled = autoExposureEnabled ? 1.0f : 0.0f;

    // Store computed exposure for next frame
    lastAutoExposure = autoExposureEnabled ? computedExposure : manualExposure;

    // Begin swapchain render pass for final composite
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = outputRenderPass;
    renderPassInfo.framebuffer = swapchainFB;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Select pipeline variant based on god ray quality setting
    VkPipeline selectedPipeline = compositePipelines[static_cast<int>(godRayQuality)];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, selectedPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout,
                            0, 1, &compositeDescriptorSets[frameIndex], 0, nullptr);

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

    // Draw fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Call pre-end callback (e.g., for GUI rendering)
    if (preEndCallback) {
        preEndCallback(cmd);
    }

    vkCmdEndRenderPass(cmd);
}

bool PostProcessSystem::createHistogramResources() {
    // Create histogram buffer (256 uint values)
    VkBufferCreateInfo histogramBufferInfo{};
    histogramBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    histogramBufferInfo.size = HISTOGRAM_BINS * sizeof(uint32_t);
    histogramBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    histogramBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo histogramAllocInfo{};
    histogramAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator, &histogramBufferInfo, &histogramAllocInfo,
                        &histogramBuffer, &histogramAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create histogram buffer");
        return false;
    }

    // Create per-frame exposure buffers (readable from CPU, writable from GPU)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(sizeof(ExposureData))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build(exposureBuffers)) {
        SDL_Log("Failed to create exposure buffers");
        return false;
    }

    // Initialize exposure data
    for (uint32_t i = 0; i < framesInFlight; i++) {
        ExposureData* data = static_cast<ExposureData*>(exposureBuffers.mappedPointers[i]);
        data->averageLuminance = 0.18f;
        data->exposureValue = 0.0f;
        data->previousExposure = 0.0f;
        data->adaptedExposure = 0.0f;

        // Flush to ensure initial values are visible to GPU
        vmaFlushAllocation(allocator, exposureBuffers.allocations[i], 0, sizeof(ExposureData));
    }

    // Create per-frame histogram params buffers
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(sizeof(HistogramReduceParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(histogramParamsBuffers)) {
        SDL_Log("Failed to create histogram params buffers");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHistogramPipelines() {
    // ============================================
    // Histogram Build Pipeline
    // ============================================
    {
        // Descriptor set layout for histogram build
        histogramBuildDescLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)   // 0: HDR color
            .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: histogram
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)  // 2: params
            .build();

        if (histogramBuildDescLayout == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram build descriptor set layout");
            return false;
        }

        histogramBuildPipelineLayout = DescriptorManager::createPipelineLayout(device, histogramBuildDescLayout);
        if (histogramBuildPipelineLayout == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram build pipeline layout");
            return false;
        }

        // Load shader
        auto shaderCode = ShaderLoader::readFile(shaderPath + "/histogram_build.comp.spv");
        if (shaderCode.empty()) {
            SDL_Log("Failed to load histogram build shader");
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::createShaderModule(device, shaderCode);
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram build shader module");
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
        pipelineInfo.layout = histogramBuildPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &histogramBuildPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            SDL_Log("Failed to create histogram build pipeline");
            return false;
        }
    }

    // ============================================
    // Histogram Reduce Pipeline
    // ============================================
    {
        // Descriptor set layout for histogram reduce
        histogramReduceDescLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: histogram
            .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: exposure
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)  // 2: params
            .build();

        if (histogramReduceDescLayout == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram reduce descriptor set layout");
            return false;
        }

        histogramReducePipelineLayout = DescriptorManager::createPipelineLayout(device, histogramReduceDescLayout);
        if (histogramReducePipelineLayout == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram reduce pipeline layout");
            return false;
        }

        // Load shader
        auto shaderCode = ShaderLoader::readFile(shaderPath + "/histogram_reduce.comp.spv");
        if (shaderCode.empty()) {
            SDL_Log("Failed to load histogram reduce shader");
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::createShaderModule(device, shaderCode);
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_Log("Failed to create histogram reduce shader module");
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
        pipelineInfo.layout = histogramReducePipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &histogramReducePipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            SDL_Log("Failed to create histogram reduce pipeline");
            return false;
        }
    }

    return true;
}

bool PostProcessSystem::createHistogramDescriptorSets() {
    // Allocate histogram build descriptor sets using managed pool
    histogramBuildDescSets = descriptorPool->allocate(histogramBuildDescLayout, framesInFlight);
    if (histogramBuildDescSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate histogram build descriptor sets");
        return false;
    }

    // Allocate histogram reduce descriptor sets using managed pool
    histogramReduceDescSets = descriptorPool->allocate(histogramReduceDescLayout, framesInFlight);
    if (histogramReduceDescSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate histogram reduce descriptor sets");
        return false;
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Build descriptor set
        DescriptorManager::SetWriter(device, histogramBuildDescSets[i])
            .writeStorageImage(0, hdrColorView)
            .writeBuffer(1, histogramBuffer, 0, HISTOGRAM_BINS * sizeof(uint32_t),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, histogramParamsBuffers.buffers[i], 0, sizeof(HistogramParams))
            .update();

        // Reduce descriptor set
        DescriptorManager::SetWriter(device, histogramReduceDescSets[i])
            .writeBuffer(0, histogramBuffer, 0, HISTOGRAM_BINS * sizeof(uint32_t),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, exposureBuffers.buffers[i], 0, sizeof(ExposureData),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, histogramParamsBuffers.buffers[i], 0, sizeof(HistogramReduceParams))
            .update();
    }

    return true;
}

void PostProcessSystem::destroyHistogramResources() {
    if (histogramBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, histogramBuffer, histogramAllocation);
        histogramBuffer = VK_NULL_HANDLE;
    }

    BufferUtils::destroyBuffers(allocator, exposureBuffers);
    BufferUtils::destroyBuffers(allocator, histogramParamsBuffers);

    if (histogramBuildPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, histogramBuildPipeline, nullptr);
        histogramBuildPipeline = VK_NULL_HANDLE;
    }
    if (histogramReducePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, histogramReducePipeline, nullptr);
        histogramReducePipeline = VK_NULL_HANDLE;
    }

    if (histogramBuildPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, histogramBuildPipelineLayout, nullptr);
        histogramBuildPipelineLayout = VK_NULL_HANDLE;
    }
    if (histogramReducePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, histogramReducePipelineLayout, nullptr);
        histogramReducePipelineLayout = VK_NULL_HANDLE;
    }

    if (histogramBuildDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, histogramBuildDescLayout, nullptr);
        histogramBuildDescLayout = VK_NULL_HANDLE;
    }
    if (histogramReduceDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, histogramReduceDescLayout, nullptr);
        histogramReduceDescLayout = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::recordHistogramCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime) {
    if (!autoExposureEnabled) return;

    // Update histogram parameters (HistogramReduceParams is a superset of HistogramBuildParams)
    // Both shaders read from the same buffer, so we only need to write once
    float logRange = MAX_LOG_LUMINANCE - MIN_LOG_LUMINANCE;
    HistogramReduceParams* params = static_cast<HistogramReduceParams*>(histogramParamsBuffers.mappedPointers[frameIndex]);
    params->minLogLum = MIN_LOG_LUMINANCE;
    params->maxLogLum = MAX_LOG_LUMINANCE;
    params->invLogLumRange = 1.0f / logRange;
    params->pixelCount = extent.width * extent.height;
    params->lowPercentile = LOW_PERCENTILE;
    params->highPercentile = HIGH_PERCENTILE;
    params->targetLuminance = TARGET_LUMINANCE;
    params->deltaTime = deltaTime;
    params->adaptSpeedUp = ADAPTATION_SPEED_UP;
    params->adaptSpeedDown = ADAPTATION_SPEED_DOWN;
    params->minExposure = MIN_EXPOSURE;
    params->maxExposure = MAX_EXPOSURE;

    // Flush mapped memory to ensure CPU writes are visible to GPU
    // (required if memory is not HOST_COHERENT)
    vmaFlushAllocation(allocator, histogramParamsBuffers.allocations[frameIndex], 0, sizeof(HistogramReduceParams));

    // Transition HDR image to general layout for compute access
    Barriers::transitionImage(cmd, hdrColorImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // Clear histogram buffer
    Barriers::clearBufferForComputeReadWrite(cmd, histogramBuffer, 0, HISTOGRAM_BINS * sizeof(uint32_t));

    // Dispatch histogram build
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramBuildPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramBuildPipelineLayout,
                            0, 1, &histogramBuildDescSets[frameIndex], 0, nullptr);

    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    barrierHistogramBuildToReduce(cmd);

    // Dispatch histogram reduce (single workgroup of 256 threads)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramReducePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramReducePipelineLayout,
                            0, 1, &histogramReduceDescSets[frameIndex], 0, nullptr);
    vkCmdDispatch(cmd, 1, 1, 1);

    barrierHistogramReduceComplete(cmd, frameIndex);
}

void PostProcessSystem::barrierHistogramBuildToReduce(VkCommandBuffer cmd) {
    Barriers::BarrierBatch(cmd)
        .setStages(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .bufferBarrier(histogramBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       0, HISTOGRAM_BINS * sizeof(uint32_t))
        .submit();
}

void PostProcessSystem::barrierHistogramReduceComplete(VkCommandBuffer cmd, uint32_t frameIndex) {
    Barriers::BarrierBatch(cmd)
        .setStages(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .bufferBarrier(exposureBuffers.buffers[frameIndex], VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_HOST_READ_BIT, 0, sizeof(ExposureData))
        .imageTransition(hdrColorImage,
                         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT)
        .submit();
}

void PostProcessSystem::setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler) {
    froxelVolumeView = volumeView;
    froxelSampler = volumeSampler;

    // Update descriptor sets with the froxel volume
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, compositeDescriptorSets[i])
            .writeImage(3, volumeView, volumeSampler)
            .update();
    }
}

void PostProcessSystem::setBloomTexture(VkImageView bloomView, VkSampler bloomSampler) {
    this->bloomView = bloomView;
    this->bloomSampler = bloomSampler;

    // Update descriptor sets with the bloom texture
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, compositeDescriptorSets[i])
            .writeImage(4, bloomView, bloomSampler)
            .update();
    }
}

void PostProcessSystem::setGodRayQuality(GodRayQuality quality) {
    godRayQuality = quality;
    const char* qualityNames[] = {"Low (16 samples)", "Medium (32 samples)", "High (64 samples)"};
    SDL_Log("God ray quality set to: %s", qualityNames[static_cast<int>(quality)]);
}
