#include "VisibilityBuffer.h"
#include "ShaderLoader.h"
#include "Mesh.h"
#include "MeshClusterBuilder.h"
#include "Texture.h"
#include "MaterialRegistry.h"
#include "vulkan/CommandBufferUtils.h"
#include "shaders/bindings.h"
#include "vulkan/VmaBufferFactory.h"

#include <SDL3/SDL_log.h>
#include <array>
#include <cstring>

// ============================================================================
// Factory methods
// ============================================================================

std::unique_ptr<VisibilityBuffer> VisibilityBuffer::create(const InitInfo& info) {
    auto system = std::make_unique<VisibilityBuffer>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<VisibilityBuffer> VisibilityBuffer::create(const InitContext& ctx, VkFormat depthFormat) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.depthFormat = depthFormat;
    info.raiiDevice = ctx.raiiDevice;
    info.graphicsQueue = ctx.graphicsQueue;
    info.commandPool = ctx.commandPool;
    return create(info);
}

VisibilityBuffer::~VisibilityBuffer() {
    cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool VisibilityBuffer::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    depthFormat_ = info.depthFormat;
    raiiDevice_ = info.raiiDevice;
    graphicsQueue_ = info.graphicsQueue;
    commandPool_ = info.commandPool;

    if (!createRenderTargets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create render targets");
        return false;
    }

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create render pass");
        return false;
    }

    if (!createFramebuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create framebuffer");
        return false;
    }

    if (!createRasterPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster pipeline");
        return false;
    }

    if (!createDebugPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create debug pipeline");
        return false;
    }

    if (!createResolveBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create resolve buffers");
        return false;
    }

    if (!createResolvePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create resolve pipeline");
        return false;
    }

    SDL_Log("VisibilityBuffer: Initialized (%ux%u, %u frames)",
            extent_.width, extent_.height, framesInFlight_);
    return true;
}

void VisibilityBuffer::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    destroyResolveBuffers();
    destroyResolvePipeline();
    destroyDebugPipeline();
    destroyRasterPipeline();
    destroyFramebuffer();
    destroyRenderPass();
    destroyRenderTargets();
    destroyDescriptorSets();
}

// ============================================================================
// Render targets
// ============================================================================

bool VisibilityBuffer::createRenderTargets() {
    // V-buffer: R32G32_UINT — R=instanceId, G=triangleId (64-bit, no packing)
    bool ok = ImageBuilder(allocator_)
        .setExtent(extent_)
        .setFormat(VISBUF_FORMAT)
        .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build(device_, visibilityImage_, visibilityView_);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create visibility image");
        return false;
    }

    // Depth buffer
    ok = ImageBuilder(allocator_)
        .setExtent(extent_)
        .setFormat(depthFormat_)
        .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .build(device_, depthImage_, depthView_, VK_IMAGE_ASPECT_DEPTH_BIT);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create depth image");
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyRenderTargets() {
    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    depthImage_.reset();

    if (visibilityView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, visibilityView_, nullptr);
        visibilityView_ = VK_NULL_HANDLE;
    }
    visibilityImage_.reset();
}

// ============================================================================
// Render pass
// ============================================================================

bool VisibilityBuffer::createRenderPass() {
    // Attachment 0: Visibility buffer (R32_UINT)
    VkAttachmentDescription visAttachment{};
    visAttachment.format = VISBUF_FORMAT;
    visAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    visAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    visAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    visAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    visAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    visAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    visAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 1: Depth
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependencies for proper synchronization
    std::array<VkSubpassDependency, 2> dependencies{};

    // External -> Subpass 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Subpass 0 -> External
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {visAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyRenderPass() {
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Framebuffer
// ============================================================================

bool VisibilityBuffer::createFramebuffer() {
    std::array<VkImageView, 2> attachments = {visibilityView_, depthView_};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = extent_.width;
    fbInfo.height = extent_.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyFramebuffer() {
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Raster pipeline (V-buffer write)
// ============================================================================

bool VisibilityBuffer::createRasterPipeline() {
    // Create descriptor set layout for raster pass
    // Binding 0: Main UBO (from main rendering set)
    // Binding 1: Diffuse texture (for alpha testing)
    std::array<vk::DescriptorSetLayoutBinding, 2> layoutBindings;
    layoutBindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_UBO)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    layoutBindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_DIFFUSE_TEX)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(layoutBindings);

    try {
        rasterDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster desc set layout: %s", e.what());
        return false;
    }

    // Push constants for per-object data
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(VisBufPushConstants);

    vk::DescriptorSetLayout vkRasterLayout(**rasterDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(vkRasterLayout)
        .setPushConstantRanges(pushRange);

    vk::Device vkDevice(device_);
    rasterPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load shaders
    auto vertModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to load raster shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages[0] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertModule)
        .setPName("main");
    shaderStages[1] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragModule)
        .setPName("main");

    // Vertex input - same as standard Vertex
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(reinterpret_cast<const vk::VertexInputBindingDescription*>(&bindingDesc))
        .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attrDescs.size()))
        .setPVertexAttributeDescriptions(reinterpret_cast<const vk::VertexInputAttributeDescription*>(attrDescs.data()));

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    // Dynamic viewport/scissor
    auto viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {extent_.width, extent_.height}};

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    // No blending for R32G32_UINT (integer format) — write R and G channels
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    // Dynamic state for viewport/scissor
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(shaderStages.data())
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(rasterPipelineLayout_)
        .setRenderPass(renderPass_)
        .setSubpass(0);

    auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster pipeline");
        vkDevice.destroyShaderModule(*vertModule);
        vkDevice.destroyShaderModule(*fragModule);
        return false;
    }
    rasterPipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    SDL_Log("VisibilityBuffer: Raster pipeline created");
    return true;
}

void VisibilityBuffer::destroyRasterPipeline() {
    if (rasterPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, rasterPipeline_, nullptr);
        rasterPipeline_ = VK_NULL_HANDLE;
    }
    if (rasterPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, rasterPipelineLayout_, nullptr);
        rasterPipelineLayout_ = VK_NULL_HANDLE;
    }
    rasterDescSetLayout_.reset();
}

// ============================================================================
// Cluster raster pipeline (indirect draws, SSBO-based transforms)
// ============================================================================

bool VisibilityBuffer::createClusterRasterPipeline() {
    if (!raiiDevice_) return false;

    // Descriptor set layout: UBO + instance SSBO + draw info SSBO
    std::array<vk::DescriptorSetLayoutBinding, 3> layoutBindings;
    layoutBindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_CLUSTER_UBO)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    layoutBindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_CLUSTER_INSTANCES)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    layoutBindings[2] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_CLUSTER_DRAW_INFO)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(layoutBindings);
    try {
        clusterRasterDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create cluster raster desc set layout: %s", e.what());
        return false;
    }

    // Pipeline layout (no push constants)
    vk::DescriptorSetLayout vkLayout(**clusterRasterDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(vkLayout);

    vk::Device vkDevice(device_);
    clusterRasterPipelineLayout_ = static_cast<VkPipelineLayout>(
        vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load cluster raster shaders
    auto vertModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_cluster.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_cluster.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to load cluster raster shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages[0] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertModule)
        .setPName("main");
    shaderStages[1] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragModule)
        .setPName("main");

    // Vertex input - same as standard Vertex
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(reinterpret_cast<const vk::VertexInputBindingDescription*>(&bindingDesc))
        .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attrDescs.size()))
        .setPVertexAttributeDescriptions(reinterpret_cast<const vk::VertexInputAttributeDescription*>(attrDescs.data()));

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    // Dynamic viewport/scissor
    auto viewport = vk::Viewport{0.0f, 0.0f,
        static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {extent_.width, extent_.height}};
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1).setPViewports(&viewport)
        .setScissorCount(1).setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(VK_FALSE)
        .setRasterizerDiscardEnable(VK_FALSE)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    auto multisample = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlend = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamicStates);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(shaderStages.data())
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisample)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlend)
        .setPDynamicState(&dynamicState)
        .setLayout(clusterRasterPipelineLayout_)
        .setRenderPass(renderPass_)
        .setSubpass(0);

    auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create cluster raster pipeline");
        return false;
    }
    clusterRasterPipeline_ = static_cast<VkPipeline>(result.value);

    SDL_Log("VisibilityBuffer: Cluster raster pipeline created");
    return true;
}

bool VisibilityBuffer::createClusterRasterDescriptorSets(
    const std::vector<VkBuffer>& uboBuffers, VkDeviceSize uboSize,
    const std::vector<VkBuffer>& instanceBuffers, VkDeviceSize instanceBufferSize,
    VkBuffer drawInfoBuffer, VkDeviceSize drawInfoBufferSize) {

    if (!clusterRasterDescSetLayout_ || uboBuffers.empty()) {
        return false;
    }

    VkDescriptorSetLayout rawLayout = **clusterRasterDescSetLayout_;
    clusterRasterDescSets_ = descriptorPool_->allocate(
        rawLayout, static_cast<uint32_t>(uboBuffers.size()));

    for (uint32_t i = 0; i < uboBuffers.size(); ++i) {
        vk::DescriptorBufferInfo uboInfo{uboBuffers[i], 0, uboSize};
        vk::DescriptorBufferInfo instanceInfo{
            (i < instanceBuffers.size()) ? instanceBuffers[i] : instanceBuffers[0],
            0, instanceBufferSize};
        vk::DescriptorBufferInfo drawInfo{drawInfoBuffer, 0, drawInfoBufferSize};

        std::array<vk::WriteDescriptorSet, 3> writes;
        writes[0] = vk::WriteDescriptorSet{}
            .setDstSet(clusterRasterDescSets_[i])
            .setDstBinding(BINDING_CLUSTER_UBO)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&uboInfo);

        writes[1] = vk::WriteDescriptorSet{}
            .setDstSet(clusterRasterDescSets_[i])
            .setDstBinding(BINDING_CLUSTER_INSTANCES)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&instanceInfo);

        writes[2] = vk::WriteDescriptorSet{}
            .setDstSet(clusterRasterDescSets_[i])
            .setDstBinding(BINDING_CLUSTER_DRAW_INFO)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&drawInfo);

        vk::Device(device_).updateDescriptorSets(writes, {});
    }

    SDL_Log("VisibilityBuffer: Cluster raster descriptor sets created (%zu frames)",
            uboBuffers.size());
    return true;
}

VkDescriptorSet VisibilityBuffer::getClusterRasterDescriptorSet(uint32_t frameIndex) const {
    if (frameIndex < clusterRasterDescSets_.size()) {
        return clusterRasterDescSets_[frameIndex];
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// Debug visualization pipeline
// ============================================================================

bool VisibilityBuffer::createDebugPipeline() {
    if (!raiiDevice_) return false;

    // Create nearest sampler for integer texture sampling
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);

    try {
        nearestSampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create sampler: %s", e.what());
        return false;
    }

    // Descriptor set layout: visibility buffer + depth buffer
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_DEBUG_INPUT)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    bindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_DEBUG_DEPTH_INPUT)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        debugDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create debug desc set layout: %s", e.what());
        return false;
    }

    // Allocate debug descriptor set
    VkDescriptorSetLayout rawDebugLayout = **debugDescSetLayout_;
    auto debugSets = descriptorPool_->allocate(rawDebugLayout, 1);
    if (debugSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to allocate debug descriptor set");
        return false;
    }
    debugDescSet_ = debugSets[0];

    // Update debug descriptor set with V-buffer and depth views
    VkSampler rawSampler = **nearestSampler_;

    VkDescriptorImageInfo visInfo{};
    visInfo.sampler = rawSampler;
    visInfo.imageView = visibilityView_;
    visInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = rawSampler;
    depthInfo.imageView = depthView_;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = debugDescSet_;
    writes[0].dstBinding = BINDING_VISBUF_DEBUG_INPUT;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &visInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = debugDescSet_;
    writes[1].dstBinding = BINDING_VISBUF_DEBUG_DEPTH_INPUT;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &depthInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Push constants
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(VisBufDebugPushConstants);

    vk::DescriptorSetLayout vkDebugLayout(**debugDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(vkDebugLayout)
        .setPushConstantRanges(pushRange);

    vk::Device vkDevice(device_);
    debugPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load debug shaders
    auto vertModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_debug.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_debug.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to load debug shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages[0] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertModule)
        .setPName("main");
    shaderStages[1] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragModule)
        .setPName("main");

    // No vertex input (fullscreen triangle)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {extent_.width, extent_.height}};

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setLineWidth(1.0f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_FALSE)
        .setDepthWriteEnable(VK_FALSE);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Debug pipeline renders to the swapchain render pass, not the V-buffer render pass.
    // We'll need the swapchain render pass to be passed in. For now, skip the
    // graphics pipeline creation and create it lazily when the render pass is known.
    // Store the shader modules for later creation.

    // Actually, the debug visualization will be rendered in the post-process pass's
    // output render pass. We don't have that here, so we'll create the pipeline lazily.
    // For now, just clean up.
    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    SDL_Log("VisibilityBuffer: Debug descriptor set created (pipeline deferred)");
    return true;
}

void VisibilityBuffer::destroyDebugPipeline() {
    nearestSampler_.reset();
    if (debugPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, debugPipeline_, nullptr);
        debugPipeline_ = VK_NULL_HANDLE;
    }
    if (debugPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, debugPipelineLayout_, nullptr);
        debugPipelineLayout_ = VK_NULL_HANDLE;
    }
    debugDescSetLayout_.reset();
}

// ============================================================================
// Resolve pipeline (compute)
// ============================================================================

bool VisibilityBuffer::createResolvePipeline() {
    if (!raiiDevice_) return false;

    // Create depth sampler for the resolve pass
    auto depthSamplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);

    try {
        depthSampler_.emplace(*raiiDevice_, depthSamplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create depth sampler: %s", e.what());
        return false;
    }

    // Create texture sampler for material textures
    auto texSamplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMaxAnisotropy(1.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        textureSampler_.emplace(*raiiDevice_, texSamplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create texture sampler: %s", e.what());
        return false;
    }

    // Descriptor set layout: 11 bindings matching visbuf_resolve.comp
    std::array<vk::DescriptorSetLayoutBinding, 11> bindings;

    // Binding 0: Visibility buffer (uimage2D, storage image)
    bindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_VISIBILITY)
        .setDescriptorType(vk::DescriptorType::eStorageImage)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 1: Depth buffer (sampler2D) — V-buffer depth
    bindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_DEPTH)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 2: HDR output (image2D, storage image)
    bindings[2] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_HDR_OUTPUT)
        .setDescriptorType(vk::DescriptorType::eStorageImage)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 3: Vertex buffer (SSBO)
    bindings[3] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_VERTEX_BUFFER)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 4: Index buffer (SSBO)
    bindings[4] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_INDEX_BUFFER)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 5: Instance buffer (SSBO)
    bindings[5] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_INSTANCE_BUFFER)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 6: Material buffer (SSBO)
    bindings[6] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_MATERIAL_BUFFER)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 7: Resolve uniforms (UBO)
    bindings[7] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_UNIFORMS)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 8: Material texture array (sampler2DArray)
    bindings[8] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_TEXTURE_ARRAY)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 9: HDR pass depth buffer (sampler2D) — for depth comparison
    bindings[9] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_HDR_DEPTH)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    // Binding 10: Dynamic light buffer (SSBO) — for multi-light resolve
    bindings[10] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_LIGHT_BUFFER)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        resolveDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create resolve desc set layout: %s", e.what());
        return false;
    }

    // Pipeline layout (no push constants needed)
    vk::DescriptorSetLayout vkResolveLayout(**resolveDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(vkResolveLayout);

    vk::Device vkDevice(device_);
    resolvePipelineLayout_ = static_cast<VkPipelineLayout>(
        vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load compute shader
    auto compModule = ShaderLoader::loadShaderModule(
        vkDevice, shaderPath_ + "/visbuf_resolve.comp.spv");

    if (!compModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to load resolve compute shader");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(resolvePipelineLayout_);

    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create resolve compute pipeline");
        vkDevice.destroyShaderModule(*compModule);
        return false;
    }
    resolvePipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*compModule);

    // Allocate descriptor sets (one per frame in flight)
    VkDescriptorSetLayout rawResolveLayout = **resolveDescSetLayout_;
    resolveDescSets_ = descriptorPool_->allocate(rawResolveLayout, framesInFlight_);
    if (resolveDescSets_.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to allocate resolve descriptor sets");
        return false;
    }

    SDL_Log("VisibilityBuffer: Resolve pipeline created (10 bindings)");
    return true;
}

void VisibilityBuffer::destroyResolvePipeline() {
    depthSampler_.reset();
    textureSampler_.reset();
    if (resolvePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, resolvePipeline_, nullptr);
        resolvePipeline_ = VK_NULL_HANDLE;
    }
    if (resolvePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, resolvePipelineLayout_, nullptr);
        resolvePipelineLayout_ = VK_NULL_HANDLE;
    }
    resolveDescSetLayout_.reset();
    resolveDescSets_.clear();
}

VkPipeline VisibilityBuffer::getResolvePipeline() const {
    return resolvePipeline_;
}

VkPipelineLayout VisibilityBuffer::getResolvePipelineLayout() const {
    return resolvePipelineLayout_;
}

// ============================================================================
// Descriptor sets
// ============================================================================

bool VisibilityBuffer::createDescriptorSets() {
    return true;
}

void VisibilityBuffer::destroyDescriptorSets() {
    resolveDescSets_.clear();
}

// ============================================================================
// Resolve buffers
// ============================================================================

bool VisibilityBuffer::createResolveBuffers() {
    VkDeviceSize uniformSize = sizeof(VisBufResolveUniforms);
    bool ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(uniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(resolveUniformBuffers_);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create resolve uniform buffers");
        return false;
    }

    // Create placeholder SSBO for unbound vertex/index/material descriptors.
    // The resolve shader early-returns on background pixels (packed == 0),
    // so these are never actually read, but Vulkan requires valid descriptors.
    if (!VmaBufferFactory::createStorageBufferHostWritable(allocator_, PLACEHOLDER_BUFFER_SIZE, placeholderBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create placeholder buffer");
        return false;
    }
    // Zero-fill placeholder
    VmaAllocationInfo placeholderAllocInfo{};
    vmaGetAllocationInfo(allocator_, placeholderBuffer_.get_deleter().allocation, &placeholderAllocInfo);
    if (placeholderAllocInfo.pMappedData) {
        memset(placeholderAllocInfo.pMappedData, 0, PLACEHOLDER_BUFFER_SIZE);
    }

    // Create a 1x1 placeholder texture for the unbound texture array descriptor
    ok = ImageBuilder(allocator_)
        .setExtent(VkExtent2D{1, 1})
        .setFormat(VK_FORMAT_R8G8B8A8_UNORM)
        .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build(device_, placeholderTexImage_, placeholderTexView_);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create placeholder texture");
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyResolveBuffers() {
    if (placeholderTexView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, placeholderTexView_, nullptr);
        placeholderTexView_ = VK_NULL_HANDLE;
    }
    placeholderTexImage_.reset();
    placeholderBuffer_.reset();
    BufferUtils::destroyBuffers(allocator_, resolveUniformBuffers_);
}

// ============================================================================
// Global vertex/index buffers (for resolve pass)
// ============================================================================

bool VisibilityBuffer::buildGlobalBuffers(const std::vector<const Mesh*>& uniqueMeshes) {
    if (uniqueMeshes.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: No meshes to build global buffers");
        return false;
    }

    // Count total vertices and indices across all meshes
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    for (const Mesh* mesh : uniqueMeshes) {
        if (!mesh) continue;
        totalVertices += static_cast<uint32_t>(mesh->getVertices().size());
        totalIndices += mesh->getIndexCount();
    }

    if (totalVertices == 0 || totalIndices == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Empty mesh data");
        return false;
    }

    // Build CPU-side packed vertex and offset-adjusted index arrays
    std::vector<VisBufPackedVertex> packedVertices;
    packedVertices.reserve(totalVertices);
    std::vector<uint32_t> globalIndices;
    globalIndices.reserve(totalIndices);
    meshInfoMap_.clear();

    uint32_t currentVertexOffset = 0;
    uint32_t currentIndexOffset = 0;

    for (const Mesh* mesh : uniqueMeshes) {
        if (!mesh) continue;

        const auto& verts = mesh->getVertices();
        uint32_t indexCount = mesh->getIndexCount();

        // Track mesh info
        VisBufMeshInfo info{};
        info.globalVertexOffset = currentVertexOffset;
        info.globalIndexOffset = currentIndexOffset;
        info.triangleOffset = currentIndexOffset / 3;
        meshInfoMap_[mesh] = info;

        // Repack vertices into PackedVertex format
        for (const auto& v : verts) {
            VisBufPackedVertex pv{};
            pv.positionAndU = glm::vec4(v.position, v.texCoord.x);
            pv.normalAndV = glm::vec4(v.normal, v.texCoord.y);
            pv.tangent = v.tangent;
            pv.color = v.color;
            packedVertices.push_back(pv);
        }

        // Copy mesh indices, offset to global vertex space
        const auto& meshIndices = mesh->getIndices();
        for (uint32_t idx : meshIndices) {
            globalIndices.push_back(idx + currentVertexOffset);
        }

        currentVertexOffset += static_cast<uint32_t>(verts.size());
        currentIndexOffset += indexCount;
    }

    // Upload to GPU storage buffers
    globalVertexBufferSize_ = packedVertices.size() * sizeof(VisBufPackedVertex);
    globalIndexBufferSize_ = globalIndices.size() * sizeof(uint32_t);

    // Vertex buffer
    if (!VmaBufferFactory::createStorageBufferHostWritable(
            allocator_, globalVertexBufferSize_, globalVertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create global vertex buffer");
        return false;
    }

    VmaAllocationInfo vertAllocInfo{};
    vmaGetAllocationInfo(allocator_, globalVertexBuffer_.get_deleter().allocation, &vertAllocInfo);
    if (vertAllocInfo.pMappedData) {
        memcpy(vertAllocInfo.pMappedData, packedVertices.data(), globalVertexBufferSize_);
        vmaFlushAllocation(allocator_, globalVertexBuffer_.get_deleter().allocation,
                           0, globalVertexBufferSize_);
    }

    // Index buffer
    if (!VmaBufferFactory::createStorageBufferHostWritable(
            allocator_, globalIndexBufferSize_, globalIndexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create global index buffer");
        return false;
    }

    VmaAllocationInfo idxAllocInfo{};
    vmaGetAllocationInfo(allocator_, globalIndexBuffer_.get_deleter().allocation, &idxAllocInfo);
    if (idxAllocInfo.pMappedData) {
        memcpy(idxAllocInfo.pMappedData, globalIndices.data(), globalIndexBufferSize_);
        vmaFlushAllocation(allocator_, globalIndexBuffer_.get_deleter().allocation,
                           0, globalIndexBufferSize_);
    }

    globalBuffersBuilt_ = true;
    SDL_Log("VisibilityBuffer: Global buffers built (%u vertices, %u indices, %zu meshes)",
            totalVertices, totalIndices, uniqueMeshes.size());
    return true;
}

bool VisibilityBuffer::buildGlobalBuffersFromClusters(
        const std::vector<std::pair<const Mesh*, const ClusteredMesh*>>& meshClusters) {
    if (meshClusters.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VisibilityBuffer: No clustered meshes to build global buffers");
        return false;
    }

    // Count total vertices and indices across all clustered meshes
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    for (const auto& [mesh, clustered] : meshClusters) {
        if (!clustered) continue;
        totalVertices += static_cast<uint32_t>(clustered->vertices.size());
        totalIndices += static_cast<uint32_t>(clustered->indices.size());
    }

    if (totalVertices == 0 || totalIndices == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VisibilityBuffer: Empty clustered mesh data");
        return false;
    }

    // Build CPU-side packed vertex and offset-adjusted index arrays
    std::vector<VisBufPackedVertex> packedVertices;
    packedVertices.reserve(totalVertices);
    std::vector<uint32_t> globalIndices;
    globalIndices.reserve(totalIndices);
    meshInfoMap_.clear();

    uint32_t currentVertexOffset = 0;
    uint32_t currentIndexOffset = 0;

    for (const auto& [mesh, clustered] : meshClusters) {
        if (!clustered || !mesh) continue;

        // Track mesh info (same structure as buildGlobalBuffers)
        VisBufMeshInfo info{};
        info.globalVertexOffset = currentVertexOffset;
        info.globalIndexOffset = currentIndexOffset;
        info.triangleOffset = currentIndexOffset / 3;
        meshInfoMap_[mesh] = info;

        // Repack cluster vertices into PackedVertex format
        for (const auto& v : clustered->vertices) {
            VisBufPackedVertex pv{};
            pv.positionAndU = glm::vec4(v.position, v.texCoord.x);
            pv.normalAndV = glm::vec4(v.normal, v.texCoord.y);
            pv.tangent = v.tangent;
            pv.color = v.color;
            packedVertices.push_back(pv);
        }

        // Copy cluster indices, offset to global vertex space
        for (uint32_t idx : clustered->indices) {
            globalIndices.push_back(idx + currentVertexOffset);
        }

        currentVertexOffset += static_cast<uint32_t>(clustered->vertices.size());
        currentIndexOffset += static_cast<uint32_t>(clustered->indices.size());
    }

    // Upload to GPU storage buffers
    globalVertexBufferSize_ = packedVertices.size() * sizeof(VisBufPackedVertex);
    globalIndexBufferSize_ = globalIndices.size() * sizeof(uint32_t);

    if (!VmaBufferFactory::createStorageBufferHostWritable(
            allocator_, globalVertexBufferSize_, globalVertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create cluster global vertex buffer");
        return false;
    }

    VmaAllocationInfo vertAllocInfo{};
    vmaGetAllocationInfo(allocator_, globalVertexBuffer_.get_deleter().allocation, &vertAllocInfo);
    if (vertAllocInfo.pMappedData) {
        memcpy(vertAllocInfo.pMappedData, packedVertices.data(), globalVertexBufferSize_);
        vmaFlushAllocation(allocator_, globalVertexBuffer_.get_deleter().allocation,
                           0, globalVertexBufferSize_);
    }

    if (!VmaBufferFactory::createStorageBufferHostWritable(
            allocator_, globalIndexBufferSize_, globalIndexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create cluster global index buffer");
        return false;
    }

    VmaAllocationInfo idxAllocInfo{};
    vmaGetAllocationInfo(allocator_, globalIndexBuffer_.get_deleter().allocation, &idxAllocInfo);
    if (idxAllocInfo.pMappedData) {
        memcpy(idxAllocInfo.pMappedData, globalIndices.data(), globalIndexBufferSize_);
        vmaFlushAllocation(allocator_, globalIndexBuffer_.get_deleter().allocation,
                           0, globalIndexBufferSize_);
    }

    globalBuffersBuilt_ = true;
    SDL_Log("VisibilityBuffer: Cluster global buffers built (%u vertices, %u indices, %zu meshes)",
            totalVertices, totalIndices, meshClusters.size());
    return true;
}

const VisBufMeshInfo* VisibilityBuffer::getMeshInfo(const Mesh* mesh) const {
    auto it = meshInfoMap_.find(mesh);
    if (it != meshInfoMap_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// Material texture array
// ============================================================================

bool VisibilityBuffer::buildMaterialTextureArray(const MaterialRegistry& registry) {
    if (graphicsQueue_ == VK_NULL_HANDLE || commandPool_ == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Cannot build texture array - no queue/command pool");
        return false;
    }

    // Collect unique textures from all materials (diffuse, normal, roughness/metallic)
    std::vector<const Texture*> textures;
    textureLayerMap_.clear();

    auto addTexture = [&](const Texture* tex) {
        if (!tex) return;
        if (textureLayerMap_.find(tex) != textureLayerMap_.end()) return;
        textureLayerMap_[tex] = static_cast<uint32_t>(textures.size());
        textures.push_back(tex);
    };

    size_t matCount = registry.getMaterialCount();
    for (uint32_t i = 0; i < matCount; ++i) {
        const auto* def = registry.getMaterial(i);
        if (!def) continue;
        addTexture(def->diffuse);
        addTexture(def->normal);
        addTexture(def->roughnessMap);
        addTexture(def->metallicMap);
    }

    if (textures.empty()) {
        SDL_Log("VisibilityBuffer: No material textures to build array from");
        return false;
    }

    // Determine array resolution from the first texture (blit handles resizing)
    uint32_t arrayW = static_cast<uint32_t>(textures[0]->getWidth());
    uint32_t arrayH = static_cast<uint32_t>(textures[0]->getHeight());
    uint32_t layerCount = static_cast<uint32_t>(textures.size());

    SDL_Log("VisibilityBuffer: Building texture array %ux%u with %u layers",
            arrayW, arrayH, layerCount);

    // Create the 2D array image (UNORM - sRGB conversion done in shader for albedo)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setExtent({arrayW, arrayH, 1})
        .setMipLevels(1)
        .setArrayLayers(layerCount)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkImage rawImage;
    VmaAllocation rawAlloc;
    if (vmaCreateImage(allocator_,
                       reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                       &allocInfo, &rawImage, &rawAlloc, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to create texture array image");
        return false;
    }
    textureArrayImage_ = ManagedImage(rawImage, {allocator_, rawAlloc});

    // One-shot command buffer for blitting
    CommandScope cmd(device_, commandPool_, graphicsQueue_);
    if (!cmd.begin()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to begin texture array command buffer");
        return false;
    }

    vk::CommandBuffer vkCmd = cmd.get();

    // Transition the entire array to TRANSFER_DST
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setImage(textureArrayImage_.get())
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, layerCount});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);
    }

    // Blit each texture into its layer
    for (uint32_t layer = 0; layer < layerCount; ++layer) {
        const Texture* tex = textures[layer];
        VkImage srcImage = tex->getImage();
        int srcW = tex->getWidth();
        int srcH = tex->getHeight();

        // Transition source to TRANSFER_SRC
        {
            auto barrier = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
                .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setImage(srcImage)
                .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

            vkCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {}, barrier);
        }

        // Blit (handles format conversion and resizing)
        vk::ImageBlit region;
        region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        region.srcOffsets[1] = vk::Offset3D{srcW, srcH, 1};
        region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, layer, 1};
        region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        region.dstOffsets[1] = vk::Offset3D{
            static_cast<int32_t>(arrayW),
            static_cast<int32_t>(arrayH), 1};

        vkCmd.blitImage(
            srcImage, vk::ImageLayout::eTransferSrcOptimal,
            textureArrayImage_.get(), vk::ImageLayout::eTransferDstOptimal,
            region, vk::Filter::eLinear);

        // Transition source back to SHADER_READ_ONLY
        {
            auto barrier = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
                .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setImage(srcImage)
                .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

            vkCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                {}, {}, {}, barrier);
        }
    }

    // Transition array to SHADER_READ_ONLY
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImage(textureArrayImage_.get())
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, layerCount});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);
    }

    if (!cmd.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to submit texture array commands");
        return false;
    }

    // Create 2D array image view
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(textureArrayImage_.get())
        .setViewType(vk::ImageViewType::e2DArray)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, layerCount});

    vk::Device vkDevice(device_);
    textureArrayView_ = static_cast<VkImageView>(vkDevice.createImageView(viewInfo));

    // Create sampler
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxAnisotropy(1.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    textureArraySampler_.emplace(*raiiDevice_, samplerInfo);

    textureArrayBuilt_ = true;
    SDL_Log("VisibilityBuffer: Texture array built (%u layers, %ux%u)",
            layerCount, arrayW, arrayH);
    return true;
}

VkSampler VisibilityBuffer::getTextureArraySampler() const {
    return textureArraySampler_ ? static_cast<VkSampler>(**textureArraySampler_) : VK_NULL_HANDLE;
}

uint32_t VisibilityBuffer::getTextureLayerIndex(const Texture* tex) const {
    auto it = textureLayerMap_.find(tex);
    return (it != textureLayerMap_.end()) ? it->second : ~0u;
}

// ============================================================================
// Raster pass descriptor sets
// ============================================================================

bool VisibilityBuffer::createRasterDescriptorSets(
    const std::vector<VkBuffer>& uboBuffers,
    VkDeviceSize uboSize) {

    if (!rasterDescSetLayout_ || uboBuffers.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Cannot create raster desc sets - layout or UBO not ready");
        return false;
    }

    VkDescriptorSetLayout rawLayout = **rasterDescSetLayout_;
    rasterDescSets_ = descriptorPool_->allocate(rawLayout, static_cast<uint32_t>(uboBuffers.size()));
    if (rasterDescSets_.size() != uboBuffers.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VisibilityBuffer: Failed to allocate raster descriptor sets");
        return false;
    }

    // Update each frame's descriptor set
    for (size_t i = 0; i < rasterDescSets_.size(); ++i) {
        VkDescriptorSet descSet = rasterDescSets_[i];

        // Binding 0: UBO
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = uboBuffers[i];
        uboInfo.offset = 0;
        uboInfo.range = uboSize;

        // Binding 1: Placeholder diffuse texture (alphaTestThreshold = 0 means it won't be sampled)
        VkDescriptorImageInfo texInfo{};
        texInfo.sampler = nearestSampler_ ? static_cast<VkSampler>(**nearestSampler_) : VK_NULL_HANDLE;
        texInfo.imageView = placeholderTexView_;
        texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSet;
        writes[0].dstBinding = 0;  // BINDING_UBO
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uboInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSet;
        writes[1].dstBinding = 1;  // BINDING_DIFFUSE_TEX
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &texInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    SDL_Log("VisibilityBuffer: Raster descriptor sets created (%zu frames)",
            rasterDescSets_.size());
    return true;
}

VkDescriptorSet VisibilityBuffer::getRasterDescriptorSet(uint32_t frameIndex) const {
    if (frameIndex < rasterDescSets_.size()) {
        return rasterDescSets_[frameIndex];
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// Resize
// ============================================================================

void VisibilityBuffer::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent_.width && newExtent.height == extent_.height) {
        return;
    }

    vkDeviceWaitIdle(device_);

    extent_ = newExtent;

    // Recreate size-dependent resources
    destroyFramebuffer();
    destroyRenderTargets();

    createRenderTargets();
    createFramebuffer();

    // Update debug descriptor set with new image views
    if (debugDescSet_ != VK_NULL_HANDLE && nearestSampler_) {
        VkSampler rawSampler = **nearestSampler_;

        VkDescriptorImageInfo visInfo{};
        visInfo.sampler = rawSampler;
        visInfo.imageView = visibilityView_;
        visInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = rawSampler;
        depthInfo.imageView = depthView_;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = debugDescSet_;
        writes[0].dstBinding = BINDING_VISBUF_DEBUG_INPUT;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &visInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = debugDescSet_;
        writes[1].dstBinding = BINDING_VISBUF_DEBUG_DEPTH_INPUT;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    SDL_Log("VisibilityBuffer: Resized to %ux%u", extent_.width, extent_.height);
}

// ============================================================================
// Command recording helpers
// ============================================================================

void VisibilityBuffer::transitionToShaderRead(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = visibilityImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VisibilityBuffer::transitionToColorAttachment(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = visibilityImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VisibilityBuffer::recordClear(VkCommandBuffer cmd) {
    // V-buffer clear happens in the render pass (loadOp = CLEAR)
    // This method is for explicit clears if needed outside the render pass
    VkClearColorValue clearColor{};
    clearColor.uint32[0] = 0;  // 0 = no geometry

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    vkCmdClearColorImage(cmd, visibilityImage_.get(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          &clearColor, 1, &range);
}

void VisibilityBuffer::setResolveBuffers(const ResolveBuffers& buffers) {
    resolveBuffers_ = buffers;
    resolveDescSetsDirty_ = true;
}

void VisibilityBuffer::updateResolveUniforms(uint32_t frameIndex,
                                              const glm::mat4& view, const glm::mat4& proj,
                                              const glm::vec3& cameraPos,
                                              const glm::vec3& sunDir, float sunIntensity,
                                              uint32_t materialCount) {
    VisBufResolveUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.invViewProj = glm::inverse(proj * view);
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        1.0f / static_cast<float>(extent_.width),
        1.0f / static_cast<float>(extent_.height)
    );
    uniforms.lightDirection = glm::vec4(sunDir, sunIntensity);
    uniforms.materialCount = materialCount > 0 ? materialCount : resolveBuffers_.materialCount;

    void* mapped = resolveUniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &uniforms, sizeof(uniforms));
        vmaFlushAllocation(allocator_, resolveUniformBuffers_.allocations[frameIndex],
                           0, sizeof(uniforms));
    }
}

void VisibilityBuffer::recordResolvePass(VkCommandBuffer cmd, uint32_t frameIndex,
                                          VkImageView hdrOutputView) {
    if (resolvePipeline_ == VK_NULL_HANDLE) {
        return; // Pipeline not yet created
    }

    if (resolveDescSets_.empty() || frameIndex >= resolveDescSets_.size()) {
        return;
    }

    // Update descriptor set if buffers changed or HDR output changed
    if (resolveDescSetsDirty_ || hdrOutputView != VK_NULL_HANDLE) {
        VkDescriptorSet descSet = resolveDescSets_[frameIndex];
        VkBuffer placeholder = placeholderBuffer_.get();

        // Always bind all 11 descriptors. Use placeholder buffer/texture for
        // unbound slots so Vulkan validation is satisfied. The resolve shader
        // early-returns on background pixels (packed == 0) so placeholders
        // are never actually read when the V-buffer is empty.

        std::array<VkWriteDescriptorSet, 11> writes{};
        for (auto& w : writes) w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

        // Binding 0: Visibility buffer (storage image)
        VkDescriptorImageInfo visImageInfo{};
        visImageInfo.imageView = visibilityView_;
        visImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].dstSet = descSet;
        writes[0].dstBinding = BINDING_VISBUF_VISIBILITY;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &visImageInfo;

        // Binding 1: Depth buffer (combined image sampler)
        VkDescriptorImageInfo depthImageInfo{};
        depthImageInfo.sampler = depthSampler_ ? static_cast<VkSampler>(**depthSampler_) : VK_NULL_HANDLE;
        depthImageInfo.imageView = depthView_;
        depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        writes[1].dstSet = descSet;
        writes[1].dstBinding = BINDING_VISBUF_DEPTH;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthImageInfo;

        // Binding 2: HDR output (storage image)
        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageView = hdrOutputView;
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[2].dstSet = descSet;
        writes[2].dstBinding = BINDING_VISBUF_HDR_OUTPUT;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo = &hdrImageInfo;

        // Binding 3: Vertex buffer (SSBO) - use placeholder if not wired
        VkDescriptorBufferInfo vertexBufInfo{};
        vertexBufInfo.buffer = resolveBuffers_.vertexBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.vertexBuffer : placeholder;
        vertexBufInfo.offset = 0;
        vertexBufInfo.range = resolveBuffers_.vertexBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.vertexBufferSize : PLACEHOLDER_BUFFER_SIZE;

        writes[3].dstSet = descSet;
        writes[3].dstBinding = BINDING_VISBUF_VERTEX_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &vertexBufInfo;

        // Binding 4: Index buffer (SSBO) - use placeholder if not wired
        VkDescriptorBufferInfo indexBufInfo{};
        indexBufInfo.buffer = resolveBuffers_.indexBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.indexBuffer : placeholder;
        indexBufInfo.offset = 0;
        indexBufInfo.range = resolveBuffers_.indexBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.indexBufferSize : PLACEHOLDER_BUFFER_SIZE;

        writes[4].dstSet = descSet;
        writes[4].dstBinding = BINDING_VISBUF_INDEX_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo = &indexBufInfo;

        // Binding 5: Instance buffer (SSBO) - use placeholder if not wired
        VkDescriptorBufferInfo instanceBufInfo{};
        instanceBufInfo.buffer = resolveBuffers_.instanceBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.instanceBuffer : placeholder;
        instanceBufInfo.offset = 0;
        instanceBufInfo.range = resolveBuffers_.instanceBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.instanceBufferSize : PLACEHOLDER_BUFFER_SIZE;

        writes[5].dstSet = descSet;
        writes[5].dstBinding = BINDING_VISBUF_INSTANCE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo = &instanceBufInfo;

        // Binding 6: Material buffer (SSBO) - use placeholder if not wired
        VkDescriptorBufferInfo materialBufInfo{};
        materialBufInfo.buffer = resolveBuffers_.materialBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.materialBuffer : placeholder;
        materialBufInfo.offset = 0;
        materialBufInfo.range = resolveBuffers_.materialBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.materialBufferSize : PLACEHOLDER_BUFFER_SIZE;

        writes[6].dstSet = descSet;
        writes[6].dstBinding = BINDING_VISBUF_MATERIAL_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].pBufferInfo = &materialBufInfo;

        // Binding 7: Resolve uniforms (UBO)
        VkDescriptorBufferInfo uniformBufInfo{};
        uniformBufInfo.buffer = resolveUniformBuffers_.buffers[frameIndex];
        uniformBufInfo.offset = 0;
        uniformBufInfo.range = sizeof(VisBufResolveUniforms);

        writes[7].dstSet = descSet;
        writes[7].dstBinding = BINDING_VISBUF_UNIFORMS;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[7].pBufferInfo = &uniformBufInfo;

        // Binding 8: Material texture array (combined image sampler)
        // Use placeholder texture if no texture array is wired
        VkDescriptorImageInfo texArrayInfo{};
        if (resolveBuffers_.textureArraySampler != VK_NULL_HANDLE) {
            texArrayInfo.sampler = resolveBuffers_.textureArraySampler;
        } else if (textureSampler_) {
            texArrayInfo.sampler = static_cast<VkSampler>(**textureSampler_);
        }
        texArrayInfo.imageView = resolveBuffers_.textureArrayView != VK_NULL_HANDLE
            ? resolveBuffers_.textureArrayView : placeholderTexView_;
        texArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[8].dstSet = descSet;
        writes[8].dstBinding = BINDING_VISBUF_TEXTURE_ARRAY;
        writes[8].descriptorCount = 1;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].pImageInfo = &texArrayInfo;

        // Binding 9: HDR pass depth buffer (combined image sampler)
        // Used for depth comparison to avoid overwriting closer HDR-pass objects
        VkDescriptorImageInfo hdrDepthInfo{};
        hdrDepthInfo.sampler = depthSampler_ ? static_cast<VkSampler>(**depthSampler_) : VK_NULL_HANDLE;
        hdrDepthInfo.imageView = resolveBuffers_.hdrDepthView != VK_NULL_HANDLE
            ? resolveBuffers_.hdrDepthView : depthView_;  // fallback to V-buffer depth
        hdrDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        writes[9].dstSet = descSet;
        writes[9].dstBinding = BINDING_VISBUF_HDR_DEPTH;
        writes[9].descriptorCount = 1;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].pImageInfo = &hdrDepthInfo;

        // Binding 10: Dynamic light buffer (SSBO)
        VkDescriptorBufferInfo lightBufInfo{};
        lightBufInfo.buffer = resolveBuffers_.lightBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.lightBuffer : placeholder;
        lightBufInfo.offset = 0;
        lightBufInfo.range = resolveBuffers_.lightBuffer != VK_NULL_HANDLE
            ? resolveBuffers_.lightBufferSize : PLACEHOLDER_BUFFER_SIZE;

        writes[10].dstSet = descSet;
        writes[10].dstBinding = BINDING_VISBUF_LIGHT_BUFFER;
        writes[10].descriptorCount = 1;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[10].pBufferInfo = &lightBufInfo;

        vkUpdateDescriptorSets(device_,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        resolveDescSetsDirty_ = false;
    }

    // Transition visibility buffer to GENERAL for storage image read
    // and HDR depth to READ_ONLY for sampling in the depth comparison
    {
        std::array<VkImageMemoryBarrier, 2> barriers{};

        // V-buffer: COLOR_ATTACHMENT → GENERAL (for storage image read)
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[0].image = visibilityImage_.get();
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.layerCount = 1;

        uint32_t barrierCount = 1;

        // HDR depth: DEPTH_STENCIL_ATTACHMENT → DEPTH_STENCIL_READ_ONLY (for sampling)
        if (resolveBuffers_.hdrDepthImage != VK_NULL_HANDLE) {
            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            barriers[1].image = resolveBuffers_.hdrDepthImage;
            barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            barriers[1].subresourceRange.levelCount = 1;
            barriers[1].subresourceRange.layerCount = 1;
            barrierCount = 2;
        }

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, barrierCount, barriers.data());
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolvePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                             resolvePipelineLayout_, 0, 1,
                             &resolveDescSets_[frameIndex], 0, nullptr);

    // Dispatch: 8x8 workgroup size
    uint32_t groupsX = (extent_.width + 7) / 8;
    uint32_t groupsY = (extent_.height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Barrier: resolve writes -> subsequent reads of HDR output
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void VisibilityBuffer::recordDebugVisualization(VkCommandBuffer cmd, uint32_t debugMode) {
    if (debugDescSet_ == VK_NULL_HANDLE || debugPipelineLayout_ == VK_NULL_HANDLE) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Bind debug descriptor set
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              vk::PipelineLayout(debugPipelineLayout_),
                              0, vk::DescriptorSet(debugDescSet_), {});

    // Push debug mode
    VisBufDebugPushConstants push{};
    push.mode = debugMode;
    vkCmd.pushConstants<VisBufDebugPushConstants>(
        vk::PipelineLayout(debugPipelineLayout_),
        vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
    vkCmd.draw(3, 1, 0, 0);
}
