#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "core/BufferUtils.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
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

    // Cleanup legacy array textures
    if (albedoArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, albedoArrayImage_, albedoArrayAllocation_);
    }
    if (normalArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, normalArrayImage_, normalArrayAllocation_);
    }

    // Cleanup octahedral array textures
    if (octAlbedoArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octAlbedoArrayImage_, octAlbedoArrayAllocation_);
    }
    if (octNormalArrayImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, octNormalArrayImage_, octNormalArrayAllocation_);
    }

    // Cleanup per-archetype atlas textures (depth buffers and framebuffers)
    // Note: Don't call ImGui_ImplVulkan_RemoveTexture here - ImGui may already
    // be shut down. ImGui cleans up its own descriptor pool on shutdown anyway.
    for (auto& atlas : atlasTextures_) {
        if (atlas.depthImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, atlas.depthImage, atlas.depthAllocation);
        }
    }

    // Cleanup per-archetype octahedral atlas textures
    for (auto& atlas : octAtlasTextures_) {
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

    if (!createOctahedralAtlasArrayTextures()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral atlas array textures");
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
    // Create array textures that will hold all archetypes
    // Each layer is one archetype's atlas (2304 x 512)

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = ImpostorAtlasConfig::ATLAS_WIDTH;
    imageInfo.extent.height = ImpostorAtlasConfig::ATLAS_HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = maxArchetypes_;  // Pre-allocate for all archetypes
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create albedo+alpha array
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &albedoArrayImage_, &albedoArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create albedo array image");
        return false;
    }

    // Create normal+depth+AO array
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &normalArrayImage_, &normalArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create normal array image");
        return false;
    }

    // Create image views for the entire arrays (for shader binding)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = maxArchetypes_;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.image = albedoArrayImage_;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create albedo array view");
        return false;
    }
    albedoArrayView_ = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = normalArrayImage_;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create normal array view");
        return false;
    }
    normalArrayView_ = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Transition both array images to shader read optimal layout
    // (They'll be transitioned to attachment when rendering each layer)
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

    barrier.image = albedoArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.image = normalArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    SDL_Log("TreeImpostorAtlas: Created array textures (%dx%d, %d layers)",
            ImpostorAtlasConfig::ATLAS_WIDTH, ImpostorAtlasConfig::ATLAS_HEIGHT, maxArchetypes_);

    return true;
}

bool TreeImpostorAtlas::createOctahedralAtlasArrayTextures() {
    // Create octahedral array textures (512x512 per archetype)

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
                       &octAlbedoArrayImage_, &octAlbedoArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array");
        return false;
    }

    // Create octahedral normal+depth+AO array
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &octNormalArrayImage_, &octNormalArrayAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array");
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

    viewInfo.image = octAlbedoArrayImage_;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo array view");
        return false;
    }
    octAlbedoArrayView_ = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = octNormalArrayImage_;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal array view");
        return false;
    }
    octNormalArrayView_ = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Transition to shader read layout
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

    barrier.image = octAlbedoArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.image = octNormalArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    SDL_Log("TreeImpostorAtlas: Created octahedral array textures (%dx%d, %d layers)",
            OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT, maxArchetypes_);

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
    viewport.width = static_cast<float>(ImpostorAtlasConfig::CELL_SIZE);
    viewport.height = static_cast<float>(ImpostorAtlasConfig::CELL_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {ImpostorAtlasConfig::CELL_SIZE, ImpostorAtlasConfig::CELL_SIZE};

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

bool TreeImpostorAtlas::createAtlasResources(uint32_t archetypeIndex) {
    // Ensure we have space for this archetype
    if (archetypeIndex >= atlasTextures_.size()) {
        atlasTextures_.resize(archetypeIndex + 1);
    }

    if (archetypeIndex >= maxArchetypes_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TreeImpostorAtlas: Archetype index %u exceeds max %u", archetypeIndex, maxArchetypes_);
        return false;
    }

    auto& atlas = atlasTextures_[archetypeIndex];

    // Create per-layer views into the shared array textures (for framebuffer attachment)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // Single layer view for framebuffer
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = archetypeIndex;  // This layer
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.image = albedoArrayImage_;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create albedo layer view");
        return false;
    }
    atlas.albedoAlphaView = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = normalArrayImage_;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create normal layer view");
        return false;
    }
    atlas.normalDepthAOView = ManagedImageView(makeUniqueImageView(device_, normalView));

    // Create depth image (not shared, one per archetype for rendering)
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = ImpostorAtlasConfig::ATLAS_WIDTH;
    depthImageInfo.extent.height = ImpostorAtlasConfig::ATLAS_HEIGHT;
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
    viewInfo.subresourceRange.baseArrayLayer = 0;  // Depth is not shared
    VkImageView depthView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create depth view");
        return false;
    }
    atlas.depthView = ManagedImageView(makeUniqueImageView(device_, depthView));

    // Create framebuffer
    std::array<VkImageView, 3> attachments = {
        atlas.albedoAlphaView.get(),
        atlas.normalDepthAOView.get(),
        atlas.depthView.get()
    };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = captureRenderPass_.get();
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = ImpostorAtlasConfig::ATLAS_WIDTH;
    fbInfo.height = ImpostorAtlasConfig::ATLAS_HEIGHT;
    fbInfo.layers = 1;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        return false;
    }
    atlas.framebuffer = ManagedFramebuffer(makeUniqueFramebuffer(device_, framebuffer));

    return true;
}

bool TreeImpostorAtlas::createOctahedralAtlasResources(uint32_t archetypeIndex) {
    // Ensure we have space for this archetype
    if (archetypeIndex >= octAtlasTextures_.size()) {
        octAtlasTextures_.resize(archetypeIndex + 1);
    }

    if (archetypeIndex >= maxArchetypes_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TreeImpostorAtlas: Archetype index %u exceeds max %u", archetypeIndex, maxArchetypes_);
        return false;
    }

    auto& atlas = octAtlasTextures_[archetypeIndex];

    // Create per-layer views into the shared octahedral array textures
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = archetypeIndex;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    // Create per-layer views for the framebuffer - these must remain valid for framebuffer lifetime
    VkImageView albedoLayerView, normalLayerView;

    viewInfo.image = octAlbedoArrayImage_;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoLayerView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral albedo layer view");
        return false;
    }
    atlas.albedoLayerView = ManagedImageView(makeUniqueImageView(device_, albedoLayerView));

    viewInfo.image = octNormalArrayImage_;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalLayerView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral normal layer view");
        return false;
    }
    atlas.normalLayerView = ManagedImageView(makeUniqueImageView(device_, normalLayerView));

    // Create depth image for octahedral rendering
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral depth image");
        return false;
    }

    viewInfo.image = atlas.depthImage;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    VkImageView depthView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral depth view");
        return false;
    }
    atlas.depthView = ManagedImageView(makeUniqueImageView(device_, depthView));

    // Create framebuffer for octahedral atlas rendering
    std::array<VkImageView, 3> attachments = {
        atlas.albedoLayerView.get(),
        atlas.normalLayerView.get(),
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral framebuffer");
        return false;
    }
    atlas.framebuffer = ManagedFramebuffer(makeUniqueFramebuffer(device_, framebuffer));

    // Note: Layer views are stored in atlas struct and must remain valid for framebuffer lifetime
    return true;
}

glm::vec2 TreeImpostorAtlas::octahedralEncode(glm::vec3 dir) {
    // Project onto octahedron: |x| + |y| + |z| = 1
    dir /= (std::abs(dir.x) + std::abs(dir.y) + std::abs(dir.z));

    // For lower hemisphere (Y < 0), fold onto upper hemisphere
    if (dir.y < 0.0f) {
        float signX = dir.x >= 0.0f ? 1.0f : -1.0f;
        float signZ = dir.z >= 0.0f ? 1.0f : -1.0f;
        float newX = (1.0f - std::abs(dir.z)) * signX;
        float newZ = (1.0f - std::abs(dir.x)) * signZ;
        dir.x = newX;
        dir.z = newZ;
    }

    // Map from [-1,1] to [0,1]
    return glm::vec2(dir.x * 0.5f + 0.5f, dir.z * 0.5f + 0.5f);
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

    // Create legacy atlas resources for this archetype
    if (!createAtlasResources(archetypeIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create atlas resources for %s", name.c_str());
        return -1;
    }

    // Create octahedral atlas resources
    if (!createOctahedralAtlasResources(archetypeIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create octahedral atlas resources for %s", name.c_str());
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

    // Transition the specific array layer to color attachment for rendering
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

    preBarrier.image = albedoArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &preBarrier);

    preBarrier.image = normalArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &preBarrier);

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
    renderPassInfo.renderArea.extent = {ImpostorAtlasConfig::ATLAS_WIDTH, ImpostorAtlasConfig::ATLAS_HEIGHT};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchCapturePipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, capturePipelineLayout_.get(),
                           0, 1, &captureDescSet, 0, nullptr);

    // Bind mesh buffers
    VkBuffer vertexBuffers[] = {branchMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // Render from each viewing angle
    int cellIndex = 0;

    // Row 0: 8 horizon views + 1 top-down
    for (int h = 0; h < ImpostorAtlasConfig::HORIZONTAL_ANGLES; h++) {
        float azimuth = h * (360.0f / ImpostorAtlasConfig::HORIZONTAL_ANGLES);
        renderToCell(cmd, h, 0, azimuth, 0.0f, branchMesh, leafInstances,
                     horizontalRadius, boundingSphereRadius, halfHeight,
                     centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
        cellIndex++;
    }

    // Top-down view (cell 8 of row 0)
    renderToCell(cmd, 8, 0, 0.0f, 90.0f, branchMesh, leafInstances,
                 horizontalRadius, boundingSphereRadius, halfHeight,
                 centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
    cellIndex++;

    // Row 1: 8 elevated views (45 degrees)
    for (int h = 0; h < ImpostorAtlasConfig::HORIZONTAL_ANGLES; h++) {
        float azimuth = h * (360.0f / ImpostorAtlasConfig::HORIZONTAL_ANGLES);
        renderToCell(cmd, h, 1, azimuth, 45.0f, branchMesh, leafInstances,
                     horizontalRadius, boundingSphereRadius, halfHeight,
                     centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
        cellIndex++;
    }

    vkCmdEndRenderPass(cmd);

    // ===== GENERATE OCTAHEDRAL ATLAS =====
    // Transition octahedral array layer to color attachment
    VkImageMemoryBarrier octPreBarrier{};
    octPreBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    octPreBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    octPreBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    octPreBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    octPreBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    octPreBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    octPreBarrier.subresourceRange.baseMipLevel = 0;
    octPreBarrier.subresourceRange.levelCount = 1;
    octPreBarrier.subresourceRange.baseArrayLayer = archetypeIndex;
    octPreBarrier.subresourceRange.layerCount = 1;
    octPreBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    octPreBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    octPreBarrier.image = octAlbedoArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &octPreBarrier);

    octPreBarrier.image = octNormalArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &octPreBarrier);

    // Begin octahedral render pass
    VkRenderPassBeginInfo octRenderPassInfo{};
    octRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    octRenderPassInfo.renderPass = captureRenderPass_.get();
    octRenderPassInfo.framebuffer = octAtlasTextures_[archetypeIndex].framebuffer.get();
    octRenderPassInfo.renderArea.offset = {0, 0};
    octRenderPassInfo.renderArea.extent = {OctahedralAtlasConfig::ATLAS_WIDTH, OctahedralAtlasConfig::ATLAS_HEIGHT};
    octRenderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    octRenderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &octRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchCapturePipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, capturePipelineLayout_.get(),
                           0, 1, &captureDescSet, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // For octahedral atlas, we render multiple views covering the hemisphere
    // Each view is rendered to a portion of the 512x512 atlas based on its octahedral UV
    // We use a grid of views and let bilinear filtering interpolate at runtime
    const int OCT_AZIMUTH_STEPS = 16;   // 16 horizontal angles (22.5 degrees each)
    const int OCT_ELEVATION_STEPS = 8;  // 8 elevation levels from horizon to top-down

    for (int elev = 0; elev < OCT_ELEVATION_STEPS; elev++) {
        // Elevation from 0 (horizon) to 90 (top-down)
        float elevation = 90.0f * static_cast<float>(elev) / static_cast<float>(OCT_ELEVATION_STEPS - 1);

        for (int az = 0; az < OCT_AZIMUTH_STEPS; az++) {
            float azimuth = 360.0f * static_cast<float>(az) / static_cast<float>(OCT_AZIMUTH_STEPS);

            renderToOctahedral(cmd, azimuth, elevation, branchMesh, leafInstances,
                              horizontalRadius, boundingSphereRadius, halfHeight,
                              centerHeight, minBounds.y, captureDescSet, leafCaptureDescSet);
        }
    }

    vkCmdEndRenderPass(cmd);

    // Transition octahedral array layer back to shader read
    VkImageMemoryBarrier octPostBarrier = octPreBarrier;
    octPostBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    octPostBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    octPostBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    octPostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    octPostBarrier.image = octAlbedoArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &octPostBarrier);

    octPostBarrier.image = octNormalArrayImage_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &octPostBarrier);

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
    archetype.albedoAlphaView = atlasTextures_[archetypeIndex].albedoAlphaView.get();
    archetype.normalDepthAOView = atlasTextures_[archetypeIndex].normalDepthAOView.get();
    archetype.atlasIndex = archetypeIndex;

    archetypes_.push_back(archetype);

    // Note: Preview descriptor set is created lazily in getPreviewDescriptorSet()
    // because ImGui may not be initialized yet at this point

    SDL_Log("TreeImpostorAtlas: Generated archetype '%s' (hRadius=%.2f, height=%.2f, baseOffset=%.2f, %d cells)",
            name.c_str(), horizontalRadius, treeExtent.y, minBounds.y, cellIndex);

    return static_cast<int32_t>(archetypeIndex);
}

void TreeImpostorAtlas::renderToCell(
    VkCommandBuffer cmd,
    int cellX, int cellY,
    float azimuth,
    float elevation,
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
    viewport.x = static_cast<float>(cellX * ImpostorAtlasConfig::CELL_SIZE);
    viewport.y = static_cast<float>(cellY * ImpostorAtlasConfig::CELL_SIZE);
    viewport.width = static_cast<float>(ImpostorAtlasConfig::CELL_SIZE);
    viewport.height = static_cast<float>(ImpostorAtlasConfig::CELL_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {cellX * ImpostorAtlasConfig::CELL_SIZE, cellY * ImpostorAtlasConfig::CELL_SIZE};
    scissor.extent = {static_cast<uint32_t>(ImpostorAtlasConfig::CELL_SIZE),
                      static_cast<uint32_t>(ImpostorAtlasConfig::CELL_SIZE)};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Calculate camera position for this view angle
    // Camera looks at tree center (at centerHeight)
    float azimuthRad = glm::radians(azimuth);
    float elevationRad = glm::radians(elevation);

    // Use bounding sphere for camera distance and depth clipping to ensure no geometry is clipped
    float camDist = boundingSphereRadius * 3.0f;
    glm::vec3 target(0.0f, centerHeight, 0.0f);  // Look at actual tree center
    glm::vec3 camPos(
        camDist * cos(elevationRad) * sin(azimuthRad),
        centerHeight + camDist * sin(elevationRad),
        camDist * cos(elevationRad) * cos(azimuthRad)
    );
    // For top-down view (elevation ~90), up vector (0,1,0) is parallel to view direction
    // which makes lookAt degenerate. Use a horizontal up vector instead.
    glm::vec3 up = (elevation > 80.0f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(camPos, target, up);

    // Orthographic projection: use horizontalRadius for horizontal extent, halfHeight for vertical
    // This gives proper framing for the tree's actual shape
    float hSize = horizontalRadius * 1.1f;  // Horizontal extent with small margin
    float vSize = halfHeight * 1.1f;        // Vertical extent with small margin

    glm::mat4 proj;
    if (elevation < 80.0f) {
        // For horizon and elevated views: place tree base at bottom of cell
        // In view space, Y coordinate depends on viewing elevation:
        // view.y = (worldY - centerHeight) * cos(elevation)
        float elevCos = cos(elevationRad);
        float baseInViewSpace = (baseY - centerHeight) * elevCos;
        float yBottom = baseInViewSpace;
        float yTop = yBottom + 2.0f * vSize * elevCos;
        // Use boundingSphereRadius for near/far to ensure no depth clipping
        proj = glm::ortho(-hSize, hSize, yBottom, yTop, 0.1f, camDist + boundingSphereRadius * 2.0f);
    } else {
        // For top-down views: use symmetric projection (no clear "bottom")
        // Use max of horizontal and vertical for both axes
        float maxSize = glm::max(hSize, vSize);
        proj = glm::ortho(-maxSize, maxSize, -maxSize, maxSize, 0.1f, camDist + boundingSphereRadius * 2.0f);
    }

    // Vulkan clip space correction - for asymmetric projections, must flip both Y scale and Y translation
    proj[1][1] *= -1;
    proj[3][1] *= -1;

    glm::mat4 viewProj = proj * view;

    // ===== DRAW BRANCHES =====
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchCapturePipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, capturePipelineLayout_.get(),
                           0, 1, &branchDescSet, 0, nullptr);

    // Bind branch mesh buffers
    VkBuffer branchVertexBuffers[] = {branchMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, branchVertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // Push constants for branches
    struct {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 captureParams;
    } branchPush;

    branchPush.viewProj = viewProj;
    branchPush.model = glm::mat4(1.0f);
    branchPush.captureParams = glm::vec4(
        static_cast<float>(cellX + cellY * ImpostorAtlasConfig::CELLS_PER_ROW),
        0.0f,  // is leaf pass = false
        boundingSphereRadius,
        0.1f   // alpha test
    );

    vkCmdPushConstants(cmd, capturePipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(branchPush), &branchPush);

    // Draw branches
    vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);

    // ===== DRAW LEAVES =====
    if (leafDescSet != VK_NULL_HANDLE && !leafInstances.empty() && leafQuadIndexCount_ > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafCapturePipeline_.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafCapturePipelineLayout_.get(),
                               0, 1, &leafDescSet, 0, nullptr);

        // Bind leaf quad mesh
        VkBuffer leafVertexBuffers[] = {leafQuadVertexBuffer_};
        vkCmdBindVertexBuffers(cmd, 0, 1, leafVertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, leafQuadIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);

        // Reset viewport and scissor (they may have been affected)
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push constants for leaves
        struct {
            glm::mat4 viewProj;
            glm::mat4 model;
            glm::vec4 captureParams;
            int32_t firstInstance;
        } leafPush;

        leafPush.viewProj = viewProj;
        leafPush.model = glm::mat4(1.0f);
        leafPush.captureParams = glm::vec4(
            static_cast<float>(cellX + cellY * ImpostorAtlasConfig::CELLS_PER_ROW),
            1.0f,  // is leaf pass = true
            boundingSphereRadius,
            0.3f   // alpha test for leaves
        );
        leafPush.firstInstance = 0;  // All leaves start at offset 0

        vkCmdPushConstants(cmd, leafCapturePipelineLayout_.get(),
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(leafPush), &leafPush);

        // Draw all leaf instances
        vkCmdDrawIndexed(cmd, leafQuadIndexCount_, static_cast<uint32_t>(leafInstances.size()), 0, 0, 0);
    }
}

void TreeImpostorAtlas::renderToOctahedral(
    VkCommandBuffer cmd,
    float azimuth,
    float elevation,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    float horizontalRadius,
    float boundingSphereRadius,
    float halfHeight,
    float centerHeight,
    float baseY,
    VkDescriptorSet branchDescSet,
    VkDescriptorSet leafDescSet) {

    // Convert azimuth/elevation to view direction (direction from tree to camera)
    float azimuthRad = glm::radians(azimuth);
    float elevationRad = glm::radians(elevation);

    glm::vec3 viewDir(
        std::cos(elevationRad) * std::sin(azimuthRad),
        std::sin(elevationRad),
        std::cos(elevationRad) * std::cos(azimuthRad)
    );

    // Compute octahedral UV for this view direction
    glm::vec2 octUV = octahedralEncode(viewDir);

    // Each view covers a region of the atlas based on the sampling density
    // With 16 azimuth x 8 elevation steps, each cell is roughly:
    // - horizontal: 512 / 16 = 32 pixels at equator, less at poles
    // - vertical: similar, with adjustment for octahedral distortion

    // Compute viewport region for this view
    // Use a fixed cell size based on the sampling grid
    const int CELL_SIZE = 64;  // Each view covers 64x64 pixels (overlapping for smooth blending)

    int viewportX = static_cast<int>(octUV.x * OctahedralAtlasConfig::ATLAS_WIDTH - CELL_SIZE / 2);
    int viewportY = static_cast<int>(octUV.y * OctahedralAtlasConfig::ATLAS_HEIGHT - CELL_SIZE / 2);

    // Clamp to atlas bounds
    viewportX = std::max(0, std::min(viewportX, OctahedralAtlasConfig::ATLAS_WIDTH - CELL_SIZE));
    viewportY = std::max(0, std::min(viewportY, OctahedralAtlasConfig::ATLAS_HEIGHT - CELL_SIZE));

    VkViewport viewport{};
    viewport.x = static_cast<float>(viewportX);
    viewport.y = static_cast<float>(viewportY);
    viewport.width = static_cast<float>(CELL_SIZE);
    viewport.height = static_cast<float>(CELL_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {viewportX, viewportY};
    scissor.extent = {static_cast<uint32_t>(CELL_SIZE), static_cast<uint32_t>(CELL_SIZE)};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Calculate camera position for this view angle
    float camDist = boundingSphereRadius * 3.0f;
    glm::vec3 target(0.0f, centerHeight, 0.0f);
    glm::vec3 camPos(
        camDist * std::cos(elevationRad) * std::sin(azimuthRad),
        centerHeight + camDist * std::sin(elevationRad),
        camDist * std::cos(elevationRad) * std::cos(azimuthRad)
    );

    // Up vector handling for top-down view
    glm::vec3 up = (elevation > 80.0f) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(camPos, target, up);

    // Orthographic projection
    float hSize = horizontalRadius * 1.1f;
    float vSize = halfHeight * 1.1f;

    glm::mat4 proj;
    if (elevation < 80.0f) {
        float elevCos = std::cos(elevationRad);
        float baseInViewSpace = (baseY - centerHeight) * elevCos;
        float yBottom = baseInViewSpace;
        float yTop = yBottom + 2.0f * vSize * elevCos;
        proj = glm::ortho(-hSize, hSize, yBottom, yTop, 0.1f, camDist + boundingSphereRadius * 2.0f);
    } else {
        float maxSize = std::max(hSize, vSize);
        proj = glm::ortho(-maxSize, maxSize, -maxSize, maxSize, 0.1f, camDist + boundingSphereRadius * 2.0f);
    }

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
    branchPush.captureParams = glm::vec4(0.0f, 0.0f, boundingSphereRadius, 0.1f);

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
        leafPush.captureParams = glm::vec4(0.0f, 1.0f, boundingSphereRadius, 0.3f);
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

VkImageView TreeImpostorAtlas::getAlbedoAtlasView(uint32_t archetypeIndex) const {
    if (archetypeIndex < atlasTextures_.size()) {
        return atlasTextures_[archetypeIndex].albedoAlphaView.get();
    }
    return VK_NULL_HANDLE;
}

VkImageView TreeImpostorAtlas::getNormalAtlasView(uint32_t archetypeIndex) const {
    if (archetypeIndex < atlasTextures_.size()) {
        return atlasTextures_[archetypeIndex].normalDepthAOView.get();
    }
    return VK_NULL_HANDLE;
}

VkImageView TreeImpostorAtlas::getPreviewImageView(uint32_t archetypeIndex) const {
    return getAlbedoAtlasView(archetypeIndex);
}

VkDescriptorSet TreeImpostorAtlas::getPreviewDescriptorSet(uint32_t archetypeIndex) {
    if (archetypeIndex >= atlasTextures_.size()) {
        return VK_NULL_HANDLE;
    }

    // Lazy initialization: create ImGui descriptor set on first request
    // (ImGui must be initialized by this point, which happens after renderer init)
    if (atlasTextures_[archetypeIndex].previewDescriptorSet == VK_NULL_HANDLE) {
        atlasTextures_[archetypeIndex].previewDescriptorSet = ImGui_ImplVulkan_AddTexture(
            atlasSampler_.get(),
            atlasTextures_[archetypeIndex].albedoAlphaView.get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    return atlasTextures_[archetypeIndex].previewDescriptorSet;
}
