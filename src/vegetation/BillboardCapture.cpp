#include "BillboardCapture.h"
#include "ShaderLoader.h"
#include "TreeEditSystem.h"
#include "VulkanResourceFactory.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>
#include <array>
#include <cstring>
#include <cmath>

using ShaderLoader::loadShaderModule;

std::vector<CaptureAngle> BillboardCapture::getStandardAngles() {
    std::vector<CaptureAngle> angles;

    // 8 side views (elevation 0 degrees)
    for (int i = 0; i < 8; ++i) {
        float azimuth = i * 45.0f;
        angles.push_back({azimuth, 0.0f, "side_" + std::to_string(i * 45)});
    }

    // 8 angled views (elevation 45 degrees)
    for (int i = 0; i < 8; ++i) {
        float azimuth = i * 45.0f;
        angles.push_back({azimuth, 45.0f, "angled_" + std::to_string(i * 45)});
    }

    // 1 top view (elevation 90 degrees)
    angles.push_back({0.0f, 90.0f, "top"});

    return angles;
}

std::unique_ptr<BillboardCapture> BillboardCapture::create(const InitInfo& info) {
    std::unique_ptr<BillboardCapture> instance(new BillboardCapture());
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

BillboardCapture::~BillboardCapture() {
    cleanup();
}

bool BillboardCapture::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;

    if (!createRenderPass()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createUniformBuffer()) return false;

    SDL_Log("Billboard capture system initialized");
    return true;
}

void BillboardCapture::cleanup() {
    destroyRenderTarget();

    // ManagedBuffer cleanup for UBO
    if (uboMapped) {
        uboBuffer_.unmap();
        uboMapped = nullptr;
    }
    uboBuffer_.reset();

    // RAII wrappers automatically clean up: solidPipeline_, leafPipeline_,
    // pipelineLayout_, descriptorSetLayout_, renderPass_, framebuffer_, sampler_
}

bool BillboardCapture::createRenderPass() {
    // Color attachment (RGBA8)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = COLOR_FORMAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = DEPTH_FORMAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    if (!ManagedRenderPass::create(device, createInfo, renderPass_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard render pass");
        return false;
    }

    return true;
}

bool BillboardCapture::createRenderTarget(uint32_t width, uint32_t height) {
    // Destroy existing if any
    destroyRenderTarget();

    renderWidth = width;
    renderHeight = height;

    // Create color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = COLOR_FORMAT;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &colorImage, &colorAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard color image");
        return false;
    }

    // Create color image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = COLOR_FORMAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &colorImageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard color image view");
        return false;
    }

    // Create depth image
    imageInfo.format = DEPTH_FORMAT;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard depth image");
        return false;
    }

    // Create depth image view
    viewInfo.image = depthImage;
    viewInfo.format = DEPTH_FORMAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard depth image view");
        return false;
    }

    // Create framebuffer
    std::array<VkImageView, 2> attachments = {colorImageView, depthImageView};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_.get();
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;

    if (!ManagedFramebuffer::create(device, fbInfo, framebuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard framebuffer");
        return false;
    }

    // Create staging buffer for pixel readback using RAII wrapper
    VkDeviceSize stagingSize = width * height * 4;  // RGBA8

    if (!VulkanResourceFactory::createReadbackBuffer(allocator, stagingSize, stagingBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard staging buffer");
        return false;
    }

    return true;
}

void BillboardCapture::destroyRenderTarget() {
    // ManagedBuffer cleanup for staging buffer (RAII via reset)
    stagingBuffer_.reset();

    // Reset RAII framebuffer wrapper
    framebuffer_ = ManagedFramebuffer();

    if (depthImageView) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }

    if (depthImage) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
    }

    if (colorImageView) {
        vkDestroyImageView(device, colorImageView, nullptr);
        colorImageView = VK_NULL_HANDLE;
    }

    if (colorImage) {
        vmaDestroyImage(allocator, colorImage, colorAllocation);
        colorImage = VK_NULL_HANDLE;
    }

    renderWidth = 0;
    renderHeight = 0;
}

bool BillboardCapture::createDescriptorSetLayout() {
    // Use DescriptorManager::LayoutBuilder for consistent descriptor set layout creation
    // Same layout as TreeEditSystem: 1 UBO + 5 texture samplers
    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: Scene UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: Bark color texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: Bark normal texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: Bark AO texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 4: Bark roughness texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: Leaf texture
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard descriptor set layout");
        return false;
    }

    // Adopt raw handle into RAII wrapper
    descriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);
    return true;
}

bool BillboardCapture::createDescriptorSets() {
    if (!descriptorPool) return false;

    auto sets = descriptorPool->allocate(descriptorSetLayout_.get(), 1);
    if (sets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate billboard descriptor set");
        return false;
    }
    descriptorSet = sets[0];

    return true;
}

bool BillboardCapture::createUniformBuffer() {
    if (!VulkanResourceFactory::createUniformBuffer(allocator, sizeof(UniformBufferObject), uboBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard UBO buffer");
        return false;
    }

    uboMapped = uboBuffer_.map();
    return true;
}

bool BillboardCapture::createPipeline() {
    // Load shaders - use tree.vert but tree_billboard.frag (no fog)
    auto vertModule = loadShaderModule(device, shaderPath + "/tree.vert.spv");
    auto fragModule = loadShaderModule(device, shaderPath + "/tree_billboard.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load billboard shaders");
        if (vertModule) vkDestroyShaderModule(device, *vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, *fragModule, nullptr);
        return false;
    }

    // Shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragModule;
    shaderStages[1].pName = "main";

    // Vertex input
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants (same as TreeEditSystem)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TreePushConstants);

    VkDescriptorSetLayout rawLayout = descriptorSetLayout_.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, pipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard pipeline layout");
        vkDestroyShaderModule(device, *vertModule, nullptr);
        vkDestroyShaderModule(device, *fragModule, nullptr);
        return false;
    }

    // Create solid pipeline (for branches)
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
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_.get();
    pipelineInfo.subpass = 0;

    if (!ManagedPipeline::createGraphics(device, VK_NULL_HANDLE, pipelineInfo, solidPipeline_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard solid pipeline");
        vkDestroyShaderModule(device, *vertModule, nullptr);
        vkDestroyShaderModule(device, *fragModule, nullptr);
        return false;
    }

    // Create leaf pipeline (no culling, alpha blending)
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (!ManagedPipeline::createGraphics(device, VK_NULL_HANDLE, pipelineInfo, leafPipeline_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create billboard leaf pipeline");
        vkDestroyShaderModule(device, *vertModule, nullptr);
        vkDestroyShaderModule(device, *fragModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, *vertModule, nullptr);
    vkDestroyShaderModule(device, *fragModule, nullptr);

    return true;
}

void BillboardCapture::calculateBoundingSphere(const Mesh& branchMesh, const Mesh& leafMesh,
                                                glm::vec3& outCenter, float& outRadius) {
    // Calculate bounding box from both meshes
    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());

    auto processVertices = [&](const std::vector<Vertex>& vertices) {
        for (const auto& v : vertices) {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }
    };

    processVertices(branchMesh.getVertices());
    if (!leafMesh.getVertices().empty()) {
        processVertices(leafMesh.getVertices());
    }

    // Calculate center and radius
    outCenter = (minPos + maxPos) * 0.5f;
    outRadius = glm::length(maxPos - minPos) * 0.5f;

    // Add some padding
    outRadius *= 1.1f;
}

glm::mat4 BillboardCapture::calculateViewMatrix(const CaptureAngle& angle, const glm::vec3& center, float distance) {
    float azimuthRad = glm::radians(angle.azimuth);
    float elevationRad = glm::radians(angle.elevation);

    // Calculate camera position on sphere around center
    float cosElev = std::cos(elevationRad);
    float sinElev = std::sin(elevationRad);
    float cosAz = std::cos(azimuthRad);
    float sinAz = std::sin(azimuthRad);

    glm::vec3 cameraPos = center + glm::vec3(
        sinAz * cosElev * distance,
        sinElev * distance,
        cosAz * cosElev * distance
    );

    glm::vec3 up = (angle.elevation >= 89.0f) ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);

    return glm::lookAt(cameraPos, center, up);
}

bool BillboardCapture::renderCapture(
    const Mesh& branchMesh,
    const Mesh& leafMesh,
    const TreeParameters& treeParams,
    const glm::mat4& view,
    const glm::mat4& proj,
    const Texture& barkColorTex,
    const Texture& barkNormalTex,
    const Texture& barkAOTex,
    const Texture& barkRoughnessTex,
    const Texture& leafTex)
{
    // Update UBO
    UniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;

    // Extract camera position from view matrix
    glm::mat4 invView = glm::inverse(view);
    ubo.cameraPosition = glm::vec4(invView[3]);

    // Set up lighting for consistent billboard appearance
    // Use overhead sun for even lighting
    ubo.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)), 1.0f);
    ubo.sunColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
    ubo.moonDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    ubo.moonColor = glm::vec4(0.0f);
    ubo.ambientColor = glm::vec4(0.4f, 0.45f, 0.5f, 1.0f);

    memcpy(uboMapped, &ubo, sizeof(ubo));

    // Update descriptor set using DescriptorManager::SetWriter for consistency
    DescriptorManager::SetWriter(device, descriptorSet)
        .writeBuffer(0, uboBuffer_.get(), 0, sizeof(UniformBufferObject))
        .writeImage(1, barkColorTex.getImageView(), barkColorTex.getSampler())
        .writeImage(2, barkNormalTex.getImageView(), barkNormalTex.getSampler())
        .writeImage(3, barkAOTex.getImageView(), barkAOTex.getSampler())
        .writeImage(4, barkRoughnessTex.getImageView(), barkRoughnessTex.getSampler())
        .writeImage(5, leafTex.getImageView(), leafTex.getSampler())
        .update();

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Transparent background
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass_.get();
    rpInfo.framebuffer = framebuffer_.get();
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {renderWidth, renderHeight};
    rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(renderWidth);
    viewport.height = static_cast<float>(renderHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {renderWidth, renderHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(),
                            0, 1, &descriptorSet, 0, nullptr);

    // Push constants
    TreePushConstants pc{};
    pc.model = glm::mat4(1.0f);  // Identity - tree at origin
    pc.roughness = 0.8f;
    pc.metallic = 0.0f;
    pc.alphaTest = 0.0f;
    pc.isLeaf = 0;

    // Draw branches
    if (branchMesh.getIndexCount() > 0 && branchMesh.getVertexBuffer() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solidPipeline_.get());
        vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreePushConstants), &pc);

        VkBuffer vertexBuffers[] = {branchMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);
    }

    // Draw leaves
    if (leafMesh.getIndexCount() > 0 && leafMesh.getVertexBuffer() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipeline_.get());

        pc.roughness = 0.6f;
        pc.alphaTest = treeParams.leafAlphaTest;
        pc.isLeaf = 1;
        vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreePushConstants), &pc);

        VkBuffer vertexBuffers[] = {leafMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, leafMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, leafMesh.getIndexCount(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // Copy color image to staging buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {renderWidth, renderHeight, 1};

    vkCmdCopyImageToBuffer(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer_.get(), 1, &region);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    return true;
}

bool BillboardCapture::readPixels(std::vector<uint8_t>& outPixels) {
    outPixels.resize(renderWidth * renderHeight * 4);

    void* data = stagingBuffer_.map();
    if (!data) return false;
    memcpy(outPixels.data(), data, outPixels.size());
    stagingBuffer_.unmap();

    return true;
}

bool BillboardCapture::generateAtlas(
    const Mesh& branchMesh,
    const Mesh& leafMesh,
    const TreeParameters& treeParams,
    const Texture& barkColorTex,
    const Texture& barkNormalTex,
    const Texture& barkAOTex,
    const Texture& barkRoughnessTex,
    const Texture& leafTex,
    uint32_t captureResolution,
    BillboardAtlas& outAtlas)
{
    auto angles = getStandardAngles();
    uint32_t numCaptures = static_cast<uint32_t>(angles.size());

    // Calculate atlas layout (5 columns x 4 rows = 20 cells, we use 17)
    uint32_t cols = 5;
    uint32_t rows = 4;

    outAtlas.cellWidth = captureResolution;
    outAtlas.cellHeight = captureResolution;
    outAtlas.columns = cols;
    outAtlas.rows = rows;
    outAtlas.width = cols * captureResolution;
    outAtlas.height = rows * captureResolution;
    outAtlas.angles = angles;

    // Initialize atlas with transparent pixels
    outAtlas.rgbaPixels.resize(outAtlas.width * outAtlas.height * 4, 0);

    // Create render target
    if (!createRenderTarget(captureResolution, captureResolution)) {
        return false;
    }

    // Allocate descriptor set
    if (!createDescriptorSets()) {
        return false;
    }

    // Calculate tree bounding sphere
    glm::vec3 center;
    float radius;
    calculateBoundingSphere(branchMesh, leafMesh, center, radius);

    // Orthographic projection that fits the tree
    float orthoSize = radius * 1.1f;
    glm::mat4 proj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, radius * 4.0f);
    // Flip Y for Vulkan
    proj[1][1] *= -1;

    float cameraDistance = radius * 2.0f;

    SDL_Log("Generating billboard atlas: %d captures at %dx%d", numCaptures, captureResolution, captureResolution);

    // Capture each angle
    for (uint32_t i = 0; i < numCaptures; ++i) {
        const auto& angle = angles[i];

        glm::mat4 view = calculateViewMatrix(angle, center, cameraDistance);

        // Render
        if (!renderCapture(branchMesh, leafMesh, treeParams, view, proj,
                          barkColorTex, barkNormalTex, barkAOTex, barkRoughnessTex, leafTex)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render capture %d (%s)", i, angle.name.c_str());
            continue;
        }

        // Read pixels
        std::vector<uint8_t> capturePixels;
        if (!readPixels(capturePixels)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read capture %d (%s)", i, angle.name.c_str());
            continue;
        }

        // Copy to atlas position
        uint32_t col = i % cols;
        uint32_t row = i / cols;
        uint32_t atlasX = col * captureResolution;
        uint32_t atlasY = row * captureResolution;

        for (uint32_t y = 0; y < captureResolution; ++y) {
            uint32_t srcOffset = y * captureResolution * 4;
            uint32_t dstOffset = ((atlasY + y) * outAtlas.width + atlasX) * 4;
            memcpy(&outAtlas.rgbaPixels[dstOffset], &capturePixels[srcOffset], captureResolution * 4);
        }

        SDL_Log("Captured angle %d: %s (azimuth=%.0f, elevation=%.0f)",
                i, angle.name.c_str(), angle.azimuth, angle.elevation);
    }

    SDL_Log("Billboard atlas generated: %dx%d (%d captures)", outAtlas.width, outAtlas.height, numCaptures);

    return true;
}

// TreeMesh overload - calculates bounding sphere from TreeVertex data
void BillboardCapture::calculateBoundingSphere(const TreeMesh& branchMesh, const TreeMesh& leafMesh,
                                                glm::vec3& outCenter, float& outRadius) {
    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());

    auto processVertices = [&](const std::vector<TreeVertex>& vertices) {
        for (const auto& v : vertices) {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }
    };

    processVertices(branchMesh.getVertices());
    if (!leafMesh.getVertices().empty()) {
        processVertices(leafMesh.getVertices());
    }

    outCenter = (minPos + maxPos) * 0.5f;
    outRadius = glm::length(maxPos - minPos) * 0.5f;
    outRadius *= 1.1f;
}

// TreeMesh overload for renderCapture
bool BillboardCapture::renderCapture(
    const TreeMesh& branchMesh,
    const TreeMesh& leafMesh,
    const TreeParameters& treeParams,
    const glm::mat4& view,
    const glm::mat4& proj,
    const Texture& barkColorTex,
    const Texture& barkNormalTex,
    const Texture& barkAOTex,
    const Texture& barkRoughnessTex,
    const Texture& leafTex)
{
    // Update UBO
    UniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;

    glm::mat4 invView = glm::inverse(view);
    ubo.cameraPosition = glm::vec4(invView[3]);

    ubo.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)), 1.0f);
    ubo.sunColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
    ubo.moonDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    ubo.moonColor = glm::vec4(0.0f);
    ubo.ambientColor = glm::vec4(0.4f, 0.45f, 0.5f, 1.0f);

    memcpy(uboMapped, &ubo, sizeof(ubo));

    DescriptorManager::SetWriter(device, descriptorSet)
        .writeBuffer(0, uboBuffer_.get(), 0, sizeof(UniformBufferObject))
        .writeImage(1, barkColorTex.getImageView(), barkColorTex.getSampler())
        .writeImage(2, barkNormalTex.getImageView(), barkNormalTex.getSampler())
        .writeImage(3, barkAOTex.getImageView(), barkAOTex.getSampler())
        .writeImage(4, barkRoughnessTex.getImageView(), barkRoughnessTex.getSampler())
        .writeImage(5, leafTex.getImageView(), leafTex.getSampler())
        .update();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass_.get();
    rpInfo.framebuffer = framebuffer_.get();
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {renderWidth, renderHeight};
    rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(renderWidth);
    viewport.height = static_cast<float>(renderHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {renderWidth, renderHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(),
                            0, 1, &descriptorSet, 0, nullptr);

    TreePushConstants pc{};
    pc.model = glm::mat4(1.0f);
    pc.roughness = 0.8f;
    pc.metallic = 0.0f;
    pc.alphaTest = 0.0f;
    pc.isLeaf = 0;

    // Draw branches
    if (branchMesh.getIndexCount() > 0 && branchMesh.getVertexBuffer() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solidPipeline_.get());
        vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreePushConstants), &pc);

        VkBuffer vertexBuffers[] = {branchMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);
    }

    // Draw leaves
    if (leafMesh.getIndexCount() > 0 && leafMesh.getVertexBuffer() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipeline_.get());

        pc.roughness = 0.6f;
        pc.alphaTest = treeParams.leafAlphaTest;
        pc.isLeaf = 1;
        vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreePushConstants), &pc);

        VkBuffer vertexBuffers[] = {leafMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, leafMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, leafMesh.getIndexCount(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {renderWidth, renderHeight, 1};

    vkCmdCopyImageToBuffer(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer_.get(), 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    return true;
}

// TreeMesh overload for generateAtlas
bool BillboardCapture::generateAtlas(
    const TreeMesh& branchMesh,
    const TreeMesh& leafMesh,
    const TreeParameters& treeParams,
    const Texture& barkColorTex,
    const Texture& barkNormalTex,
    const Texture& barkAOTex,
    const Texture& barkRoughnessTex,
    const Texture& leafTex,
    uint32_t captureResolution,
    BillboardAtlas& outAtlas)
{
    auto angles = getStandardAngles();
    uint32_t numCaptures = static_cast<uint32_t>(angles.size());

    uint32_t cols = 5;
    uint32_t rows = 4;

    outAtlas.cellWidth = captureResolution;
    outAtlas.cellHeight = captureResolution;
    outAtlas.columns = cols;
    outAtlas.rows = rows;
    outAtlas.width = cols * captureResolution;
    outAtlas.height = rows * captureResolution;
    outAtlas.angles = angles;

    outAtlas.rgbaPixels.resize(outAtlas.width * outAtlas.height * 4, 0);

    if (!createRenderTarget(captureResolution, captureResolution)) {
        return false;
    }

    if (!createDescriptorSets()) {
        return false;
    }

    glm::vec3 center;
    float radius;
    calculateBoundingSphere(branchMesh, leafMesh, center, radius);

    float orthoSize = radius * 1.1f;
    glm::mat4 proj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, radius * 4.0f);
    proj[1][1] *= -1;

    float cameraDistance = radius * 2.0f;

    SDL_Log("Generating billboard atlas: %d captures at %dx%d", numCaptures, captureResolution, captureResolution);

    for (uint32_t i = 0; i < numCaptures; ++i) {
        const auto& angle = angles[i];

        glm::mat4 view = calculateViewMatrix(angle, center, cameraDistance);

        if (!renderCapture(branchMesh, leafMesh, treeParams, view, proj,
                          barkColorTex, barkNormalTex, barkAOTex, barkRoughnessTex, leafTex)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to render capture %d (%s)", i, angle.name.c_str());
            continue;
        }

        std::vector<uint8_t> capturePixels;
        if (!readPixels(capturePixels)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read capture %d (%s)", i, angle.name.c_str());
            continue;
        }

        uint32_t col = i % cols;
        uint32_t row = i / cols;
        uint32_t atlasX = col * captureResolution;
        uint32_t atlasY = row * captureResolution;

        for (uint32_t y = 0; y < captureResolution; ++y) {
            uint32_t srcOffset = y * captureResolution * 4;
            uint32_t dstOffset = ((atlasY + y) * outAtlas.width + atlasX) * 4;
            memcpy(&outAtlas.rgbaPixels[dstOffset], &capturePixels[srcOffset], captureResolution * 4);
        }

        SDL_Log("Captured angle %d: %s (azimuth=%.0f, elevation=%.0f)",
                i, angle.name.c_str(), angle.azimuth, angle.elevation);
    }

    SDL_Log("Billboard atlas generated: %dx%d (%d captures)", outAtlas.width, outAtlas.height, numCaptures);

    return true;
}

bool BillboardCapture::saveAtlasToPNG(const BillboardAtlas& atlas, const std::string& filepath) {
    int result = stbi_write_png(filepath.c_str(), atlas.width, atlas.height, 4,
                                 atlas.rgbaPixels.data(), atlas.width * 4);
    if (result == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write billboard atlas to %s", filepath.c_str());
        return false;
    }

    SDL_Log("Billboard atlas saved to %s", filepath.c_str());
    return true;
}
