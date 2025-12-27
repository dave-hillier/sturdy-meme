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
    std::array<VkAttachmentDescription, 3> attachments{};

    // Albedo + Alpha attachment (RGBA8)
    // Note: We pre-transition array layers to COLOR_ATTACHMENT_OPTIMAL before the render pass
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal + Depth + AO attachment (RGBA8)
    attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Depth attachment
    attachments[2].format = VK_FORMAT_D32_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 2> colorRefs{};
    colorRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depthRef{};
    depthRef.attachment = 2;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        return false;
    }
    captureRenderPass_ = ManagedRenderPass(makeUniqueRenderPass(device_, renderPass));

    return true;
}

bool TreeImpostorAtlas::createAtlasArrayTextures() {
    // Create octahedral array textures that will hold all archetypes
    // Each layer is one archetype's atlas (GRID_SIZE x GRID_SIZE cells)

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = OctahedralAtlasConfig::ATLAS_WIDTH;
    imageInfo.extent.height = OctahedralAtlasConfig::ATLAS_HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = maxArchetypes_;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create octahedral albedo+alpha array
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &octaAlbedoArrayImage_, &octaAlbedoArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array image");
        return false;
    }

    // Create octahedral normal+depth+AO array
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &octaNormalArrayImage_, &octaNormalArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array image");
        return false;
    }

    // Create image views for the entire arrays
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = maxArchetypes_;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.image = octaAlbedoArrayImage_;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array view");
        return false;
    }
    octaAlbedoArrayView_ = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = octaNormalArrayImage_;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array view");
        return false;
    }
    octaNormalArrayView_ = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Transition both array images to shader read optimal layout
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

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = maxArchetypes_;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // Batch both image barriers into a single call
    std::array<VkImageMemoryBarrier, 2> barriers = {barrier, barrier};
    barriers[0].image = octaAlbedoArrayImage_;
    barriers[1].image = octaNormalArrayImage_;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());

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
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = archetypeIndex;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.image = octaAlbedoArrayImage_;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create albedo layer view");
        return false;
    }
    atlas.albedoView = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = octaNormalArrayImage_;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create normal layer view");
        return false;
    }
    atlas.normalView = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Create depth image
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = OctahedralAtlasConfig::ATLAS_WIDTH;
    depthImageInfo.extent.height = OctahedralAtlasConfig::ATLAS_HEIGHT;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, &depthImageInfo, &allocInfo,
                       &atlas.depthImage, &atlas.depthAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create depth image");
        return false;
    }

    viewInfo.image = atlas.depthImage;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    VkImageView depthView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
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
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Albedo texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Normal texture (for AO extraction)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    captureDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, descriptorSetLayout));

    // Create pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4);  // viewProj, model, captureParams

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

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

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragModule;
    shaderStages[1].pName = "main";

    // Vertex input (position, normal, texcoord)
    std::array<VkVertexInputBindingDescription, 1> bindingDescriptions{};
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(OctahedralAtlasConfig::CELL_SIZE);
    viewport.height = static_cast<float>(OctahedralAtlasConfig::CELL_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {OctahedralAtlasConfig::CELL_SIZE, OctahedralAtlasConfig::CELL_SIZE};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for capture
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Two color blend attachments (both write all channels)
    std::array<VkPipelineColorBlendAttachmentState, 2> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic viewport and scissor for rendering to different cells
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = captureRenderPass_.get();
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
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
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Albedo texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Normal texture (unused for leaves but kept for compatibility)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Leaf instance SSBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    leafCaptureDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, descriptorSetLayout));

    // Create pipeline layout with push constants (includes firstInstance for leaf offset)
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4) + sizeof(int32_t);  // viewProj, model, captureParams, firstInstance

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

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

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragModule;
    shaderStages[1].pName = "main";

    // Vertex input (same as branch capture - position, normal, texcoord)
    std::array<VkVertexInputBindingDescription, 1> bindingDescriptions{};
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 2> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = captureRenderPass_.get();
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
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
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = stagingSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexSize);
    memcpy(static_cast<char*>(data) + vertexSize, indices.data(), indexSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Create GPU buffers
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    bufferInfo.size = vertexSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &gpuAllocInfo, &leafQuadVertexBuffer_, &leafQuadVertexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    bufferInfo.size = indexSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &gpuAllocInfo, &leafQuadIndexBuffer_, &leafQuadIndexAllocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    // Copy to GPU
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

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = requiredSize;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

            if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &leafCaptureBuffer_, &leafCaptureAllocation_, nullptr) != VK_SUCCESS) {
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
