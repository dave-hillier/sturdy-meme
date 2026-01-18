#include "WaterGBuffer.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "core/vulkan/SamplerFactory.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <algorithm>

std::unique_ptr<WaterGBuffer> WaterGBuffer::create(const InitInfo& info) {
    auto system = std::make_unique<WaterGBuffer>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

WaterGBuffer::~WaterGBuffer() {
    cleanup();
}

bool WaterGBuffer::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    fullResExtent = info.fullResExtent;
    resolutionScale = info.resolutionScale;
    shaderPath = info.shaderPath;
    descriptorPool = info.descriptorPool;
    framesInFlight = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: raiiDevice is required");
        return false;
    }

    // Calculate G-buffer resolution
    gbufferExtent.width = static_cast<uint32_t>(fullResExtent.width * resolutionScale);
    gbufferExtent.height = static_cast<uint32_t>(fullResExtent.height * resolutionScale);
    gbufferExtent.width = std::max(gbufferExtent.width, 1u);
    gbufferExtent.height = std::max(gbufferExtent.height, 1u);

    SDL_Log("WaterGBuffer: Initializing at %dx%d (%.0f%% of %dx%d)",
            gbufferExtent.width, gbufferExtent.height,
            resolutionScale * 100.0f,
            fullResExtent.width, fullResExtent.height);

    if (!createImages()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create images");
        return false;
    }

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create render pass");
        return false;
    }

    if (!createFramebuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create framebuffer");
        return false;
    }

    if (!createSampler()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create sampler");
        return false;
    }

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create descriptor set layout");
        return false;
    }

    if (!createPipelineLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create pipeline layout");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create pipeline");
        return false;
    }

    SDL_Log("WaterGBuffer: Initialized successfully");
    return true;
}

void WaterGBuffer::cleanup() {
    if (!raiiDevice_) return;

    raiiDevice_->waitIdle();

    // RAII wrappers handle cleanup automatically - just reset them
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();
    // Note: descriptor sets are freed when pool is destroyed
    sampler_.reset();
    framebuffer_.reset();
    renderPass_.reset();

    destroyImages();

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;

    SDL_Log("WaterGBuffer: Destroyed");
}

void WaterGBuffer::resize(VkExtent2D newFullResExtent) {
    fullResExtent = newFullResExtent;

    // Recalculate G-buffer resolution
    gbufferExtent.width = static_cast<uint32_t>(fullResExtent.width * resolutionScale);
    gbufferExtent.height = static_cast<uint32_t>(fullResExtent.height * resolutionScale);
    gbufferExtent.width = std::max(gbufferExtent.width, 1u);
    gbufferExtent.height = std::max(gbufferExtent.height, 1u);

    SDL_Log("WaterGBuffer: Resizing to %dx%d", gbufferExtent.width, gbufferExtent.height);

    if (raiiDevice_) {
        raiiDevice_->waitIdle();
    }

    // Destroy old framebuffer (RAII reset)
    framebuffer_.reset();

    // Destroy and recreate images
    destroyImages();
    createImages();
    createFramebuffer();
}

bool WaterGBuffer::createImages() {
    // Data image (RGBA8 - material data)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(gbufferExtent.width, gbufferExtent.height)
                .setFormat(VK_FORMAT_R8G8B8A8_UNORM)
                .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .build(*raiiDevice_, image, dataImageView_)) {
            return false;
        }
        image.releaseToRaw(dataImage, dataAllocation);
    }

    // Normal image (RGBA16F - normals + depth)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(gbufferExtent.width, gbufferExtent.height)
                .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .build(*raiiDevice_, image, normalImageView_)) {
            return false;
        }
        image.releaseToRaw(normalImage, normalAllocation);
    }

    // Depth image (D32F - water-only depth)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(gbufferExtent.width, gbufferExtent.height)
                .setFormat(VK_FORMAT_D32_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .build(*raiiDevice_, image, depthImageView_, vk::ImageAspectFlagBits::eDepth)) {
            return false;
        }
        image.releaseToRaw(depthImage, depthAllocation);
    }

    return true;
}

void WaterGBuffer::destroyImages() {
    dataImageView_.reset();
    if (dataImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, dataImage, dataAllocation);
        dataImage = VK_NULL_HANDLE;
        dataAllocation = VK_NULL_HANDLE;
    }

    normalImageView_.reset();
    if (normalImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, normalImage, normalAllocation);
        normalImage = VK_NULL_HANDLE;
        normalAllocation = VK_NULL_HANDLE;
    }

    depthImageView_.reset();
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
        depthAllocation = VK_NULL_HANDLE;
    }
}

bool WaterGBuffer::createRenderPass() {
    // Attachment descriptions
    std::array<vk::AttachmentDescription, 3> attachments = {
        // Data attachment (RGBA8)
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        // Normal attachment (RGBA16F)
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        // Depth attachment (D32F)
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eD32Sfloat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal)
    };

    // Subpass
    std::array<vk::AttachmentReference, 2> colorRefs = {
        vk::AttachmentReference{0, vk::ImageLayout::eColorAttachmentOptimal},
        vk::AttachmentReference{1, vk::ImageLayout::eColorAttachmentOptimal}
    };

    vk::AttachmentReference depthRef{2, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorRefs)
        .setPDepthStencilAttachment(&depthRef);

    // Subpass dependencies
    std::array<vk::SubpassDependency, 2> dependencies = {
        vk::SubpassDependency{}
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(vk::PipelineStageFlagBits::eFragmentShader)
            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite),
        vk::SubpassDependency{}
            .setSrcSubpass(0)
            .setDstSubpass(VK_SUBPASS_EXTERNAL)
            .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests)
            .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
            .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
    };

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachments(attachments)
        .setSubpasses(subpass)
        .setDependencies(dependencies);

    try {
        renderPass_.emplace(*raiiDevice_, renderPassInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create render pass: %s", e.what());
        return false;
    }
    return true;
}

bool WaterGBuffer::createFramebuffer() {
    std::array<vk::ImageView, 3> attachments = {
        **dataImageView_,
        **normalImageView_,
        **depthImageView_
    };

    auto framebufferInfo = vk::FramebufferCreateInfo{}
        .setRenderPass(**renderPass_)
        .setAttachments(attachments)
        .setWidth(gbufferExtent.width)
        .setHeight(gbufferExtent.height)
        .setLayers(1);

    try {
        framebuffer_.emplace(*raiiDevice_, framebufferInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create framebuffer: %s", e.what());
        return false;
    }
    return true;
}

bool WaterGBuffer::createSampler() {
    // Use factory for linear clamp sampler
    sampler_ = SamplerFactory::createSamplerLinearClampLimitedMip(*raiiDevice_, 0.0f);
    if (!sampler_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create sampler");
        return false;
    }
    return true;
}

void WaterGBuffer::beginRenderPass(VkCommandBuffer cmd) {
    std::array<vk::ClearValue, 3> clearValues{};
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});  // Data (no water)
    clearValues[1].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});  // Normal
    clearValues[2].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};            // Depth (far)

    auto renderPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(**renderPass_)
        .setFramebuffer(**framebuffer_)
        .setRenderArea(vk::Rect2D{{0, 0}, vk::Extent2D{gbufferExtent.width, gbufferExtent.height}})
        .setClearValues(clearValues);

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set viewport and scissor
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(gbufferExtent.width))
        .setHeight(static_cast<float>(gbufferExtent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{gbufferExtent.width, gbufferExtent.height});
    vkCmd.setScissor(0, scissor);
}

void WaterGBuffer::endRenderPass(VkCommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.endRenderPass();
}

void WaterGBuffer::clear(VkCommandBuffer cmd) {
    // The render pass already clears on begin, so this is a no-op
    // But could be used for mid-frame clearing if needed
}

bool WaterGBuffer::createDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
        // Binding 0: Main UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        // Binding 1: Water UBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        // Binding 3: Terrain height map
        vk::DescriptorSetLayoutBinding{}
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Binding 4: Flow map
        vk::DescriptorSetLayoutBinding{}
            .setBinding(4)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    try {
        descriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create descriptor set layout: %s", e.what());
        return false;
    }

    SDL_Log("WaterGBuffer: Descriptor set layout created");
    return true;
}

bool WaterGBuffer::createPipelineLayout() {
    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descriptorSetLayout_)
            .buildInto(pipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create pipeline layout");
        return false;
    }

    SDL_Log("WaterGBuffer: Pipeline layout created");
    return true;
}

bool WaterGBuffer::createPipeline() {
    GraphicsPipelineFactory factory(device);

    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc};
    std::vector<VkVertexInputAttributeDescription> attributes(attrDescs.begin(), attrDescs.end());

    // G-buffer pipeline: write to both color attachments, depth test and write
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    bool success = factory
        .setShaders(shaderPath + "/water_position.vert.spv",
                    shaderPath + "/water_position.frag.spv")
        .setRenderPass(**renderPass_)
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(gbufferExtent)
        .setDynamicViewport(true)
        .setVertexInput(bindings, attributes)
        .setDepthTest(true)
        .setDepthWrite(true)
        .setCullMode(VK_CULL_MODE_NONE)
        .setColorAttachmentCount(2)  // Data + Normal textures
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create pipeline");
        return false;
    }

    pipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("WaterGBuffer: Pipeline created");
    return true;
}

bool WaterGBuffer::createDescriptorSets(
    const std::vector<VkBuffer>& mainUBOs,
    VkDeviceSize mainUBOSize,
    const std::vector<VkBuffer>& waterUBOs,
    VkDeviceSize waterUBOSize,
    VkImageView terrainHeightView, VkSampler terrainSampler,
    VkImageView flowMapView, VkSampler flowMapSampler) {

    if (descriptorPool == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Descriptor pool is null");
        return false;
    }

    // Allocate descriptor sets using DescriptorManager::Pool
    descriptorSets = descriptorPool->allocate(**descriptorSetLayout_, framesInFlight);
    if (descriptorSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to allocate descriptor sets");
        return false;
    }

    // Update descriptor sets for each frame
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeBuffer(0, mainUBOs[i], 0, mainUBOSize)
            .writeBuffer(1, waterUBOs[i], 0, waterUBOSize)
            .writeImage(3, terrainHeightView, terrainSampler)
            .writeImage(4, flowMapView, flowMapSampler)
            .update();
    }

    SDL_Log("WaterGBuffer: Descriptor sets created for %u frames", framesInFlight);
    return true;
}
