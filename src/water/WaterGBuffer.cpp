#include "WaterGBuffer.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
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
    pipeline = ManagedPipeline();
    pipelineLayout = ManagedPipelineLayout();
    descriptorSetLayout = ManagedDescriptorSetLayout();
    // Note: descriptor sets are freed when pool is destroyed
    sampler = ManagedSampler();
    framebuffer = ManagedFramebuffer();
    renderPass = ManagedRenderPass();

    destroyImages();

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
    framebuffer = ManagedFramebuffer();

    // Destroy and recreate images
    destroyImages();
    createImages();
    createFramebuffer();
}

bool WaterGBuffer::createImages() {
    // Data image (RGBA8 - material data)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {gbufferExtent.width, gbufferExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &dataImage, &dataAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = dataImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &dataImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Normal image (RGBA16F - normals + depth)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = {gbufferExtent.width, gbufferExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &normalImage, &normalAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = normalImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &normalImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Depth image (D32F - water-only depth)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.extent = {gbufferExtent.width, gbufferExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
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

    return ManagedRenderPass::create(device, renderPassInfo, renderPass);
}

bool WaterGBuffer::createFramebuffer() {
    std::array<VkImageView, 3> attachments = {
        dataImageView,
        normalImageView,
        depthImageView
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass.get();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = gbufferExtent.width;
    framebufferInfo.height = gbufferExtent.height;
    framebufferInfo.layers = 1;

    return ManagedFramebuffer::create(device, framebufferInfo, framebuffer);
}

bool WaterGBuffer::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    return ManagedSampler::create(device, samplerInfo, sampler);
}

void WaterGBuffer::beginRenderPass(VkCommandBuffer cmd) {
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Data (no water)
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
    clearValues[2].depthStencil = {1.0f, 0};            // Depth (far)

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass.get();
    renderPassInfo.framebuffer = framebuffer.get();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = gbufferExtent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(gbufferExtent.width);
    viewport.height = static_cast<float>(gbufferExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = gbufferExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void WaterGBuffer::endRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
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

    if (!ManagedDescriptorSetLayout::create(device, layoutInfo, descriptorSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterGBuffer: Failed to create descriptor set layout");
        return false;
    }

    SDL_Log("WaterGBuffer: Descriptor set layout created");
    return true;
}

bool WaterGBuffer::createPipelineLayout() {
    VkDescriptorSetLayout rawLayout = descriptorSetLayout.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, pipelineLayout)) {
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
        .setRenderPass(renderPass.get())
        .setPipelineLayout(pipelineLayout.get())
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

    pipeline = ManagedPipeline::fromRaw(device, rawPipeline);

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
    descriptorSets = descriptorPool->allocate(descriptorSetLayout.get(), framesInFlight);
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
