#include "PostProcessSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <array>

using namespace vk;  // Vulkan-Hpp type-safe wrappers

std::unique_ptr<PostProcessSystem> PostProcessSystem::create(const InitInfo& info) {
    std::unique_ptr<PostProcessSystem> system(new PostProcessSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<PostProcessSystem> PostProcessSystem::create(const InitContext& ctx, VkRenderPass outputRenderPass, VkFormat swapchainFormat) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.outputRenderPass = outputRenderPass;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.swapchainFormat = swapchainFormat;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    return create(info);
}

PostProcessSystem::~PostProcessSystem() {
    cleanup();
}

bool PostProcessSystem::initInternal(const InitInfo& info) {
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

void PostProcessSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;  // Not initialized

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

    hdrSampler.reset();
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

void PostProcessSystem::resize(VkExtent2D newExtent) {
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
    ImageCreateInfo colorImageInfo{
        {},                                  // flags
        ImageType::e2D,
        HDR_FORMAT,
        Extent3D{extent.width, extent.height, 1},
        1, 1,                                // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eColorAttachment | ImageUsageFlagBits::eSampled | ImageUsageFlagBits::eStorage,
        SharingMode::eExclusive,
        0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo colorAllocInfo{};
    colorAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkColorImageInfo = static_cast<VkImageCreateInfo>(colorImageInfo);
    if (vmaCreateImage(allocator, &vkColorImageInfo, &colorAllocInfo,
                       &hdrColorImage, &hdrColorAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image");
        return false;
    }

    ImageViewCreateInfo colorViewInfo{
        {},                              // flags
        hdrColorImage,
        ImageViewType::e2D,
        HDR_FORMAT,
        ComponentMapping{},              // identity swizzle
        ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    };

    auto vkColorViewInfo = static_cast<VkImageViewCreateInfo>(colorViewInfo);
    if (vkCreateImageView(device, &vkColorViewInfo, nullptr, &hdrColorView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image view");
        return false;
    }

    // Create HDR depth image
    ImageCreateInfo depthImageInfo{
        {},                                  // flags
        ImageType::e2D,
        DEPTH_FORMAT,
        Extent3D{extent.width, extent.height, 1},
        1, 1,                                // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eDepthStencilAttachment | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkDepthImageInfo = static_cast<VkImageCreateInfo>(depthImageInfo);
    if (vmaCreateImage(allocator, &vkDepthImageInfo, &depthAllocInfo,
                       &hdrDepthImage, &hdrDepthAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image");
        return false;
    }

    ImageViewCreateInfo depthViewInfo{
        {},                              // flags
        hdrDepthImage,
        ImageViewType::e2D,
        DEPTH_FORMAT,
        ComponentMapping{},              // identity swizzle
        ImageSubresourceRange{ImageAspectFlagBits::eDepth, 0, 1, 0, 1}
    };

    auto vkDepthViewInfo = static_cast<VkImageViewCreateInfo>(depthViewInfo);
    if (vkCreateImageView(device, &vkDepthViewInfo, nullptr, &hdrDepthView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image view");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRRenderPass() {
    AttachmentDescription colorAttachment{
        {},                                  // flags
        HDR_FORMAT,
        SampleCountFlagBits::e1,
        AttachmentLoadOp::eClear,
        AttachmentStoreOp::eStore,
        AttachmentLoadOp::eDontCare,         // stencilLoadOp
        AttachmentStoreOp::eDontCare,        // stencilStoreOp
        ImageLayout::eUndefined,
        ImageLayout::eShaderReadOnlyOptimal
    };

    AttachmentDescription depthAttachment{
        {},                                  // flags
        DEPTH_FORMAT,
        SampleCountFlagBits::e1,
        AttachmentLoadOp::eClear,
        AttachmentStoreOp::eStore,           // Store for sampling in post-process
        AttachmentLoadOp::eDontCare,         // stencilLoadOp
        AttachmentStoreOp::eDontCare,        // stencilStoreOp
        ImageLayout::eUndefined,
        ImageLayout::eDepthStencilReadOnlyOptimal  // For sampling
    };

    AttachmentReference colorAttachmentRef{0, ImageLayout::eColorAttachmentOptimal};
    AttachmentReference depthAttachmentRef{1, ImageLayout::eDepthStencilAttachmentOptimal};

    SubpassDescription subpass{
        {},                                  // flags
        PipelineBindPoint::eGraphics,
        0, nullptr,                          // inputAttachmentCount, pInputAttachments
        1, &colorAttachmentRef,              // colorAttachmentCount, pColorAttachments
        nullptr,                             // pResolveAttachments
        &depthAttachmentRef                  // pDepthStencilAttachment
    };

    SubpassDependency dependency{
        VK_SUBPASS_EXTERNAL, 0,              // srcSubpass, dstSubpass
        PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eEarlyFragmentTests,
        PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eEarlyFragmentTests,
        {},                                  // srcAccessMask
        AccessFlagBits::eColorAttachmentWrite | AccessFlagBits::eDepthStencilAttachmentWrite
    };

    std::array<AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    RenderPassCreateInfo renderPassInfo{
        {},                                  // flags
        attachments,
        subpass,
        dependency
    };

    auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
    if (vkCreateRenderPass(device, &vkRenderPassInfo, nullptr, &hdrRenderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR render pass");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRFramebuffer() {
    std::array<VkImageView, 2> attachments = {hdrColorView, hdrDepthView};

    FramebufferCreateInfo framebufferInfo{
        {},                                  // flags
        hdrRenderPass,
        attachments,
        extent.width,
        extent.height,
        1                                    // layers
    };

    auto vkFramebufferInfo = static_cast<VkFramebufferCreateInfo>(framebufferInfo);
    if (vkCreateFramebuffer(device, &vkFramebufferInfo, nullptr, &hdrFramebuffer) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR framebuffer");
        return false;
    }

    return true;
}

bool PostProcessSystem::createSampler() {
    if (!VulkanResourceFactory::createSamplerLinearClamp(device, hdrSampler)) {
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
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: bilateral grid
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

    if (!vertShaderCode || !fragShaderCode) {
        SDL_Log("Failed to read post-process shader files");
        return false;
    }

    auto vertShaderModule = ShaderLoader::createShaderModule(device, *vertShaderCode);
    auto fragShaderModule = ShaderLoader::createShaderModule(device, *fragShaderCode);

    if (!vertShaderModule || !fragShaderModule) {
        SDL_Log("Failed to create post-process shader modules");
        if (vertShaderModule) vkDestroyShaderModule(device, *vertShaderModule, nullptr);
        if (fragShaderModule) vkDestroyShaderModule(device, *fragShaderModule, nullptr);
        return false;
    }

    PipelineShaderStageCreateInfo vertShaderStageInfo{
        {},                              // flags
        ShaderStageFlagBits::eVertex,
        *vertShaderModule,
        "main"
    };

    // Specialization constant for god ray sample count
    // constant_id = 0 maps to GOD_RAY_SAMPLES in shader
    SpecializationMapEntry specMapEntry{0, 0, sizeof(int32_t)};

    // Sample counts for each quality level: Low=16, Medium=32, High=64
    const int32_t sampleCounts[3] = {16, 32, 64};

    PipelineShaderStageCreateInfo fragShaderStageInfo{
        {},                              // flags
        ShaderStageFlagBits::eFragment,
        *fragShaderModule,
        "main"
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        static_cast<VkPipelineShaderStageCreateInfo>(vertShaderStageInfo),
        static_cast<VkPipelineShaderStageCreateInfo>(fragShaderStageInfo)
    };

    // No vertex input (fullscreen triangle generated in shader)
    PipelineVertexInputStateCreateInfo vertexInputInfo{};

    PipelineInputAssemblyStateCreateInfo inputAssembly{
        {},                              // flags
        PrimitiveTopology::eTriangleList,
        VK_FALSE                         // primitiveRestartEnable
    };

    PipelineViewportStateCreateInfo viewportState{
        {},                              // flags
        1, nullptr,                      // viewportCount, pViewports
        1, nullptr                       // scissorCount, pScissors
    };

    PipelineRasterizationStateCreateInfo rasterizer{
        {},                              // flags
        VK_FALSE,                        // depthClampEnable
        VK_FALSE,                        // rasterizerDiscardEnable
        PolygonMode::eFill,
        CullModeFlagBits::eNone,
        FrontFace::eCounterClockwise,
        VK_FALSE, 0.0f, 0.0f, 0.0f,      // depthBias params
        1.0f                             // lineWidth
    };

    PipelineMultisampleStateCreateInfo multisampling{
        {},                              // flags
        SampleCountFlagBits::e1,
        VK_FALSE                         // sampleShadingEnable
    };

    PipelineDepthStencilStateCreateInfo depthStencil{
        {},                              // flags
        VK_FALSE,                        // depthTestEnable
        VK_FALSE,                        // depthWriteEnable
        CompareOp::eLess,
        VK_FALSE,                        // depthBoundsTestEnable
        VK_FALSE                         // stencilTestEnable
    };

    PipelineColorBlendAttachmentState colorBlendAttachment{
        VK_FALSE,                        // blendEnable
        {}, {}, {},                      // srcColorBlendFactor, dstColorBlendFactor, colorBlendOp
        {}, {}, {},                      // srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp
        ColorComponentFlagBits::eR | ColorComponentFlagBits::eG |
        ColorComponentFlagBits::eB | ColorComponentFlagBits::eA
    };

    PipelineColorBlendStateCreateInfo colorBlending{
        {},                              // flags
        VK_FALSE,                        // logicOpEnable
        {},                              // logicOp
        1, &colorBlendAttachment
    };

    std::array<DynamicState, 2> dynamicStates = {DynamicState::eViewport, DynamicState::eScissor};
    PipelineDynamicStateCreateInfo dynamicState{
        {},                              // flags
        dynamicStates
    };

    compositePipelineLayout = DescriptorManager::createPipelineLayout(device, compositeDescriptorSetLayout);
    if (compositePipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create composite pipeline layout");
        vkDestroyShaderModule(device, *vertShaderModule, nullptr);
        vkDestroyShaderModule(device, *fragShaderModule, nullptr);
        return false;
    }

    auto vkVertexInputInfo = static_cast<VkPipelineVertexInputStateCreateInfo>(vertexInputInfo);
    auto vkInputAssembly = static_cast<VkPipelineInputAssemblyStateCreateInfo>(inputAssembly);
    auto vkViewportState = static_cast<VkPipelineViewportStateCreateInfo>(viewportState);
    auto vkRasterizer = static_cast<VkPipelineRasterizationStateCreateInfo>(rasterizer);
    auto vkMultisampling = static_cast<VkPipelineMultisampleStateCreateInfo>(multisampling);
    auto vkDepthStencil = static_cast<VkPipelineDepthStencilStateCreateInfo>(depthStencil);
    auto vkColorBlending = static_cast<VkPipelineColorBlendStateCreateInfo>(colorBlending);
    auto vkDynamicState = static_cast<VkPipelineDynamicStateCreateInfo>(dynamicState);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vkVertexInputInfo;
    pipelineInfo.pInputAssemblyState = &vkInputAssembly;
    pipelineInfo.pViewportState = &vkViewportState;
    pipelineInfo.pRasterizationState = &vkRasterizer;
    pipelineInfo.pMultisampleState = &vkMultisampling;
    pipelineInfo.pDepthStencilState = &vkDepthStencil;
    pipelineInfo.pColorBlendState = &vkColorBlending;
    pipelineInfo.pDynamicState = &vkDynamicState;
    pipelineInfo.layout = compositePipelineLayout;
    pipelineInfo.renderPass = outputRenderPass;
    pipelineInfo.subpass = 0;

    // Create pipeline variants for each god ray quality level
    for (int i = 0; i < 3; i++) {
        SpecializationInfo specInfo{
            1, &specMapEntry,
            sizeof(int32_t),
            &sampleCounts[i]
        };

        // Update fragment shader stage with specialization info
        auto vkSpecInfo = static_cast<VkSpecializationInfo>(specInfo);
        shaderStages[1].pSpecializationInfo = &vkSpecInfo;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipelines[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create composite graphics pipeline variant %d", i);
            vkDestroyShaderModule(device, *vertShaderModule, nullptr);
            vkDestroyShaderModule(device, *fragShaderModule, nullptr);
            return false;
        }
        SDL_Log("Created post-process pipeline variant %d (god ray samples: %d)", i, sampleCounts[i]);
    }

    vkDestroyShaderModule(device, *vertShaderModule, nullptr);
    vkDestroyShaderModule(device, *fragShaderModule, nullptr);

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
    // Local tone mapping (bilateral grid)
    ubo->localToneMapEnabled = localToneMapEnabled ? 1.0f : 0.0f;
    ubo->localToneMapContrast = localToneMapContrast;
    ubo->localToneMapDetail = localToneMapDetail;
    ubo->minLogLuminance = minLogLuminance;
    ubo->maxLogLuminance = maxLogLuminance;
    ubo->bilateralBlend = bilateralBlend;

    // Water Volume Renderer - Underwater effects (Phase 2)
    ubo->underwaterEnabled = isUnderwater_ ? 1.0f : 0.0f;
    ubo->underwaterDepth = underwaterDepth_;
    ubo->underwaterAbsorption = glm::vec4(underwaterAbsorption_, underwaterTurbidity_);
    ubo->underwaterColor = underwaterColor_;
    ubo->underwaterWaterLevel = underwaterWaterLevel_;

    // Store computed exposure for next frame
    lastAutoExposure = autoExposureEnabled ? computedExposure : manualExposure;

    // Begin swapchain render pass for final composite (RAII scope)
    {
        RenderPassScope renderPass = RenderPassScope::begin(cmd)
            .renderPass(outputRenderPass)
            .framebuffer(swapchainFB)
            .renderAreaFullExtent(extent.width, extent.height)
            .clearColor(0.0f, 0.0f, 0.0f, 1.0f)
            .clearDepth(1.0f, 0);

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
    } // vkCmdEndRenderPass called automatically
}

bool PostProcessSystem::createHistogramResources() {
    // Create histogram buffer (256 uint values)
    BufferCreateInfo histogramBufferInfo{
        {},                                  // flags
        HISTOGRAM_BINS * sizeof(uint32_t),
        BufferUsageFlagBits::eStorageBuffer | BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive
    };

    VmaAllocationCreateInfo histogramAllocInfo{};
    histogramAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    auto vkHistogramBufferInfo = static_cast<VkBufferCreateInfo>(histogramBufferInfo);
    if (!ManagedBuffer::create(allocator, vkHistogramBufferInfo, histogramAllocInfo, histogramBuffer)) {
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
        if (!shaderCode) {
            SDL_Log("Failed to load histogram build shader");
            return false;
        }

        auto shaderModule = ShaderLoader::createShaderModule(device, *shaderCode);
        if (!shaderModule) {
            SDL_Log("Failed to create histogram build shader module");
            return false;
        }

        PipelineShaderStageCreateInfo stageInfo{
            {},                              // flags
            ShaderStageFlagBits::eCompute,
            *shaderModule,
            "main"
        };

        ComputePipelineCreateInfo pipelineInfo{
            {},                              // flags
            stageInfo,
            histogramBuildPipelineLayout
        };

        auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &vkPipelineInfo, nullptr, &histogramBuildPipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

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
        if (!shaderCode) {
            SDL_Log("Failed to load histogram reduce shader");
            return false;
        }

        auto shaderModule = ShaderLoader::createShaderModule(device, *shaderCode);
        if (!shaderModule) {
            SDL_Log("Failed to create histogram reduce shader module");
            return false;
        }

        PipelineShaderStageCreateInfo stageInfo{
            {},                              // flags
            ShaderStageFlagBits::eCompute,
            *shaderModule,
            "main"
        };

        ComputePipelineCreateInfo pipelineInfo{
            {},                              // flags
            stageInfo,
            histogramReducePipelineLayout
        };

        auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &vkPipelineInfo, nullptr, &histogramReducePipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

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
            .writeBuffer(1, histogramBuffer.get(), 0, HISTOGRAM_BINS * sizeof(uint32_t),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, histogramParamsBuffers.buffers[i], 0, sizeof(HistogramParams))
            .update();

        // Reduce descriptor set
        DescriptorManager::SetWriter(device, histogramReduceDescSets[i])
            .writeBuffer(0, histogramBuffer.get(), 0, HISTOGRAM_BINS * sizeof(uint32_t),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(1, exposureBuffers.buffers[i], 0, sizeof(ExposureData),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, histogramParamsBuffers.buffers[i], 0, sizeof(HistogramReduceParams))
            .update();
    }

    return true;
}

void PostProcessSystem::destroyHistogramResources() {
    histogramBuffer.reset();

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
    Barriers::clearBufferForComputeReadWrite(cmd, histogramBuffer.get(), 0, HISTOGRAM_BINS * sizeof(uint32_t));

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
        .bufferBarrier(histogramBuffer.get(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
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

void PostProcessSystem::setBilateralGrid(VkImageView gridView, VkSampler gridSampler) {
    this->bilateralGridView = gridView;
    this->bilateralGridSampler = gridSampler;

    // Update descriptor sets with the bilateral grid texture
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, compositeDescriptorSets[i])
            .writeImage(5, gridView, gridSampler)
            .update();
    }
}

void PostProcessSystem::setGodRayQuality(GodRayQuality quality) {
    godRayQuality = quality;
    const char* qualityNames[] = {"Low (16 samples)", "Medium (32 samples)", "High (64 samples)"};
    SDL_Log("God ray quality set to: %s", qualityNames[static_cast<int>(quality)]);
}
