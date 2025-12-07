#include "ShadowSystem.h"
#include "ShaderLoader.h"
#include "Mesh.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>

bool ShadowSystem::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    mainDescriptorSetLayout = info.mainDescriptorSetLayout;
    skinnedDescriptorSetLayout = info.skinnedDescriptorSetLayout;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createShadowResources()) return false;
    if (!createShadowRenderPass()) return false;
    if (!createDynamicShadowResources()) return false;
    if (!createDynamicShadowRenderPass()) return false;
    if (!createShadowPipeline()) return false;
    if (!createSkinnedShadowPipeline()) return false;
    if (!createDynamicShadowPipeline()) return false;

    return true;
}

void ShadowSystem::destroy() {
    if (device == VK_NULL_HANDLE) return;

    // Shadow cleanup
    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, shadowPipeline, nullptr);
    }
    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
    }

    // Skinned shadow pipeline cleanup
    if (skinnedShadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skinnedShadowPipeline, nullptr);
    }
    if (skinnedShadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skinnedShadowPipelineLayout, nullptr);
    }

    // Destroy per-cascade framebuffers
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        if (cascadeFramebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, cascadeFramebuffers[i], nullptr);
        }
    }

    if (shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, shadowRenderPass, nullptr);
    }

    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, shadowSampler, nullptr);
    }

    // Destroy per-cascade image views
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        if (cascadeImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, cascadeImageViews[i], nullptr);
        }
    }

    if (shadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowImageView, nullptr);
    }

    if (shadowImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, shadowImage, shadowImageAllocation);
    }

    // Dynamic shadow cleanup
    destroyDynamicShadowResources();
}

bool ShadowSystem::createShadowResources() {
    // Create shadow map depth image array (NUM_SHADOW_CASCADES layers)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = SHADOW_MAP_SIZE;
    imageInfo.extent.height = SHADOW_MAP_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = NUM_SHADOW_CASCADES;  // 4 layers for CSM
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &shadowImage, &shadowImageAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow map image array");
        return false;
    }

    // Create shadow map array view (for sampling all cascades)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;  // Array view for shader sampling
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = NUM_SHADOW_CASCADES;

    if (vkCreateImageView(device, &viewInfo, nullptr, &shadowImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow map array view");
        return false;
    }

    // Create per-cascade image views (for rendering to individual layers)
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        VkImageViewCreateInfo cascadeViewInfo{};
        cascadeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cascadeViewInfo.image = shadowImage;
        cascadeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // Single layer view
        cascadeViewInfo.format = VK_FORMAT_D32_SFLOAT;
        cascadeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        cascadeViewInfo.subresourceRange.baseMipLevel = 0;
        cascadeViewInfo.subresourceRange.levelCount = 1;
        cascadeViewInfo.subresourceRange.baseArrayLayer = i;  // Layer for this cascade
        cascadeViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &cascadeViewInfo, nullptr, &cascadeImageViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create cascade image view %u", i);
            return false;
        }
    }

    // Create shadow sampler with depth comparison
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow sampler");
        return false;
    }

    return true;
}

bool ShadowSystem::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow render pass");
        return false;
    }

    // Create per-cascade framebuffers
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &cascadeImageViews[i];  // Per-cascade view
        framebufferInfo.width = SHADOW_MAP_SIZE;
        framebufferInfo.height = SHADOW_MAP_SIZE;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &cascadeFramebuffers[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create cascade framebuffer %u", i);
            return false;
        }
    }

    return true;
}

bool ShadowSystem::createShadowPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load shadow shaders");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Use the same vertex input as main pipeline
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
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;  // No color attachments

    // Shadow pipeline layout - reuse the main descriptor set layout for compatibility
    // (shadow shader only uses binding 0, but the descriptor sets have all bindings)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mainDescriptorSetLayout;  // Use main layout for compatibility
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

bool ShadowSystem::createSkinnedShadowPipeline() {
    // Skip if no skinned descriptor set layout provided
    if (skinnedDescriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Skinned shadow pipeline skipped (no skinned descriptor set layout)");
        return true;
    }

    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/skinned_shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load skinned shadow shaders");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Use SkinnedVertex input layout
    auto bindingDescription = SkinnedVertex::getBindingDescription();
    auto attributeDescriptions = SkinnedVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;  // No color attachments

    // Skinned shadow pipeline layout - use skinned descriptor set layout (has binding 12 for bone matrices)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &skinnedDescriptorSetLayout;  // Use skinned layout (has binding 12 for bones)
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skinnedShadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create skinned shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = skinnedShadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skinnedShadowPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create skinned shadow pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    SDL_Log("Created skinned shadow pipeline for GPU-skinned character shadows");
    return true;
}

bool ShadowSystem::createDynamicShadowResources() {
    // Resize per-frame vectors
    pointShadowImages.resize(framesInFlight);
    pointShadowAllocations.resize(framesInFlight);
    pointShadowArrayViews.resize(framesInFlight);
    pointShadowFaceViews.resize(framesInFlight);

    spotShadowImages.resize(framesInFlight);
    spotShadowAllocations.resize(framesInFlight);
    spotShadowArrayViews.resize(framesInFlight);
    spotShadowLayerViews.resize(framesInFlight);

    pointShadowFramebuffers.resize(framesInFlight);
    spotShadowFramebuffers.resize(framesInFlight);

    // Create resources per frame
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        // Create point light shadow cube maps
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.height = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = MAX_SHADOW_CASTING_LIGHTS * 6;  // 6 faces per cube map
            imageInfo.format = VK_FORMAT_D32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &pointShadowImages[frame],
                             &pointShadowAllocations[frame], nullptr) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow cube map array");
                return false;
            }

            // Create array view (for sampling in shaders)
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = pointShadowImages[frame];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            viewInfo.format = VK_FORMAT_D32_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = MAX_SHADOW_CASTING_LIGHTS * 6;

            if (vkCreateImageView(device, &viewInfo, nullptr, &pointShadowArrayViews[frame]) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow array view");
                return false;
            }

            // Create per-face views for rendering (we'll only use the first light's faces for now)
            for (uint32_t face = 0; face < 6; face++) {
                VkImageViewCreateInfo faceViewInfo{};
                faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                faceViewInfo.image = pointShadowImages[frame];
                faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                faceViewInfo.format = VK_FORMAT_D32_SFLOAT;
                faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                faceViewInfo.subresourceRange.baseMipLevel = 0;
                faceViewInfo.subresourceRange.levelCount = 1;
                faceViewInfo.subresourceRange.baseArrayLayer = face;  // First light only for now
                faceViewInfo.subresourceRange.layerCount = 1;

                if (vkCreateImageView(device, &faceViewInfo, nullptr, &pointShadowFaceViews[frame][face]) != VK_SUCCESS) {
                    SDL_Log("Failed to create point shadow face view");
                    return false;
                }
            }
        }

        // Create spot light shadow 2D texture array
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.height = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = MAX_SHADOW_CASTING_LIGHTS;
            imageInfo.format = VK_FORMAT_D32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &spotShadowImages[frame],
                             &spotShadowAllocations[frame], nullptr) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow texture array");
                return false;
            }

            // Create array view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = spotShadowImages[frame];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = VK_FORMAT_D32_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = MAX_SHADOW_CASTING_LIGHTS;

            if (vkCreateImageView(device, &viewInfo, nullptr, &spotShadowArrayViews[frame]) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow array view");
                return false;
            }

            // Create per-layer views
            spotShadowLayerViews[frame].resize(MAX_SHADOW_CASTING_LIGHTS);
            for (uint32_t light = 0; light < MAX_SHADOW_CASTING_LIGHTS; light++) {
                VkImageViewCreateInfo layerViewInfo{};
                layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                layerViewInfo.image = spotShadowImages[frame];
                layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                layerViewInfo.format = VK_FORMAT_D32_SFLOAT;
                layerViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                layerViewInfo.subresourceRange.baseMipLevel = 0;
                layerViewInfo.subresourceRange.levelCount = 1;
                layerViewInfo.subresourceRange.baseArrayLayer = light;
                layerViewInfo.subresourceRange.layerCount = 1;

                if (vkCreateImageView(device, &layerViewInfo, nullptr, &spotShadowLayerViews[frame][light]) != VK_SUCCESS) {
                    SDL_Log("Failed to create spot shadow layer view");
                    return false;
                }
            }
        }
    }

    // Create samplers
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &pointShadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create point shadow sampler");
        return false;
    }

    if (vkCreateSampler(device, &samplerInfo, nullptr, &spotShadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create spot shadow sampler");
        return false;
    }

    return true;
}

bool ShadowSystem::createDynamicShadowRenderPass() {
    // Same as CSM shadow render pass
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPassDynamic) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow render pass");
        return false;
    }

    // Create framebuffers for each frame
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        // Point shadow framebuffers (6 faces for first light only for now)
        pointShadowFramebuffers[frame].resize(6);
        for (uint32_t face = 0; face < 6; face++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = shadowRenderPassDynamic;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &pointShadowFaceViews[frame][face];
            framebufferInfo.width = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.height = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                  &pointShadowFramebuffers[frame][face]) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow framebuffer");
                return false;
            }
        }

        // Spot shadow framebuffers (1 per light)
        spotShadowFramebuffers[frame].resize(MAX_SHADOW_CASTING_LIGHTS);
        for (uint32_t light = 0; light < MAX_SHADOW_CASTING_LIGHTS; light++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = shadowRenderPassDynamic;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &spotShadowLayerViews[frame][light];
            framebufferInfo.width = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.height = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                  &spotShadowFramebuffers[frame][light]) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow framebuffer");
                return false;
            }
        }
    }

    return true;
}

bool ShadowSystem::createDynamicShadowPipeline() {
    // Reuse CSM shadow shaders for now (we'll create specialized ones later if needed)
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load dynamic shadow shaders");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front-face culling for shadow maps
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

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

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mainDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &dynamicShadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = dynamicShadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPassDynamic;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dynamicShadowPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

void ShadowSystem::destroyDynamicShadowResources() {
    // Destroy framebuffers
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        for (auto fb : pointShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        }
        for (auto fb : spotShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        }
    }

    // Destroy image views
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        if (frame < pointShadowArrayViews.size() && pointShadowArrayViews[frame] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, pointShadowArrayViews[frame], nullptr);
        }
        if (frame < spotShadowArrayViews.size() && spotShadowArrayViews[frame] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, spotShadowArrayViews[frame], nullptr);
        }

        if (frame < pointShadowFaceViews.size()) {
            for (auto& faceView : pointShadowFaceViews[frame]) {
                if (faceView != VK_NULL_HANDLE) vkDestroyImageView(device, faceView, nullptr);
            }
        }
        if (frame < spotShadowLayerViews.size()) {
            for (auto& layerView : spotShadowLayerViews[frame]) {
                if (layerView != VK_NULL_HANDLE) vkDestroyImageView(device, layerView, nullptr);
            }
        }
    }

    // Destroy images
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        if (frame < pointShadowImages.size() && pointShadowImages[frame] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, pointShadowImages[frame], pointShadowAllocations[frame]);
        }
        if (frame < spotShadowImages.size() && spotShadowImages[frame] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, spotShadowImages[frame], spotShadowAllocations[frame]);
        }
    }

    // Destroy samplers
    if (pointShadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, pointShadowSampler, nullptr);
    if (spotShadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, spotShadowSampler, nullptr);

    // Destroy pipeline and layout
    if (dynamicShadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, dynamicShadowPipeline, nullptr);
    if (dynamicShadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, dynamicShadowPipelineLayout, nullptr);

    // Destroy render pass
    if (shadowRenderPassDynamic != VK_NULL_HANDLE) vkDestroyRenderPass(device, shadowRenderPassDynamic, nullptr);
}

void ShadowSystem::calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits) {
    // PSSM - Parallel Split Shadow Maps
    // Blend between logarithmic and uniform distribution
    splits.resize(NUM_SHADOW_CASCADES + 1);
    splits[0] = nearClip;

    float clipRange = farClip - nearClip;
    float ratio = farClip / nearClip;

    for (uint32_t i = 1; i <= NUM_SHADOW_CASCADES; i++) {
        float p = static_cast<float>(i) / NUM_SHADOW_CASCADES;

        // Logarithmic split (better near distribution)
        float logSplit = nearClip * std::pow(ratio, p);

        // Uniform split
        float uniformSplit = nearClip + clipRange * p;

        // Blend between log and uniform using lambda
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

glm::mat4 ShadowSystem::calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, float nearSplit, float farSplit) {
    glm::vec3 lightDirNorm = glm::normalize(lightDir);
    if (glm::length(lightDirNorm) < std::numeric_limits<float>::epsilon()) {
        lightDirNorm = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    // Get camera's projection matrix (which has Vulkan Y-flip) and undo the flip for frustum calculation
    glm::mat4 cameraProj = camera.getProjectionMatrix();
    cameraProj[1][1] *= -1.0f;  // Undo Vulkan Y-flip for standard frustum corners

    // Extract frustum parameters from the camera's projection matrix
    // For perspective: proj[0][0] = 1/(aspect*tan(fov/2)), proj[1][1] = 1/tan(fov/2)
    float tanHalfFov = 1.0f / cameraProj[1][1];
    float aspect = cameraProj[1][1] / cameraProj[0][0];

    // Calculate frustum corners at near and far split distances
    float nearHeight = nearSplit * tanHalfFov;
    float nearWidth = nearHeight * aspect;
    float farHeight = farSplit * tanHalfFov;
    float farWidth = farHeight * aspect;

    // Get camera vectors from inverse view matrix
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 camPos = glm::vec3(invView[3]);
    glm::vec3 camForward = -glm::vec3(invView[2]);  // Camera looks down -Z
    glm::vec3 camRight = glm::vec3(invView[0]);
    glm::vec3 camUp = glm::vec3(invView[1]);

    // Calculate frustum corners in world space
    glm::vec3 nearCenter = camPos + camForward * nearSplit;
    glm::vec3 farCenter = camPos + camForward * farSplit;

    std::array<glm::vec3, 8> frustumCorners{
        // Near plane corners
        nearCenter - camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth + camUp * nearHeight,
        nearCenter - camRight * nearWidth + camUp * nearHeight,
        // Far plane corners
        farCenter - camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth + camUp * farHeight,
        farCenter - camRight * farWidth + camUp * farHeight,
    };

    // Calculate frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) {
        center += corner;
    }
    center /= static_cast<float>(frustumCorners.size());

    // Use bounding sphere for uniform shadow map coverage (like the original working code)
    float radius = 0.0f;
    for (const auto& corner : frustumCorners) {
        radius = std::max(radius, glm::length(corner - center));
    }

    // Position light far enough to avoid near-plane clipping
    glm::vec3 up = (std::abs(lightDirNorm.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 lightPos = center + lightDirNorm * (radius + 50.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, center, up);

    // Use sphere-based ortho projection for uniform texel density
    float orthoSize = radius * 1.1f;  // Small margin for safety
    float zRange = radius * 2.0f + 100.0f;  // Cover the full sphere plus padding

    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, zRange);

    // Vulkan corrections:
    // 1. Flip Y (Vulkan has inverted Y compared to OpenGL)
    lightProjection[1][1] *= -1.0f;
    // 2. Transform Z from [-1,1] (OpenGL) to [0,1] (Vulkan)
    //    new_z = old_z * 0.5 + 0.5
    lightProjection[2][2] = lightProjection[2][2] * 0.5f;
    lightProjection[3][2] = lightProjection[3][2] * 0.5f + 0.5f;

    return lightProjection * lightView;
}

void ShadowSystem::updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera) {
    // Calculate cascade splits using PSSM
    const float shadowNear = 0.1f;
    const float shadowFar = 150.0f;  // Extended range for cascades
    const float lambda = 0.5f;  // 0.5 is good balance between log and uniform

    calculateCascadeSplits(shadowNear, shadowFar, lambda, cascadeSplitDepths);

    // Calculate light space matrix for each cascade
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        cascadeMatrices[i] = calculateCascadeMatrix(
            lightDir, camera,
            cascadeSplitDepths[i],
            cascadeSplitDepths[i + 1]
        );
    }
}

void ShadowSystem::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex,
                                     VkDescriptorSet descriptorSet,
                                     const std::vector<Renderable>& sceneObjects,
                                     const DrawCallback& terrainDrawCallback,
                                     const DrawCallback& grassDrawCallback,
                                     const DrawCallback& skinnedDrawCallback) {
    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = shadowRenderPass;
        shadowPassInfo.framebuffer = cascadeFramebuffers[cascade];
        shadowPassInfo.renderArea.offset = {0, 0};
        shadowPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;

        vkCmdBeginRenderPass(cmd, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        for (const auto& obj : sceneObjects) {
            if (!obj.castsShadow) continue;

            ShadowPushConstants shadowPush{};
            shadowPush.model = obj.transform;
            shadowPush.cascadeIndex = static_cast<int>(cascade);
            vkCmdPushConstants(cmd, shadowPipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &shadowPush);

            VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
        }

        // Terrain shadows via callback
        if (terrainDrawCallback) {
            terrainDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        }

        // Grass shadows via callback
        if (grassDrawCallback) {
            grassDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        }

        // Skinned character shadows via callback
        if (skinnedDrawCallback) {
            skinnedDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        }

        vkCmdEndRenderPass(cmd);
    }
}

void ShadowSystem::bindSkinnedShadowPipeline(VkCommandBuffer cmd, VkDescriptorSet descriptorSet) {
    if (skinnedShadowPipeline == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            skinnedShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
}

void ShadowSystem::recordSkinnedMeshShadow(VkCommandBuffer cmd, uint32_t cascade,
                                            const glm::mat4& modelMatrix,
                                            const SkinnedMesh& mesh) {
    if (skinnedShadowPipelineLayout == VK_NULL_HANDLE) return;

    ShadowPushConstants shadowPush{};
    shadowPush.model = modelMatrix;
    shadowPush.cascadeIndex = static_cast<int>(cascade);
    vkCmdPushConstants(cmd, skinnedShadowPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &shadowPush);

    VkBuffer vertexBuffers[] = {mesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.getIndexCount(), 1, 0, 0, 0);
}

void ShadowSystem::renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                        VkDescriptorSet descriptorSet,
                                        const std::vector<Renderable>& sceneObjects,
                                        const DrawCallback& terrainDrawCallback,
                                        const DrawCallback& grassDrawCallback,
                                        const DrawCallback& skinnedDrawCallback,
                                        const std::vector<Light>& visibleLights) {
    if (dynamicShadowPipeline == VK_NULL_HANDLE || shadowRenderPassDynamic == VK_NULL_HANDLE) {
        return;
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

    uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(visibleLights.size(), MAX_SHADOW_CASTING_LIGHTS));

    for (uint32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
        const Light& light = visibleLights[lightIndex];
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            if (frameIndex >= pointShadowFramebuffers.size()) continue;
            for (uint32_t face = 0; face < pointShadowFramebuffers[frameIndex].size(); face++) {
                VkRenderPassBeginInfo passInfo{};
                passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                passInfo.renderPass = shadowRenderPassDynamic;
                passInfo.framebuffer = pointShadowFramebuffers[frameIndex][face];
                passInfo.renderArea.offset = {0, 0};
                passInfo.renderArea.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

                VkClearValue clear{};
                clear.depthStencil = {1.0f, 0};
                passInfo.clearValueCount = 1;
                passInfo.pClearValues = &clear;

                vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        dynamicShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                for (const auto& obj : sceneObjects) {
                    if (!obj.castsShadow) continue;

                    ShadowPushConstants push{};
                    push.model = obj.transform;
                    push.cascadeIndex = static_cast<int>(face);
                    vkCmdPushConstants(cmd, dynamicShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(ShadowPushConstants), &push);

                    VkBuffer vb[] = {obj.mesh->getVertexBuffer()};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
                    vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
                }

                if (terrainDrawCallback) {
                    terrainDrawCallback(cmd, face, glm::mat4(1.0f));
                }
                if (grassDrawCallback) {
                    grassDrawCallback(cmd, face, glm::mat4(1.0f));
                }
                if (skinnedDrawCallback) {
                    skinnedDrawCallback(cmd, face, glm::mat4(1.0f));
                }

                vkCmdEndRenderPass(cmd);
            }
        } else {
            if (frameIndex >= spotShadowFramebuffers.size() ||
                lightIndex >= spotShadowFramebuffers[frameIndex].size()) {
                continue;
            }

            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = shadowRenderPassDynamic;
            passInfo.framebuffer = spotShadowFramebuffers[frameIndex][lightIndex];
            passInfo.renderArea.offset = {0, 0};
            passInfo.renderArea.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

            VkClearValue clear{};
            clear.depthStencil = {1.0f, 0};
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clear;

            vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    dynamicShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            for (const auto& obj : sceneObjects) {
                if (!obj.castsShadow) continue;

                ShadowPushConstants push{};
                push.model = obj.transform;
                push.cascadeIndex = static_cast<int>(lightIndex);
                vkCmdPushConstants(cmd, dynamicShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(ShadowPushConstants), &push);

                VkBuffer vb[] = {obj.mesh->getVertexBuffer()};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
                vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
            }

            if (terrainDrawCallback) {
                terrainDrawCallback(cmd, lightIndex, glm::mat4(1.0f));
            }
            if (grassDrawCallback) {
                grassDrawCallback(cmd, lightIndex, glm::mat4(1.0f));
            }
            if (skinnedDrawCallback) {
                skinnedDrawCallback(cmd, lightIndex, glm::mat4(1.0f));
            }

            vkCmdEndRenderPass(cmd);
        }
    }
}
