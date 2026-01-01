#include "WaterGBuffer.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <algorithm>

std::unique_ptr<WaterGBuffer> WaterGBuffer::create(const InitInfo& info) {
    std::unique_ptr<WaterGBuffer> system(new WaterGBuffer());
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
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

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

    vkDeviceWaitIdle(device);

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
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setExtent(vk::Extent3D{gbufferExtent.width, gbufferExtent.height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &dataImage, &dataAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(dataImage)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &dataImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Normal image (RGBA16F - normals + depth)
    {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setExtent(vk::Extent3D{gbufferExtent.width, gbufferExtent.height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &normalImage, &normalAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(normalImage)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &normalImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Depth image (D32F - water-only depth)
    {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setExtent(vk::Extent3D{gbufferExtent.width, gbufferExtent.height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &depthImage, &depthAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(depthImage)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &depthImageView) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

void WaterGBuffer::destroyImages() {
    if (dataImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, dataImageView, nullptr);
        dataImageView = VK_NULL_HANDLE;
    }
    if (dataImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, dataImage, dataAllocation);
        dataImage = VK_NULL_HANDLE;
        dataAllocation = VK_NULL_HANDLE;
    }

    if (normalImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, normalImageView, nullptr);
        normalImageView = VK_NULL_HANDLE;
    }
    if (normalImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, normalImage, normalAllocation);
        normalImage = VK_NULL_HANDLE;
        normalAllocation = VK_NULL_HANDLE;
    }

    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
        depthAllocation = VK_NULL_HANDLE;
    }
}

bool WaterGBuffer::createRenderPass() {
    // Attachment descriptions
    std::array<VkAttachmentDescription, 3> attachments{};

    // Data attachment (RGBA8)
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal attachment (RGBA16F)
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Depth attachment (D32F)
    attachments[2].format = VK_FORMAT_D32_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    // Subpass
    std::array<VkAttachmentReference, 2> colorRefs{};
    colorRefs[0].attachment = 0;
    colorRefs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorRefs[1].attachment = 1;
    colorRefs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 2;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    // Subpass dependencies
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass rawRenderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &rawRenderPass) != VK_SUCCESS) {
        return false;
    }
    renderPass_.emplace(*raiiDevice_, rawRenderPass);
    return true;
}

bool WaterGBuffer::createFramebuffer() {
    std::array<VkImageView, 3> attachments = {
        dataImageView,
        normalImageView,
        depthImageView
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = **renderPass_;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = gbufferExtent.width;
    framebufferInfo.height = gbufferExtent.height;
    framebufferInfo.layers = 1;

    VkFramebuffer rawFramebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &rawFramebuffer) != VK_SUCCESS) {
        return false;
    }
    framebuffer_.emplace(*raiiDevice_, rawFramebuffer);
    return true;
}

bool WaterGBuffer::createSampler() {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_FALSE)
        .setMaxAnisotropy(1.0f)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create sampler: %s", e.what());
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
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

    // Binding 0: Main UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Water UBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 3: Terrain height map
    bindings[2].binding = 3;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    // Binding 4: Flow map
    bindings[3].binding = 4;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rawLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    SDL_Log("WaterGBuffer: Descriptor set layout created");
    return true;
}

bool WaterGBuffer::createPipelineLayout() {
    VkDescriptorSetLayout rawLayout = **descriptorSetLayout_;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &rawPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create pipeline layout");
        return false;
    }
    pipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

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
