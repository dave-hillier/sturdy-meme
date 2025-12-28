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
    ctx_ = info.vulkanContext;
    shaderPath_ = info.shaderPath;

    if (!ctx_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: VulkanContext is null");
        return false;
    }

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
    VkDevice device = ctx_->getDevice();
    VkFormat swapchainFormat = ctx_->getSwapchainImageFormat();

    // Single color attachment, no depth
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    return ManagedRenderPass::create(device, renderPassInfo, renderPass_);
}

bool LoadingRenderer::createFramebuffers() {
    VkDevice device = ctx_->getDevice();
    const auto& imageViews = ctx_->getSwapchainImageViews();
    VkExtent2D extent = ctx_->getSwapchainExtent();

    framebuffers_.clear();
    framebuffers_.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_.get();
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (!ManagedFramebuffer::create(device, framebufferInfo, framebuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to create framebuffer %zu", i);
            return false;
        }
    }

    return true;
}

bool LoadingRenderer::createPipeline() {
    VkDevice device = ctx_->getDevice();
    VkExtent2D extent = ctx_->getSwapchainExtent();

    // Load shaders
    std::string vertPath = shaderPath_ + "/loading.vert.spv";
    std::string fragPath = shaderPath_ + "/loading.frag.spv";

    auto vertModule = ShaderLoader::loadShaderModule(device, vertPath);
    auto fragModule = ShaderLoader::loadShaderModule(device, fragPath);

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to load shaders");
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = *vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = *fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    // No vertex input (positions are in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for simple loading quad
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling (disabled)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LoadingPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, pipelineLayout_)) {
        vkDestroyShaderModule(device, *vertModule, nullptr);
        vkDestroyShaderModule(device, *fragModule, nullptr);
        return false;
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_.get();
    pipelineInfo.subpass = 0;

    bool success = ManagedPipeline::createGraphics(device, ctx_->getPipelineCache(), pipelineInfo, pipeline_);

    // Clean up shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(device, *vertModule, nullptr);
    vkDestroyShaderModule(device, *fragModule, nullptr);

    return success;
}

bool LoadingRenderer::createCommandPool() {
    VkDevice device = ctx_->getDevice();
    uint32_t queueFamily = ctx_->getGraphicsQueueFamily();

    if (!ManagedCommandPool::create(device, queueFamily,
                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                    commandPool_)) {
        return false;
    }

    // Allocate command buffers (one per swapchain image)
    uint32_t imageCount = ctx_->getSwapchainImageCount();
    commandBuffers_.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_.get();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool LoadingRenderer::createSyncObjects() {
    VkDevice device = ctx_->getDevice();

    if (!ManagedSemaphore::create(device, imageAvailableSemaphore_)) return false;
    if (!ManagedSemaphore::create(device, renderFinishedSemaphore_)) return false;
    if (!ManagedFence::createSignaled(device, inFlightFence_)) return false;

    return true;
}

bool LoadingRenderer::render() {
    if (!initialized_) return false;

    VkDevice device = ctx_->getDevice();
    VkExtent2D extent = ctx_->getSwapchainExtent();

    // Skip if window is minimized
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    // Wait for previous frame
    inFlightFence_.wait();
    inFlightFence_.resetFence();

    // Acquire swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, ctx_->getSwapchain(),
                                            UINT64_MAX, imageAvailableSemaphore_.get(),
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
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_.get();
    renderPassInfo.framebuffer = framebuffers_[imageIndex].get();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    // Dark background
    VkClearValue clearColor = {{{0.02f, 0.02f, 0.05f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
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

    // Bind pipeline and draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

    // Push constants
    LoadingPushConstants pushConstants{};
    pushConstants.time = elapsedTime;
    pushConstants.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    pushConstants.progress = progress_;
    pushConstants._pad = 0.0f;

    vkCmdPushConstants(cmd, pipelineLayout_.get(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(LoadingPushConstants), &pushConstants);

    // Draw quad (6 vertices, 2 triangles)
    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to end command buffer");
        return false;
    }

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore_.get()};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore_.get()};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(ctx_->getGraphicsQueue(), 1, &submitInfo, inFlightFence_.get()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadingRenderer: Failed to submit draw command");
        return false;
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {ctx_->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(ctx_->getPresentQueue(), &presentInfo);

    return true;
}

void LoadingRenderer::cleanup() {
    if (!initialized_) return;

    VkDevice device = ctx_->getDevice();

    // Wait for GPU to finish
    vkDeviceWaitIdle(device);

    // Free command buffers before destroying pool
    if (!commandBuffers_.empty() && commandPool_.get() != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, commandPool_.get(),
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    // RAII handles cleanup of other resources via destructors
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
