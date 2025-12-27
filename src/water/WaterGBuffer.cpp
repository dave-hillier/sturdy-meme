#include "WaterGBuffer.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <array>
#include <algorithm>

using namespace vk;

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
    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Data image (RGBA8 - material data)
    {
        ImageCreateInfo imageInfo{
            {},                                  // flags
            ImageType::e2D,
            Format::eR8G8B8A8Unorm,
            Extent3D{gbufferExtent.width, gbufferExtent.height, 1},
            1, 1,                                // mipLevels, arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eColorAttachment | ImageUsageFlagBits::eSampled,
            SharingMode::eExclusive,
            0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &vmaAllocInfo, &dataImage, &dataAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                                  // flags
            dataImage,
            ImageViewType::e2D,
            Format::eR8G8B8A8Unorm,
            ComponentMapping{},                  // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &dataImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Normal image (RGBA16F - normals + depth)
    {
        ImageCreateInfo imageInfo{
            {},                                  // flags
            ImageType::e2D,
            Format::eR16G16B16A16Sfloat,
            Extent3D{gbufferExtent.width, gbufferExtent.height, 1},
            1, 1,                                // mipLevels, arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eColorAttachment | ImageUsageFlagBits::eSampled,
            SharingMode::eExclusive,
            0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &vmaAllocInfo, &normalImage, &normalAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                                  // flags
            normalImage,
            ImageViewType::e2D,
            Format::eR16G16B16A16Sfloat,
            ComponentMapping{},                  // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &normalImageView) != VK_SUCCESS) {
            return false;
        }
    }

    // Depth image (D32F - water-only depth)
    {
        ImageCreateInfo imageInfo{
            {},                                  // flags
            ImageType::e2D,
            Format::eD32Sfloat,
            Extent3D{gbufferExtent.width, gbufferExtent.height, 1},
            1, 1,                                // mipLevels, arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eDepthStencilAttachment | ImageUsageFlagBits::eSampled,
            SharingMode::eExclusive,
            0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &vmaAllocInfo, &depthImage, &depthAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                                  // flags
            depthImage,
            ImageViewType::e2D,
            Format::eD32Sfloat,
            ComponentMapping{},                  // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eDepth, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
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
    std::array<AttachmentDescription, 3> attachments{{
        // Data attachment (RGBA8)
        {
            {},                                      // flags
            Format::eR8G8B8A8Unorm,
            SampleCountFlagBits::e1,
            AttachmentLoadOp::eClear,
            AttachmentStoreOp::eStore,
            AttachmentLoadOp::eDontCare,             // stencilLoadOp
            AttachmentStoreOp::eDontCare,            // stencilStoreOp
            ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal
        },
        // Normal attachment (RGBA16F)
        {
            {},                                      // flags
            Format::eR16G16B16A16Sfloat,
            SampleCountFlagBits::e1,
            AttachmentLoadOp::eClear,
            AttachmentStoreOp::eStore,
            AttachmentLoadOp::eDontCare,             // stencilLoadOp
            AttachmentStoreOp::eDontCare,            // stencilStoreOp
            ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal
        },
        // Depth attachment (D32F)
        {
            {},                                      // flags
            Format::eD32Sfloat,
            SampleCountFlagBits::e1,
            AttachmentLoadOp::eClear,
            AttachmentStoreOp::eStore,
            AttachmentLoadOp::eDontCare,             // stencilLoadOp
            AttachmentStoreOp::eDontCare,            // stencilStoreOp
            ImageLayout::eUndefined,
            ImageLayout::eDepthStencilReadOnlyOptimal
        }
    }};

    // Subpass attachment references
    std::array<AttachmentReference, 2> colorRefs{{
        {0, ImageLayout::eColorAttachmentOptimal},
        {1, ImageLayout::eColorAttachmentOptimal}
    }};

    AttachmentReference depthRef{2, ImageLayout::eDepthStencilAttachmentOptimal};

    SubpassDescription subpass{
        {},                                          // flags
        PipelineBindPoint::eGraphics,
        0, nullptr,                                  // inputAttachmentCount, pInputAttachments
        static_cast<uint32_t>(colorRefs.size()),
        colorRefs.data(),
        nullptr,                                     // pResolveAttachments
        &depthRef
    };

    // Subpass dependencies
    std::array<SubpassDependency, 2> dependencies{{
        {
            VK_SUBPASS_EXTERNAL, 0,
            PipelineStageFlagBits::eFragmentShader,
            PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eEarlyFragmentTests,
            AccessFlagBits::eShaderRead,
            AccessFlagBits::eColorAttachmentWrite | AccessFlagBits::eDepthStencilAttachmentWrite
        },
        {
            0, VK_SUBPASS_EXTERNAL,
            PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eLateFragmentTests,
            PipelineStageFlagBits::eFragmentShader,
            AccessFlagBits::eColorAttachmentWrite | AccessFlagBits::eDepthStencilAttachmentWrite,
            AccessFlagBits::eShaderRead
        }
    }};

    RenderPassCreateInfo renderPassInfo{
        {},                                          // flags
        attachments,
        subpass,
        dependencies
    };

    auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
    return ManagedRenderPass::create(device, vkRenderPassInfo, renderPass);
}

bool WaterGBuffer::createFramebuffer() {
    std::array<VkImageView, 3> attachments = {
        dataImageView,
        normalImageView,
        depthImageView
    };

    // Use raw struct for VkImageView array interop
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
    SamplerCreateInfo samplerInfo{
        {},                                          // flags
        Filter::eLinear,                             // magFilter
        Filter::eLinear,                             // minFilter
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToEdge,            // addressModeU
        SamplerAddressMode::eClampToEdge,            // addressModeV
        SamplerAddressMode::eClampToEdge,            // addressModeW
        0.0f,                                        // mipLodBias
        VK_FALSE,                                    // anisotropyEnable
        1.0f,                                        // maxAnisotropy
        VK_FALSE,                                    // compareEnable
        {},                                          // compareOp
        0.0f,                                        // minLod
        0.0f,                                        // maxLod
        BorderColor::eFloatOpaqueBlack
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, sampler);
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
