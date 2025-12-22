#include "TreeImpostorAtlas.h"
#include "TreeSystem.h"
#include "Mesh.h"
#include "ShaderLoader.h"
#include "core/BufferUtils.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

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

    // Cleanup atlas textures
    for (auto& atlas : atlasTextures_) {
        if (atlas.albedoAlphaImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, atlas.albedoAlphaImage, atlas.albedoAlphaAllocation);
        }
        if (atlas.normalDepthAOImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, atlas.normalDepthAOImage, atlas.normalDepthAOAllocation);
        }
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

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create render pass");
        return false;
    }

    if (!createCapturePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeImpostorAtlas: Failed to create capture pipeline");
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
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal + Depth + AO attachment (RGBA8)
    attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

bool TreeImpostorAtlas::createAtlasResources(uint32_t archetypeIndex) {
    // Ensure we have space for this archetype
    if (archetypeIndex >= atlasTextures_.size()) {
        atlasTextures_.resize(archetypeIndex + 1);
    }

    auto& atlas = atlasTextures_[archetypeIndex];

    // Create albedo+alpha image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = ImpostorAtlasConfig::ATLAS_WIDTH;
    imageInfo.extent.height = ImpostorAtlasConfig::ATLAS_HEIGHT;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &atlas.albedoAlphaImage, &atlas.albedoAlphaAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create normal+depth+AO image
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &atlas.normalDepthAOImage, &atlas.normalDepthAOAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create depth image
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &atlas.depthImage, &atlas.depthAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image views
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    viewInfo.image = atlas.albedoAlphaImage;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageView albedoView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        return false;
    }
    atlas.albedoAlphaView = ManagedImageView(makeUniqueImageView(device_, albedoView));

    viewInfo.image = atlas.normalDepthAOImage;
    VkImageView normalView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &normalView) != VK_SUCCESS) {
        return false;
    }
    atlas.normalDepthAOView = ManagedImageView(makeUniqueImageView(device_, normalView));

    viewInfo.image = atlas.depthImage;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageView depthView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
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

    // Calculate bounding sphere from mesh
    float maxRadius = 0.0f;
    for (const auto& vertex : branchMesh.getVertices()) {
        float dist = glm::length(vertex.position);
        maxRadius = std::max(maxRadius, dist);
    }

    // Include leaves in bounding calculation
    for (const auto& leaf : leafInstances) {
        float dist = glm::length(glm::vec3(leaf.positionAndSize)) + leaf.positionAndSize.w;
        maxRadius = std::max(maxRadius, dist);
    }

    // Allocate descriptor set for capture
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
        renderToCell(cmd, h, 0, azimuth, 0.0f, branchMesh, leafInstances, maxRadius,
                     barkAlbedo, barkNormal, leafAlbedo, captureDescSet);
        cellIndex++;
    }

    // Top-down view (cell 8 of row 0)
    renderToCell(cmd, 8, 0, 0.0f, 90.0f, branchMesh, leafInstances, maxRadius,
                 barkAlbedo, barkNormal, leafAlbedo, captureDescSet);
    cellIndex++;

    // Row 1: 8 elevated views (45 degrees)
    for (int h = 0; h < ImpostorAtlasConfig::HORIZONTAL_ANGLES; h++) {
        float azimuth = h * (360.0f / ImpostorAtlasConfig::HORIZONTAL_ANGLES);
        renderToCell(cmd, h, 1, azimuth, 45.0f, branchMesh, leafInstances, maxRadius,
                     barkAlbedo, barkNormal, leafAlbedo, captureDescSet);
        cellIndex++;
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
    archetype.boundingSphereRadius = maxRadius;
    archetype.albedoAlphaView = atlasTextures_[archetypeIndex].albedoAlphaView.get();
    archetype.normalDepthAOView = atlasTextures_[archetypeIndex].normalDepthAOView.get();
    archetype.atlasIndex = archetypeIndex;

    archetypes_.push_back(archetype);

    SDL_Log("TreeImpostorAtlas: Generated archetype '%s' (radius=%.2f, %d cells)",
            name.c_str(), maxRadius, cellIndex);

    return static_cast<int32_t>(archetypeIndex);
}

void TreeImpostorAtlas::renderToCell(
    VkCommandBuffer cmd,
    int cellX, int cellY,
    float azimuth,
    float elevation,
    const Mesh& branchMesh,
    const std::vector<LeafInstanceGPU>& leafInstances,
    float boundingRadius,
    VkImageView barkAlbedo,
    VkImageView barkNormal,
    VkImageView leafAlbedo,
    VkDescriptorSet captureDescSet) {

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
    scissor.extent = {ImpostorAtlasConfig::CELL_SIZE, ImpostorAtlasConfig::CELL_SIZE};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Calculate camera position for this view angle
    float azimuthRad = glm::radians(azimuth);
    float elevationRad = glm::radians(elevation);

    float camDist = boundingRadius * 3.0f;
    glm::vec3 camPos(
        camDist * cos(elevationRad) * sin(azimuthRad),
        camDist * sin(elevationRad),
        camDist * cos(elevationRad) * cos(azimuthRad)
    );

    glm::vec3 target(0.0f, boundingRadius * 0.5f, 0.0f);  // Look at tree center
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(camPos, target, up);

    // Orthographic projection sized to fit tree
    float orthoSize = boundingRadius * 1.2f;
    glm::mat4 proj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, camDist * 2.0f);

    // Vulkan clip space correction
    proj[1][1] *= -1;

    glm::mat4 viewProj = proj * view;

    // Push constants
    struct {
        glm::mat4 viewProj;
        glm::mat4 model;
        glm::vec4 captureParams;
    } pushConstants;

    pushConstants.viewProj = viewProj;
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.captureParams = glm::vec4(
        static_cast<float>(cellX + cellY * ImpostorAtlasConfig::CELLS_PER_ROW),
        0.0f,  // is leaf pass
        boundingRadius,
        0.1f   // alpha test
    );

    vkCmdPushConstants(cmd, capturePipelineLayout_.get(),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Draw branches
    vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);
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

VkDescriptorSet TreeImpostorAtlas::getPreviewDescriptorSet(uint32_t archetypeIndex) const {
    if (archetypeIndex < atlasTextures_.size()) {
        return atlasTextures_[archetypeIndex].previewDescriptorSet;
    }
    return VK_NULL_HANDLE;
}
