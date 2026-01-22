#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "core/InitInfoBuilder.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "SamplerFactory.h"
#include "VmaBuffer.h"
#include "CommandBufferUtils.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <array>
#include <vulkan/vulkan.hpp>

std::unique_ptr<PostProcessSystem> PostProcessSystem::create(const InitInfo& info) {
    auto system = std::make_unique<PostProcessSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<PostProcessSystem> PostProcessSystem::create(const InitContext& ctx, VkRenderPass outputRenderPass, VkFormat swapchainFormat) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    info.outputRenderPass = outputRenderPass;
    info.swapchainFormat = swapchainFormat;
    return create(info);
}

std::optional<PostProcessSystem::Bundle> PostProcessSystem::createWithDependencies(
    const InitContext& ctx,
    VkRenderPass finalRenderPass,
    VkFormat swapchainImageFormat
) {
    // Create post-process system
    auto postProcessSystem = create(ctx, finalRenderPass, swapchainImageFormat);
    if (!postProcessSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize PostProcessSystem");
        return std::nullopt;
    }

    // Create bloom system
    auto bloomSystem = BloomSystem::create(ctx);
    if (!bloomSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize BloomSystem");
        return std::nullopt;
    }

    // Create bilateral grid system (for local tone mapping)
    auto bilateralGridSystem = BilateralGridSystem::create(ctx);
    if (!bilateralGridSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize BilateralGridSystem");
        return std::nullopt;
    }

    // Wire bloom texture to post-process system
    postProcessSystem->setBloomTexture(bloomSystem->getBloomOutput(), bloomSystem->getBloomSampler());

    // Wire bilateral grid to post-process system
    postProcessSystem->setBilateralGrid(bilateralGridSystem->getGridView(), bilateralGridSystem->getGridSampler());

    return Bundle{
        std::move(postProcessSystem),
        std::move(bloomSystem),
        std::move(bilateralGridSystem)
    };
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
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PostProcessSystem requires raiiDevice");
        return false;
    }

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

    vk::Device vkDevice(device);
    for (auto& pipeline : compositePipelines) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDevice.destroyPipeline(pipeline);
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (compositePipelineLayout != VK_NULL_HANDLE) {
        vkDevice.destroyPipelineLayout(compositePipelineLayout);
        compositePipelineLayout = VK_NULL_HANDLE;
    }
    if (compositeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDevice.destroyDescriptorSetLayout(compositeDescriptorSetLayout);
        compositeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    hdrSampler_.reset();
    if (hdrRenderPass != VK_NULL_HANDLE) {
        vkDevice.destroyRenderPass(hdrRenderPass);
        hdrRenderPass = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::destroyHDRResources() {
    vk::Device vkDevice(device);
    if (hdrFramebuffer != VK_NULL_HANDLE) {
        vkDevice.destroyFramebuffer(hdrFramebuffer);
        hdrFramebuffer = VK_NULL_HANDLE;
    }
    if (hdrColorView != VK_NULL_HANDLE) {
        vkDevice.destroyImageView(hdrColorView);
        hdrColorView = VK_NULL_HANDLE;
    }
    if (hdrColorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hdrColorImage, hdrColorAllocation);
        hdrColorImage = VK_NULL_HANDLE;
        hdrColorAllocation = VK_NULL_HANDLE;
    }
    if (hdrDepthView != VK_NULL_HANDLE) {
        vkDevice.destroyImageView(hdrDepthView);
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
            .writeImage(0, hdrColorView, **hdrSampler_)
            .update();
    }
}

bool PostProcessSystem::createHDRRenderTarget() {
    // Create HDR color image
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(extent.width, extent.height)
                .setFormat(HDR_FORMAT)
                .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)
                .build(device, image, hdrColorView)) {
            SDL_Log("Failed to create HDR color image");
            return false;
        }
        image.releaseToRaw(hdrColorImage, hdrColorAllocation);
    }

    // Create HDR depth image
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(extent.width, extent.height)
                .setFormat(DEPTH_FORMAT)
                .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .build(device, image, hdrDepthView, VK_IMAGE_ASPECT_DEPTH_BIT)) {
            SDL_Log("Failed to create HDR depth image");
            return false;
        }
        image.releaseToRaw(hdrDepthImage, hdrDepthAllocation);
    }

    return true;
}

bool PostProcessSystem::createHDRRenderPass() {
    auto colorAttachment = vk::AttachmentDescription{}
        .setFormat(static_cast<vk::Format>(HDR_FORMAT))
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

    auto depthAttachment = vk::AttachmentDescription{}
        .setFormat(static_cast<vk::Format>(DEPTH_FORMAT))
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)  // Store for sampling in post-process
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);  // For sampling

    auto colorAttachmentRef = vk::AttachmentReference{}
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto depthAttachmentRef = vk::AttachmentReference{}
        .setAttachment(1)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorAttachmentRef)
        .setPDepthStencilAttachment(&depthAttachmentRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput |
                         vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput |
                         vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite |
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachments(attachments)
        .setSubpasses(subpass)
        .setDependencies(dependency);

    auto result = vk::Device(device).createRenderPass(renderPassInfo);
    if (!result) {
        SDL_Log("Failed to create HDR render pass");
        return false;
    }
    hdrRenderPass = result;

    return true;
}

bool PostProcessSystem::createHDRFramebuffer() {
    std::array<vk::ImageView, 2> attachments = {hdrColorView, hdrDepthView};

    auto framebufferInfo = vk::FramebufferCreateInfo{}
        .setRenderPass(hdrRenderPass)
        .setAttachments(attachments)
        .setWidth(extent.width)
        .setHeight(extent.height)
        .setLayers(1);

    auto result = vk::Device(device).createFramebuffer(framebufferInfo);
    if (!result) {
        SDL_Log("Failed to create HDR framebuffer");
        return false;
    }
    hdrFramebuffer = result;

    return true;
}

bool PostProcessSystem::createSampler() {
    hdrSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!hdrSampler_) {
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
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: god rays (quarter-res)
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
            .writeImage(0, hdrColorView, **hdrSampler_)
            .writeBuffer(1, uniformBuffers.buffers[i], 0, sizeof(PostProcessUniforms))
            .writeImage(2, hdrDepthView, **hdrSampler_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        // Write placeholder for optional textures (use HDR color as fallback)
        // These will be replaced when the actual systems are connected
        if (froxelVolumeView != VK_NULL_HANDLE && froxelSampler != VK_NULL_HANDLE) {
            writer.writeImage(3, froxelVolumeView, froxelSampler);
        } else {
            writer.writeImage(3, hdrColorView, **hdrSampler_);  // Placeholder for froxel
        }
        if (bloomView != VK_NULL_HANDLE && bloomSampler != VK_NULL_HANDLE) {
            writer.writeImage(4, bloomView, bloomSampler);
        } else {
            writer.writeImage(4, hdrColorView, **hdrSampler_);  // Placeholder for bloom
        }
        // Note: bilateral grid (binding 5) is sampler3D - must be set by setBilateralGrid()
        // with a valid 3D texture before use. Skip placeholder as 2D/3D mismatch causes errors.
        if (bilateralGridView != VK_NULL_HANDLE && bilateralGridSampler != VK_NULL_HANDLE) {
            writer.writeImage(5, bilateralGridView, bilateralGridSampler);
        }
        // God rays (binding 6) is sampler2D - use HDR color as safe placeholder
        if (godRaysView_ != VK_NULL_HANDLE && godRaysSampler_ != VK_NULL_HANDLE) {
            writer.writeImage(6, godRaysView_, godRaysSampler_);
        } else {
            writer.writeImage(6, hdrColorView, **hdrSampler_);  // Placeholder for god rays (black = no rays)
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
        vk::Device vkDevice(device);
        if (vertShaderModule) vkDevice.destroyShaderModule(*vertShaderModule);
        if (fragShaderModule) vkDevice.destroyShaderModule(*fragShaderModule);
        return false;
    }

    auto vertShaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertShaderModule)
        .setPName("main");

    // Specialization constant for god ray sample count
    // constant_id = 0 maps to GOD_RAY_SAMPLES in shader
    auto specMapEntry = vk::SpecializationMapEntry{}
        .setConstantID(0)
        .setOffset(0)
        .setSize(sizeof(int32_t));

    // Sample counts for each quality level: Low=16, Medium=32, High=64
    const int32_t sampleCounts[3] = {16, 32, 64};

    auto fragShaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragShaderModule)
        .setPName("main");

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    // No vertex input (fullscreen triangle generated in shader)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(false)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(false);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(false)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(false)
        .setDepthWriteEnable(false)
        .setDepthCompareOp(vk::CompareOp::eLess)
        .setDepthBoundsTestEnable(false)
        .setStencilTestEnable(false);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setBlendEnable(false);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(false)
        .setAttachments(colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    compositePipelineLayout = DescriptorManager::createPipelineLayout(device, compositeDescriptorSetLayout);
    if (compositePipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create composite pipeline layout");
        vk::Device vkDevice(device);
        vkDevice.destroyShaderModule(*vertShaderModule);
        vkDevice.destroyShaderModule(*fragShaderModule);
        return false;
    }

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(compositePipelineLayout)
        .setRenderPass(outputRenderPass)
        .setSubpass(0);

    vk::Device vkDevice(device);

    // Create pipeline variants for each god ray quality level
    for (int i = 0; i < 3; i++) {
        auto specInfo = vk::SpecializationInfo{}
            .setMapEntries(specMapEntry)
            .setDataSize(sizeof(int32_t))
            .setPData(&sampleCounts[i]);

        // Update fragment shader stage with specialization info
        shaderStages[1].setPSpecializationInfo(&specInfo);
        pipelineInfo.setStages(shaderStages);

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create composite graphics pipeline variant %d", i);
            vkDevice.destroyShaderModule(*vertShaderModule);
            vkDevice.destroyShaderModule(*fragShaderModule);
            return false;
        }
        compositePipelines[i] = result.value;
        SDL_Log("Created post-process pipeline variant %d (god ray samples: %d)", i, sampleCounts[i]);
    }

    vkDevice.destroyShaderModule(*vertShaderModule);
    vkDevice.destroyShaderModule(*fragShaderModule);

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
    // Froxel debug visualization mode
    ubo->froxelDebugMode = static_cast<float>(froxelDebugMode);

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
        vk::CommandBuffer vkCmd(cmd);
        VkPipeline selectedPipeline = compositePipelines[static_cast<int>(godRayQuality)];
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, selectedPipeline);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, compositePipelineLayout,
                                 0, vk::DescriptorSet(compositeDescriptorSets[frameIndex]), {});

        vk::Viewport viewport{
            0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f
        };
        vkCmd.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, {extent.width, extent.height}};
        vkCmd.setScissor(0, scissor);

        // Draw fullscreen triangle
        vkCmd.draw(3, 1, 0, 0);

        // Call pre-end callback (e.g., for GUI rendering)
        if (preEndCallback) {
            preEndCallback(cmd);
        }
    } // vkCmdEndRenderPass called automatically
}

bool PostProcessSystem::createHistogramResources() {
    // Create histogram buffer (256 uint values)
    auto histogramBufferInfo = vk::BufferCreateInfo{}
        .setSize(HISTOGRAM_BINS * sizeof(uint32_t))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo histogramAllocInfo{};
    histogramAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedBuffer::create(allocator, *reinterpret_cast<const VkBufferCreateInfo*>(&histogramBufferInfo),
            histogramAllocInfo, histogramBuffer)) {
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

        ComputePipelineBuilder builder(device);
        if (!builder.setShader(shaderPath + "/histogram_build.comp.spv")
                    .setPipelineLayout(histogramBuildPipelineLayout)
                    .buildRaw(histogramBuildPipeline)) {
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

        ComputePipelineBuilder builder(device);
        if (!builder.setShader(shaderPath + "/histogram_reduce.comp.spv")
                    .setPipelineLayout(histogramReducePipelineLayout)
                    .buildRaw(histogramReducePipeline)) {
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

    vk::Device vkDevice(device);
    if (histogramBuildPipeline != VK_NULL_HANDLE) {
        vkDevice.destroyPipeline(histogramBuildPipeline);
        histogramBuildPipeline = VK_NULL_HANDLE;
    }
    if (histogramReducePipeline != VK_NULL_HANDLE) {
        vkDevice.destroyPipeline(histogramReducePipeline);
        histogramReducePipeline = VK_NULL_HANDLE;
    }

    if (histogramBuildPipelineLayout != VK_NULL_HANDLE) {
        vkDevice.destroyPipelineLayout(histogramBuildPipelineLayout);
        histogramBuildPipelineLayout = VK_NULL_HANDLE;
    }
    if (histogramReducePipelineLayout != VK_NULL_HANDLE) {
        vkDevice.destroyPipelineLayout(histogramReducePipelineLayout);
        histogramReducePipelineLayout = VK_NULL_HANDLE;
    }

    if (histogramBuildDescLayout != VK_NULL_HANDLE) {
        vkDevice.destroyDescriptorSetLayout(histogramBuildDescLayout);
        histogramBuildDescLayout = VK_NULL_HANDLE;
    }
    if (histogramReduceDescLayout != VK_NULL_HANDLE) {
        vkDevice.destroyDescriptorSetLayout(histogramReduceDescLayout);
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

    vk::CommandBuffer vkCmd(cmd);

    // Transition HDR image to general layout for compute access
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(hdrColorImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);
    }

    // Clear histogram buffer
    vkCmd.fillBuffer(histogramBuffer.get(), 0, HISTOGRAM_BINS * sizeof(uint32_t), 0);

    // Barrier after fillBuffer
    BarrierHelpers::fillBufferToCompute(vkCmd);

    // Dispatch histogram build
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, histogramBuildPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, histogramBuildPipelineLayout,
                             0, vk::DescriptorSet(histogramBuildDescSets[frameIndex]), {});

    uint32_t groupsX = (extent.width + 15) / 16;
    uint32_t groupsY = (extent.height + 15) / 16;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Barrier: histogram build -> reduce
    BarrierHelpers::bufferComputeToCompute(vkCmd, histogramBuffer.get());

    // Dispatch histogram reduce (single workgroup of 256 threads)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, histogramReducePipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, histogramReducePipelineLayout,
                             0, vk::DescriptorSet(histogramReduceDescSets[frameIndex]), {});
    vkCmd.dispatch(1, 1, 1);

    // Barrier: histogram reduce complete
    {
        std::array<vk::BufferMemoryBarrier, 1> bufBarriers;
        bufBarriers[0] = vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eHostRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(exposureBuffers.buffers[frameIndex])
            .setOffset(0)
            .setSize(sizeof(ExposureData));

        std::array<vk::ImageMemoryBarrier, 1> imgBarriers;
        imgBarriers[0] = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(hdrColorImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eHost | vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, bufBarriers, imgBarriers);
    }
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

void PostProcessSystem::setGodRaysTexture(VkImageView godRaysView, VkSampler godRaysSampler) {
    this->godRaysView_ = godRaysView;
    this->godRaysSampler_ = godRaysSampler;

    // Update descriptor sets with the quarter-res god rays texture
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, compositeDescriptorSets[i])
            .writeImage(6, godRaysView, godRaysSampler)
            .update();
    }
}

void PostProcessSystem::setGodRayQuality(GodRayQuality quality) {
    godRayQuality = quality;
    const char* qualityNames[] = {"Low (16 samples)", "Medium (32 samples)", "High (64 samples)"};
    SDL_Log("God ray quality set to: %s", qualityNames[static_cast<int>(quality)]);
}
