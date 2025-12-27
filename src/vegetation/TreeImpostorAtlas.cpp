#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "CullCommon.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "BufferUtils.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

std::unique_ptr<TreeImpostorAtlas> TreeImpostorAtlas::create(const InitInfo& info) {
    auto atlas = std::unique_ptr<TreeImpostorAtlas>(new TreeImpostorAtlas());
    if (!atlas->initInternal(info)) {
        return nullptr;
    }
    return atlas;
}

TreeImpostorAtlas::~TreeImpostorAtlas() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    // Cleanup leaf capture buffer
    if (leafCaptureBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafCaptureBuffer_, leafCaptureAllocation_);
    }

    // Cleanup leaf quad mesh
    if (leafQuadVertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafQuadVertexBuffer_, leafQuadVertexAllocation_);
    }
    if (leafQuadIndexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, leafQuadIndexBuffer_, leafQuadIndexAllocation_);
    }

    // Cleanup array textures
    if (octaAlbedoArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octaAlbedoArrayImage_, octaAlbedoArrayAllocation_);
    }
    if (octaNormalArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octaNormalArrayImage_, octaNormalArrayAllocation_);
    }

    // Cleanup per-archetype atlas textures (depth buffers and framebuffers)
    // Note: Don't call ImGui_ImplVulkan_RemoveTexture here - ImGui may already
    // be shut down. ImGui cleans up its own descriptor pool on shutdown anyway.
    for (auto& atlas : atlasTextures_) {
        if (atlas.depthImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, atlas.depthImage, atlas.depthAllocation);
        }
    }
}

bool TreeImpostorAtlas::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxArchetypes_ = info.maxArchetypes;

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create render pass");
        return false;
    }

    if (!createAtlasArrayTextures()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create atlas array textures");
        return false;
    }

    if (!createCapturePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create capture pipeline");
        return false;
    }

    if (!createLeafCapturePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf capture pipeline");
        return false;
    }

    if (!createLeafQuadMesh()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf quad mesh");
        return false;
    }

    if (!createSampler()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create sampler");
        return false;
    }

    SDL_Log("TreeImpostorAtlas: Initialized successfully");
    return true;
}

bool TreeImpostorAtlas::createRenderPass() {
    // Two color attachments: albedo+alpha and normal+depth+AO
    // Albedo + Alpha attachment (RGBA8)
    // Note: We pre-transition array layers to COLOR_ATTACHMENT_OPTIMAL before the render pass
    std::array<vk::AttachmentDescription, 3> attachments = {
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        // Normal + Depth + AO attachment (RGBA8)
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
        // Depth attachment
        vk::AttachmentDescription{}
            .setFormat(vk::Format::eD32Sfloat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    };

    std::array<vk::AttachmentReference, 2> colorRefs = {
        vk::AttachmentReference{}.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal),
        vk::AttachmentReference{}.setAttachment(1).setLayout(vk::ImageLayout::eColorAttachmentOptimal)
    };

    auto depthRef = vk::AttachmentReference{}
        .setAttachment(2)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(colorRefs)
        .setPDepthStencilAttachment(&depthRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachments(attachments)
        .setSubpasses(subpass)
        .setDependencies(dependency);

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device_, reinterpret_cast<const VkRenderPassCreateInfo*>(&renderPassInfo), nullptr, &renderPass) != VK_SUCCESS) {
        return false;
    }
    captureRenderPass_ = ManagedRenderPass(makeUniqueRenderPass(device_, renderPass));

    return true;
}

bool TreeImpostorAtlas::createAtlasArrayTextures() {
    // Create octahedral array textures that will hold all archetypes
    // Each layer is one archetype's atlas (GRID_SIZE x GRID_SIZE cells)

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(maxArchetypes_)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create octahedral albedo+alpha array
    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &octaAlbedoArrayImage_, &octaAlbedoArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array image");
        return false;
    }

    // Create octahedral normal+depth+AO array
    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &octaNormalArrayImage_, &octaNormalArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array image");
        return false;
    }

    // Create image views for the entire arrays
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e2DArray)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(maxArchetypes_));

    viewInfo.setImage(octaAlbedoArrayImage_);
    VkImageView albedoView;
    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array view");
        return false;
    }
    octaAlbedoArrayView_ = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.setImage(octaNormalArrayImage_);
    VkImageView normalView;
    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array view");
        return false;
    }
    octaNormalArrayView_ = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Transition both array images to shader read optimal layout
    auto cmdAllocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(commandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, reinterpret_cast<const VkCommandBufferAllocateInfo*>(&cmdAllocInfo), &cmd);

    auto beginInfo = vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    vkBeginCommandBuffer(cmd, reinterpret_cast<const VkCommandBufferBeginInfo*>(&beginInfo));

    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(maxArchetypes_))
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    // Batch both image barriers into a single call
    std::array<vk::ImageMemoryBarrier, 2> barriers = {barrier, barrier};
    barriers[0].setImage(octaAlbedoArrayImage_);
    barriers[1].setImage(octaNormalArrayImage_);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), reinterpret_cast<const VkImageMemoryBarrier*>(barriers.data()));

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    SDL_Log("TreeImpostorAtlas: Created octahedral array textures (%dx%d, %d layers, %d views)",
            OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT,
            maxArchetypes_, OctahedralAtlasConfig::TOTAL_CELLS);

    return true;
}

// Hemi-octahedral encoding: 3D direction to 2D UV [0,1]
glm::vec2 TreeImpostorAtlas::hemiOctaEncode(glm::vec3 dir) {
    // Ensure direction is in upper hemisphere
    dir.y = glm::max(dir.y, 0.001f);

    // L1 normalize
    float sum = glm::abs(dir.x) + glm::abs(dir.y) + glm::abs(dir.z);
    dir /= sum;

    // Transform from diamond [-1,1] to square [0,1]
    glm::vec2 enc(dir.x, dir.z);
    glm::vec2 result;
    result.x = enc.x + enc.y;
    result.y = enc.y - enc.x;

    return result * 0.5f + 0.5f;
}

// Hemi-octahedral decoding: 2D UV [0,1] to 3D direction
glm::vec3 TreeImpostorAtlas::hemiOctaDecode(glm::vec2 uv) {
    // Map from [0, 1] to [-1, 1]
    uv = uv * 2.0f - 1.0f;

    // Inverse of the diamond rotation
    glm::vec2 enc;
    enc.x = (uv.x - uv.y) * 0.5f;
    enc.y = (uv.x + uv.y) * 0.5f;

    // Reconstruct Y from X and Z
    float y = 1.0f - glm::abs(enc.x) - glm::abs(enc.y);

    return glm::normalize(glm::vec3(enc.x, glm::max(y, 0.0f), enc.y));
}

bool TreeImpostorAtlas::createAtlasResources(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        atlasTextures_.resize(archetypeIndex + 1);
    }

    if (archetypeIndex >= maxArchetypes_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TreeImpostorAtlas: Archetype index %u exceeds max %u", archetypeIndex, maxArchetypes_);
        return false;
    }

    auto& atlas = atlasTextures_[archetypeIndex];

    // Create per-layer views into the shared array textures
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(archetypeIndex)
            .setLayerCount(1));

    viewInfo.setImage(octaAlbedoArrayImage_);
    VkImageView albedoView;
    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create albedo layer view");
        return false;
    }
    atlas.albedoView = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.setImage(octaNormalArrayImage_);
    VkImageView normalView;
    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create normal layer view");
        return false;
    }
    atlas.normalView = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Create depth image
    auto depthImageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(vk::Format::eD32Sfloat)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&depthImageInfo), &allocInfo,
                       &atlas.depthImage, &atlas.depthAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create depth image");
        return false;
    }

    viewInfo.setImage(atlas.depthImage)
        .setFormat(vk::Format::eD32Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    VkImageView depthView;
    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &depthView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create depth view");
        return false;
    }
    atlas.depthView = ManagedImageView(makeUniqueImageView(device_, depthView));

    // Create framebuffer
    std::array<VkImageView, 3> attachments = {
        atlas.albedoView.get(),
        atlas.normalView.get(),
        atlas.depthView.get()
    };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = captureRenderPass_.get();
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = OctahedralAtlasConfig::ATLAS_WIDTH;
    fbInfo.height = OctahedralAtlasConfig::ATLAS_HEIGHT;
    fbInfo.layers = 1;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        return false;
    }
    atlas.framebuffer = ManagedFramebuffer(makeUniqueFramebuffer(device_, framebuffer));

    return true;
}

bool TreeImpostorAtlas::createCapturePipeline() {
    // Create descriptor set layout for capture
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
        // Albedo texture
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Normal texture (for AO extraction)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device_, reinterpret_cast<const VkDescriptorSetLayoutCreateInfo*>(&layoutInfo), nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    captureDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, descriptorSetLayout));

    // Create pipeline layout with push constants
    auto pushConstant = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(glm::mat4) * 2 + sizeof(glm::vec4));  // viewProj, model, captureParams

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstant);

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    capturePipelineLayout_ = ManagedPipelineLayout(makeUniquePipelineLayout(device_, pipelineLayout));

    // Load shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to load capture shaders");
        return false;
    }

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

    // Vertex input (position, normal, texcoord)
    std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions = {
        vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(Vertex))
            .setInputRate(vk::VertexInputRate::eVertex)
    };

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, position)),
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription{}.setLocation(2).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(offsetof(Vertex, texCoord))
    };

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDescriptions)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(OctahedralAtlasConfig::CELL_SIZE))
        .setHeight(static_cast<float>(OctahedralAtlasConfig::CELL_SIZE))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

    auto scissor = vk::Rect2D{}
        .setOffset(vk::Offset2D{0, 0})
        .setExtent(vk::Extent2D{OctahedralAtlasConfig::CELL_SIZE, OctahedralAtlasConfig::CELL_SIZE});

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewports(viewport)
        .setScissors(scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(false)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // No culling for capture
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(false);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(false)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess)
        .setDepthBoundsTestEnable(false)
        .setStencilTestEnable(false);

    // Two color blend attachments (both write all channels)
    std::array<vk::PipelineColorBlendAttachmentState, 2> colorBlendAttachments = {
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(false),
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(false)
    };

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(false)
        .setAttachments(colorBlendAttachments);

    // Dynamic viewport and scissor for rendering to different cells
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

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
        .setLayout(pipelineLayout)
        .setRenderPass(captureRenderPass_.get())
        .setSubpass(0);

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, reinterpret_cast<const VkGraphicsPipelineCreateInfo*>(&pipelineInfo), nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, *vertModule, nullptr);
        vkDestroyShaderModule(device_, *fragModule, nullptr);
        return false;
    }
    branchCapturePipeline_ = ManagedPipeline(makeUniquePipeline(device_, pipeline));

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    return true;
}

bool TreeImpostorAtlas::createLeafCapturePipeline() {
    // Create descriptor set layout for leaf capture (includes SSBO for leaf instances)
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {
        // Albedo texture
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Normal texture (unused for leaves but kept for compatibility)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        // Leaf instance SSBO
        vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device_, reinterpret_cast<const VkDescriptorSetLayoutCreateInfo*>(&layoutInfo), nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    leafCaptureDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, descriptorSetLayout));

    // Create pipeline layout with push constants (includes firstInstance for leaf offset)
    auto pushConstant = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(glm::mat4) * 2 + sizeof(glm::vec4) + sizeof(int32_t));  // viewProj, model, captureParams, firstInstance

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstant);

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    leafCapturePipelineLayout_ = ManagedPipelineLayout(makeUniquePipelineLayout(device_, pipelineLayout));

    // Load shaders
    std::string shaderPath = resourcePath_ + "/shaders/";
    auto vertModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture_leaf.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(device_, shaderPath + "tree_impostor_capture.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to load leaf capture shaders");
        return false;
    }

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

    // Vertex input (same as branch capture - position, normal, texcoord)
    std::array<vk::VertexInputBindingDescription, 1> bindingDescriptions = {
        vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(Vertex))
            .setInputRate(vk::VertexInputRate::eVertex)
    };

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {
        vk::VertexInputAttributeDescription{}.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, position)),
        vk::VertexInputAttributeDescription{}.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription{}.setLocation(2).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(offsetof(Vertex, texCoord))
    };

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDescriptions)
        .setVertexAttributeDescriptions(attributeDescriptions);

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    std::array<vk::PipelineColorBlendAttachmentState, 2> colorBlendAttachments = {
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(false),
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(false)
    };

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachments(colorBlendAttachments);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

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
        .setLayout(pipelineLayout)
        .setRenderPass(captureRenderPass_.get())
        .setSubpass(0);

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, reinterpret_cast<const VkGraphicsPipelineCreateInfo*>(&pipelineInfo), nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, *vertModule, nullptr);
        vkDestroyShaderModule(device_, *fragModule, nullptr);
        return false;
    }
    leafCapturePipeline_ = ManagedPipeline(makeUniquePipeline(device_, pipeline));

    vkDestroyShaderModule(device_, *vertModule, nullptr);
    vkDestroyShaderModule(device_, *fragModule, nullptr);

    SDL_Log("TreeImpostorAtlas: Created leaf capture pipeline");
    return true;
}

bool TreeImpostorAtlas::createLeafQuadMesh() {
    // Create a simple quad mesh for leaf rendering (same as TreeSystem's shared quad)
    std::array<Vertex, 4> vertices = {{
        {glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f)},  // Bottom-left
        {glm::vec3( 0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f)},  // Bottom-right
        {glm::vec3( 0.5f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f)},  // Top-right
        {glm::vec3(-0.5f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f)},  // Top-left
    }};

    std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};
    leafQuadIndexCount_ = 6;

    VkDeviceSize vertexSize = sizeof(vertices);
    VkDeviceSize indexSize = sizeof(indices);
    VkDeviceSize stagingSize = vertexSize + indexSize;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    auto stagingInfo = vk::BufferCreateInfo{}
        .setSize(stagingSize)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc);

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo), &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexSize);
    memcpy(static_cast<char*>(data) + vertexSize, indices.data(), indexSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Create GPU buffers
    auto vertexBufferInfo = vk::BufferCreateInfo{}
        .setSize(vertexSize)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&vertexBufferInfo), &gpuAllocInfo, &leafQuadVertexBuffer_, &leafQuadVertexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    auto indexBufferInfo = vk::BufferCreateInfo{}
        .setSize(indexSize)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&indexBufferInfo), &gpuAllocInfo, &leafQuadIndexBuffer_, &leafQuadIndexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    // Copy to GPU
    auto cmdAllocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(commandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, reinterpret_cast<const VkCommandBufferAllocateInfo*>(&cmdAllocInfo), &cmd);

    auto beginInfo = vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    vkBeginCommandBuffer(cmd, reinterpret_cast<const VkCommandBufferBeginInfo*>(&beginInfo));

    VkBufferCopy vertexCopy{0, 0, vertexSize};
    vkCmdCopyBuffer(cmd, stagingBuffer, leafQuadVertexBuffer_, 1, &vertexCopy);

    VkBufferCopy indexCopy{vertexSize, 0, indexSize};
    vkCmdCopyBuffer(cmd, stagingBuffer, leafQuadIndexBuffer_, 1, &indexCopy);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    SDL_Log("TreeImpostorAtlas: Created leaf quad mesh");
    return true;
}

bool TreeImpostorAtlas::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 4.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        return false;
    }
    atlasSampler_ = ManagedSampler(makeUniqueSampler(device_, sampler));

    return true;
}

int32_t TreeImpostorAtlas::generateArchetype(
    const std::string& name,
    const TreeOptions& options,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    VkImageView barkAlbedo,
    VkImageView barkNormal,
    VkImageView leafAlbedo,
    VkSampler sampler) {

    uint32_t archetypeIndex = static_cast<uint32_t>(archetypes_.size());

    // Create atlas resources for this archetype
    if (!createAtlasResources(archetypeIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create atlas resources for %s", name.c_str());
        return -1;
    }

    // Calculate bounding box from mesh and leaves
    glm::vec3 minBounds(FLT_MAX);
    glm::vec3 maxBounds(-FLT_MAX);

    for (const auto& vertex : branchMesh.getVertices()) {
        minBounds = glm::min(minBounds, vertex.position);
        maxBounds = glm::max(maxBounds, vertex.position);
    }

    // Include leaves in bounding calculation
    for (const auto& leaf : leafInstances) {
        glm::vec3 leafPos = glm::vec3(leaf.positionAndSize);
        float leafSize = leaf.positionAndSize.w;
        minBounds = glm::min(minBounds, leafPos - glm::vec3(leafSize));
        maxBounds = glm::max(maxBounds, leafPos + glm::vec3(leafSize));
    }

    // Calculate tree center and dimensions
    glm::vec3 treeCenter = (minBounds + maxBounds) * 0.5f;
    glm::vec3 treeExtent = maxBounds - minBounds;
    // Horizontal radius is max of X and Z extents (not Y - that's vertical)
    float horizontalRadius = glm::max(treeExtent.x, treeExtent.z) * 0.5f;
    // For depth clipping, use the full 3D bounding sphere radius
    float boundingSphereRadius = glm::length(treeExtent) * 0.5f;
    float centerHeight = treeCenter.y;  // Height of tree center above origin
    float halfHeight = treeExtent.y * 0.5f;

    SDL_Log("TreeImpostorAtlas: Tree bounds X=[%.2f, %.2f], Y=[%.2f, %.2f], Z=[%.2f, %.2f]",
            minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, minBounds.z, maxBounds.z);
    SDL_Log("TreeImpostorAtlas: horizontalRadius=%.2f, halfHeight=%.2f, boundingSphere=%.2f",
            horizontalRadius, halfHeight, boundingSphereRadius);

    // Upload leaf instances to buffer if we have any
    VkDescriptorSet leafCaptureDescSet = VK_NULL_HANDLE;
    if (!leafInstances.empty()) {
        VkDeviceSize requiredSize = leafInstances.size() * sizeof(LeafInstanceGPU);

        // Resize buffer if needed
        if (requiredSize > leafCaptureBufferSize_) {
            if (leafCaptureBuffer_ != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, leafCaptureBuffer_, leafCaptureAllocation_);
            }

            auto bufferInfo = vk::BufferCreateInfo{}
                .setSize(requiredSize)
                .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)
                .setSharingMode(vk::SharingMode::eExclusive);

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

            if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &leafCaptureBuffer_, &leafCaptureAllocation_, nullptr) != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create leaf capture buffer");
                return -1;
            }
            leafCaptureBufferSize_ = requiredSize;
        }

        // Upload leaf instances
        void* data;
        vmaMapMemory(allocator_, leafCaptureAllocation_, &data);
        memcpy(data, leafInstances.data(), requiredSize);
        vmaUnmapMemory(allocator_, leafCaptureAllocation_);

        // Allocate leaf capture descriptor set
        leafCaptureDescSet = descriptorPool_->allocateSingle(leafCaptureDescriptorSetLayout_.get());
        if (leafCaptureDescSet != VK_NULL_HANDLE) {
            // Update leaf capture descriptor set
            VkDescriptorImageInfo leafImageInfo{};
            leafImageInfo.sampler = sampler;
            leafImageInfo.imageView = leafAlbedo;
            leafImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorBufferInfo ssboInfo{};
            ssboInfo.buffer = leafCaptureBuffer_;
            ssboInfo.offset = 0;
            ssboInfo.range = VK_WHOLE_SIZE;

            std::array<VkWriteDescriptorSet, 3> leafWrites{};
            leafWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            leafWrites[0].dstSet = leafCaptureDescSet;
            leafWrites[0].dstBinding = 0;
            leafWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            leafWrites[0].descriptorCount = 1;
            leafWrites[0].pImageInfo = &leafImageInfo;

            // Binding 1: use bark normal as placeholder (required by layout)
            VkDescriptorImageInfo normalInfo{};
            normalInfo.sampler = sampler;
            normalInfo.imageView = barkNormal;
            normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            leafWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            leafWrites[1].dstSet = leafCaptureDescSet;
            leafWrites[1].dstBinding = 1;
            leafWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            leafWrites[1].descriptorCount = 1;
            leafWrites[1].pImageInfo = &normalInfo;

            leafWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            leafWrites[2].dstSet = leafCaptureDescSet;
            leafWrites[2].dstBinding = 2;
            leafWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            leafWrites[2].descriptorCount = 1;
            leafWrites[2].pBufferInfo = &ssboInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(leafWrites.size()), leafWrites.data(), 0, nullptr);
        }
    }

    // Allocate descriptor set for branch capture
    VkDescriptorSet captureDescSet = descriptorPool_->allocateSingle(captureDescriptorSetLayout_.get());
    if (captureDescSet == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to allocate descriptor set");
        return -1;
    }

    // Update descriptor set with bark textures
    std::array<VkDescriptorImageInfo, 2> imageInfos{};
    imageInfos[0].sampler = sampler;
    imageInfos[0].imageView = barkAlbedo;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = sampler;
    imageInfos[1].imageView = barkNormal;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = captureDescSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfos[0];

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = captureDescSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfos[1];

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Begin command buffer for capture
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition the array layer to color attachment for rendering
    VkImageMemoryBarrier preBarrier{};
    preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    preBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    preBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preBarrier.subresourceRange.baseMipLevel = 0;
    preBarrier.subresourceRange.levelCount = 1;
    preBarrier.subresourceRange.baseArrayLayer = archetypeIndex;
    preBarrier.subresourceRange.layerCount = 1;
    preBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    preBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Batch both image barriers into a single call
    std::array<VkImageMemoryBarrier, 2> preBarriers = {preBarrier, preBarrier};
    preBarriers[0].image = octaAlbedoArrayImage_;
    preBarriers[1].image = octaNormalArrayImage_;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(preBarriers.size()), preBarriers.data());

    // Clear the atlas
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Transparent
    clearValues[1].color = {{0.5f, 0.5f, 0.5f, 1.0f}};  // Neutral normal, mid depth, full AO
    clearValues[2].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = captureRenderPass_.get();
    renderPassInfo.framebuffer = atlasTextures_[archetypeIndex].framebuffer.get();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Render from each octahedral view direction
    int cellCount = 0;
    for (int y = 0; y < OctahedralAtlasConfig::GRID_SIZE; y++) {
        for (int x = 0; x < OctahedralAtlasConfig::GRID_SIZE; x++) {
            // Compute view direction for this cell using hemi-octahedral decode
            glm::vec2 cellCenter = (glm::vec2(x, y) + 0.5f) / static_cast<float>(OctahedralAtlasConfig::GRID_SIZE);
            glm::vec3 viewDir = hemiOctaDecode(cellCenter);

            // Render tree from this direction
            renderOctahedralCell(cmd, x, y, viewDir, branchMesh, leafInstances,
                                 horizontalRadius, boundingSphereRadius, halfHeight,
                                 centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
            cellCount++;
        }
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    // Store archetype info
    TreeImpostorArchetype archetype;
    archetype.name = name;
    archetype.treeType = options.bark.type;
    archetype.boundingSphereRadius = horizontalRadius;  // Used for billboard sizing
    archetype.centerHeight = centerHeight;
    archetype.treeHeight = treeExtent.y;
    archetype.baseOffset = minBounds.y;
    archetype.albedoAlphaView = atlasTextures_[archetypeIndex].albedoView.get();
    archetype.normalDepthAOView = atlasTextures_[archetypeIndex].normalView.get();
    archetype.atlasIndex = archetypeIndex;

    archetypes_.push_back(archetype);

    // Note: Preview descriptor set is created lazily in getPreviewDescriptorSet()
    // because ImGui may not be initialized yet at this point

    SDL_Log("TreeImpostorAtlas: Generated archetype '%s' (%dx%d grid = %d views, hRadius=%.2f, height=%.2f)",
            name.c_str(), OctahedralAtlasConfig::GRID_SIZE, OctahedralAtlasConfig::GRID_SIZE, cellCount,
            horizontalRadius, treeExtent.y);

    return static_cast<int32_t>(archetypeIndex);
}

void TreeImpostorAtlas::renderOctahedralCell(
    VkCommandBuffer cmd,
    int cellX, int cellY,
    glm::vec3 viewDirection,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    float horizontalRadius,
    float boundingSphereRadius,
    float halfHeight,
    float centerHeight,
    float baseY,
    VkDescriptorSet branchDescSet,
    VkDescriptorSet leafDescSet) {

    // Set viewport and scissor for this cell
    VkViewport viewport{};
    viewport.x = static_cast<float>(cellX * OctahedralAtlasConfig::CELL_SIZE);
    viewport.y = static_cast<float>(cellY * OctahedralAtlasConfig::CELL_SIZE);
    viewport.width = static_cast<float>(OctahedralAtlasConfig::CELL_SIZE);
    viewport.height = static_cast<float>(OctahedralAtlasConfig::CELL_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {cellX * OctahedralAtlasConfig::CELL_SIZE, cellY * OctahedralAtlasConfig::CELL_SIZE};
    scissor.extent = {static_cast<uint32_t>(OctahedralAtlasConfig::CELL_SIZE),
                      static_cast<uint32_t>(OctahedralAtlasConfig::CELL_SIZE)};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Compute camera position from view direction
    // viewDirection is the direction FROM tree TO camera (normalized)
    float camDist = boundingSphereRadius * 3.0f;
    glm::vec3 target(0.0f, centerHeight, 0.0f);
    glm::vec3 camPos = target + viewDirection * camDist;

    // Compute up vector - avoid degenerate case when looking straight down
    float elevation = glm::degrees(glm::asin(glm::clamp(viewDirection.y, -1.0f, 1.0f)));
    glm::vec3 up = (elevation > 80.0f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(camPos, target, up);

    // Orthographic projection that encompasses the tree from the current view angle
    // For elevated views, the tree's depth contributes to projected width
    // Use squared elevation factor for gentler blending - most views stay close to horizontalRadius
    float elevationFactor = glm::abs(elevation) / 90.0f;  // 0 at horizon, 1 at top-down
    float blendFactor = elevationFactor * elevationFactor;  // Quadratic: stays low until high elevations

    // Horizontal size: blend from horizontalRadius toward bounding sphere at steep angles
    float effectiveHSize = glm::mix(horizontalRadius, boundingSphereRadius, blendFactor) * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
    // Vertical size: use half-height with margin
    float effectiveVSize = halfHeight * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
    // Use the larger of the two for a square projection (simpler billboard math)
    float projSize = glm::max(effectiveHSize, effectiveVSize);

    glm::mat4 proj;
    // Symmetric projection centered on tree center
    proj = glm::ortho(-projSize, projSize, -projSize, projSize, 0.1f, camDist + boundingSphereRadius * 2.0f);

    // Vulkan clip space correction
    proj[1][1] *= -1;
    proj[3][1] *= -1;

    glm::mat4 viewProj = proj * view;

    // ===== DRAW BRANCHES =====
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchCapturePipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, capturePipelineLayout_.get(),
                           0, 1, &branchDescSet, 0, nullptr);

    VkBuffer branchVertexBuffers[] = {branchMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, branchVertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    struct {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 captureParams;
    } branchPush;

    branchPush.viewProj = viewProj;
    branchPush.model = glm::mat4(1.0f);
    branchPush.captureParams = glm::vec4(
        static_cast<float>(cellX + cellY * OctahedralAtlasConfig::GRID_SIZE),
        0.0f,  // is leaf pass = false
        boundingSphereRadius,
        0.1f   // alpha test
    );

    vkCmdPushConstants(cmd, capturePipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(branchPush), &branchPush);

    vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);

    // ===== DRAW LEAVES =====
    if (leafDescSet != VK_NULL_HANDLE && !leafInstances.empty() && leafQuadIndexCount_ > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafCapturePipeline_.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafCapturePipelineLayout_.get(),
                               0, 1, &leafDescSet, 0, nullptr);

        VkBuffer leafVertexBuffers[] = {leafQuadVertexBuffer_};
        vkCmdBindVertexBuffers(cmd, 0, 1, leafVertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, leafQuadIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        struct {
            glm::mat4 viewProj;
            glm::mat4 model;
            glm::vec4 captureParams;
            int32_t firstInstance;
        } leafPush;

        leafPush.viewProj = viewProj;
        leafPush.model = glm::mat4(1.0f);
        leafPush.captureParams = glm::vec4(
            static_cast<float>(cellX + cellY * OctahedralAtlasConfig::GRID_SIZE),
            1.0f,  // is leaf pass = true
            boundingSphereRadius,
            0.3f   // alpha test for leaves
        );
        leafPush.firstInstance = 0;

        vkCmdPushConstants(cmd, leafCapturePipelineLayout_.get(),
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(leafPush), &leafPush);

        vkCmdDrawIndexed(cmd, leafQuadIndexCount_, static_cast<uint32_t>(leafInstances.size()), 0, 0, 0);
    }
}

const TreeImpostorArchetype* TreeImpostorAtlas::getArchetype(const std::string& name) const {
    for (const auto& archetype : archetypes_) {
        if (archetype.name == name) {
            return &archetype;
        }
    }
    return nullptr;
}

const TreeImpostorArchetype* TreeImpostorAtlas::getArchetype(uint32_t index) const {
    if (index < archetypes_.size()) {
        return &archetypes_[index];
    }
    return nullptr;
}

VkDescriptorSet TreeImpostorAtlas::getPreviewDescriptorSet(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        return VK_NULL_HANDLE;
    }

    // Lazy initialization: create ImGui descriptor set on first request
    // (ImGui must be initialized by this point, which happens after renderer init)
    if (atlasTextures_[archetypeIndex].previewDescriptorSet == VK_NULL_HANDLE) {
        if (atlasTextures_[archetypeIndex].albedoView.get() != VK_NULL_HANDLE) {
            atlasTextures_[archetypeIndex].previewDescriptorSet = ImGui_ImplVulkan_AddTexture(
                atlasSampler_.get(),
                atlasTextures_[archetypeIndex].albedoView.get(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }

    return atlasTextures_[archetypeIndex].previewDescriptorSet;
}
