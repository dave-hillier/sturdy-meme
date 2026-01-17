#include "LoadingRenderer.h"
#include "vulkan/VulkanContext.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <chrono>

std::unique_ptr<LoadingRenderer> LoadingRenderer::create(const InitInfo& info) {
    std::unique_ptr<LoadingRenderer> instance(new LoadingRenderer());
    if (!instance->init(info)) {
        return nullptr;
    }
    return instance;
}

LoadingRenderer::~LoadingRenderer() {
    cleanup();
}

bool LoadingRenderer::init(const InitInfo& info) {
    if (!info.vulkanContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: VulkanContext is null");
        return false;
    }
    ctx_.emplace(*info.vulkanContext);
    shaderPath_ = info.shaderPath;

    if (!createRenderPass()) return false;
    if (!createFramebuffers()) return false;
    if (!createPipeline()) return false;
    if (!createCommandPool()) return false;
    if (!createSyncObjects()) return false;

    // Record start time for animation
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    startTime_ = std::chrono::duration<float>(duration).count();

    initialized_ = true;
    SDL_Log("LoadingRenderer initialized");
    return true;
}

bool LoadingRenderer::createRenderPass() {
    const auto& device = ctx_->get().getRaiiDevice();
    vk::Format swapchainFormat = static_cast<vk::Format>(ctx_->get().getVkSwapchainImageFormat());

    // Single color attachment, no depth
    auto colorAttachment = vk::AttachmentDescription{}
        .setFormat(swapchainFormat)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    auto colorAttachmentRef = vk::AttachmentReference{}
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorAttachmentRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setSrcAccessMask({})
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachments(colorAttachment)
        .setSubpasses(subpass)
        .setDependencies(dependency);

    try {
        renderPass_.emplace(device, renderPassInfo);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create render pass: %s", e.what());
        return false;
    }
}

bool LoadingRenderer::createFramebuffers() {
    const auto& device = ctx_->get().getRaiiDevice();
    const auto& imageViews = ctx_->get().getSwapchainImageViews();
    VkExtent2D extent = ctx_->get().getVkSwapchainExtent();

    framebuffers_.clear();
    framebuffers_.reserve(imageViews.size());

    try {
        for (size_t i = 0; i < imageViews.size(); i++) {
            vk::ImageView vkImageView(imageViews[i]);
            auto framebufferInfo = vk::FramebufferCreateInfo{}
                .setRenderPass(**renderPass_)
                .setAttachments(vkImageView)
                .setWidth(extent.width)
                .setHeight(extent.height)
                .setLayers(1);

            framebuffers_.emplace_back(device, framebufferInfo);
        }
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create framebuffer: %s", e.what());
        return false;
    }
}

bool LoadingRenderer::createPipeline() {
    const auto& device = ctx_->get().getRaiiDevice();
    VkDevice rawDevice = ctx_->get().getVkDevice();

    // Load shaders
    std::string vertPath = shaderPath_ + "/loading.vert.spv";
    std::string fragPath = shaderPath_ + "/loading.frag.spv";

    auto vertModule = ShaderLoader::loadShaderModule(rawDevice, vertPath);
    auto fragModule = ShaderLoader::loadShaderModule(rawDevice, fragPath);

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to load shaders");
        return false;
    }

    // ShaderLoader returns raw VkShaderModule, clean up when done
    auto cleanupShaders = [&]() {
        vkDestroyShaderModule(rawDevice, *vertModule, nullptr);
        vkDestroyShaderModule(rawDevice, *fragModule, nullptr);
    };

    // Shader stages
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vertModule)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*fragModule)
            .setPName("main")
    };

    // No vertex input (positions are in shader)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    // Input assembly
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

    // Viewport and scissor (dynamic)
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    // Rasterizer
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(false)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(false);

    // Multisampling (disabled)
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(false)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // Color blending
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setBlendEnable(false);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(false)
        .setAttachments(colorBlendAttachment);

    // Dynamic state
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(LoadingPushConstants));

    // Pipeline layout
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setPushConstantRanges(pushConstantRange);

    try {
        pipelineLayout_.emplace(device, pipelineLayoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create pipeline layout: %s", e.what());
        cleanupShaders();
        return false;
    }

    // Create graphics pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**pipelineLayout_)
        .setRenderPass(**renderPass_)
        .setSubpass(0);

    try {
        // Use nullptr for pipeline cache (loading screen doesn't benefit from caching)
        auto result = device.createGraphicsPipelines(nullptr, pipelineInfo);
        pipeline_.emplace(std::move(result.front()));
        cleanupShaders();
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create pipeline: %s", e.what());
        cleanupShaders();
        return false;
    }
}

bool LoadingRenderer::createCommandPool() {
    const auto& device = ctx_->get().getRaiiDevice();
    uint32_t queueFamily = ctx_->get().getGraphicsQueueFamily();

    auto poolInfo = vk::CommandPoolCreateInfo{}
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
        .setQueueFamilyIndex(queueFamily);

    try {
        commandPool_.emplace(device, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create command pool: %s", e.what());
        return false;
    }

    // Allocate command buffers (one per swapchain image)
    uint32_t imageCount = ctx_->get().getSwapchainImageCount();
    commandBuffers_.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = **commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    if (vkAllocateCommandBuffers(ctx_->get().getVkDevice(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool LoadingRenderer::createSyncObjects() {
    const auto& device = ctx_->get().getRaiiDevice();

    try {
        imageAvailableSemaphore_.emplace(device, vk::SemaphoreCreateInfo{});
        renderFinishedSemaphore_.emplace(device, vk::SemaphoreCreateInfo{});

        auto fenceInfo = vk::FenceCreateInfo{}
            .setFlags(vk::FenceCreateFlagBits::eSignaled);
        inFlightFence_.emplace(device, fenceInfo);

        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create sync objects: %s", e.what());
        return false;
    }
}

bool LoadingRenderer::render() {
    if (!initialized_) return false;

    const auto& device = ctx_->get().getRaiiDevice();
    VkDevice rawDevice = ctx_->get().getVkDevice();
    VkExtent2D extent = ctx_->get().getVkSwapchainExtent();

    // Skip if window is minimized
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    // Wait for previous frame
    auto waitResult = device.waitForFences(**inFlightFence_, vk::True, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to wait for fence");
        return false;
    }
    device.resetFences(**inFlightFence_);

    // Acquire swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(rawDevice, ctx_->get().getVkSwapchain(),
                                            UINT64_MAX, **imageAvailableSemaphore_,
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Need to recreate swapchain - skip this frame
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to acquire swapchain image");
        return false;
    }

    // Calculate time for animation
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    float currentTime = std::chrono::duration<float>(duration).count();
    float elapsedTime = currentTime - startTime_;

    // Reset and record command buffer
    VkCommandBuffer cmd = commandBuffers_[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to begin command buffer");
        return false;
    }

    // Begin render pass
    vk::ClearValue clearColor(vk::ClearColorValue(std::array<float, 4>{0.02f, 0.02f, 0.05f, 1.0f}));
    auto renderPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(**renderPass_)
        .setFramebuffer(*framebuffers_[imageIndex])
        .setRenderArea(vk::Rect2D{{0, 0}, vk::Extent2D{extent.width, extent.height}})
        .setClearValues(clearColor);

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set viewport and scissor
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{{0, 0}, vk::Extent2D{extent.width, extent.height}};
    vkCmd.setScissor(0, scissor);

    // Bind pipeline and draw
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);

    // Push constants
    LoadingPushConstants pushConstants{};
    pushConstants.time = elapsedTime;
    pushConstants.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    pushConstants.progress = progress_;
    pushConstants._pad = 0.0f;

    vkCmd.pushConstants<LoadingPushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pushConstants);

    // Draw quad (6 vertices, 2 triangles)
    vkCmd.draw(6, 1, 0, 0);

    vkCmd.endRenderPass();

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to end command buffer");
        return false;
    }

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {**imageAvailableSemaphore_};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {**renderFinishedSemaphore_};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(ctx_->get().getVkGraphicsQueue(), 1, &submitInfo, **inFlightFence_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to submit draw command");
        return false;
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {ctx_->get().getVkSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(ctx_->get().getVkPresentQueue(), &presentInfo);

    return true;
}

void LoadingRenderer::cleanup() {
    if (!initialized_) return;

    VkDevice device = ctx_->get().getVkDevice();

    // Wait for GPU to finish
    vkDeviceWaitIdle(device);

    // Free command buffers before destroying pool
    if (!commandBuffers_.empty() && commandPool_) {
        vkFreeCommandBuffers(device, **commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    // RAII handles cleanup of other resources via destructors (reset optional values)
    inFlightFence_.reset();
    renderFinishedSemaphore_.reset();
    imageAvailableSemaphore_.reset();
    commandPool_.reset();
    pipeline_.reset();
    pipelineLayout_.reset();
    framebuffers_.clear();
    renderPass_.reset();

    initialized_ = false;
    SDL_Log("LoadingRenderer cleaned up");
}
