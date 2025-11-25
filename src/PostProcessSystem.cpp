#include "PostProcessSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <array>

bool PostProcessSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    outputRenderPass = info.outputRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    swapchainFormat = info.swapchainFormat;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createHDRRenderTarget()) return false;
    if (!createHDRRenderPass()) return false;
    if (!createHDRFramebuffer()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSets()) return false;
    if (!createCompositePipeline()) return false;

    return true;
}

void PostProcessSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyHDRResources();

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
        }
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    if (compositePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, compositePipeline, nullptr);
        compositePipeline = VK_NULL_HANDLE;
    }
    if (compositePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
        compositePipelineLayout = VK_NULL_HANDLE;
    }
    if (compositeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, compositeDescriptorSetLayout, nullptr);
        compositeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (hdrSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, hdrSampler, nullptr);
        hdrSampler = VK_NULL_HANDLE;
    }
    if (hdrRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, hdrRenderPass, nullptr);
        hdrRenderPass = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::destroyHDRResources() {
    if (hdrFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, hdrFramebuffer, nullptr);
        hdrFramebuffer = VK_NULL_HANDLE;
    }
    if (hdrColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hdrColorView, nullptr);
        hdrColorView = VK_NULL_HANDLE;
    }
    if (hdrColorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hdrColorImage, hdrColorAllocation);
        hdrColorImage = VK_NULL_HANDLE;
        hdrColorAllocation = VK_NULL_HANDLE;
    }
    if (hdrDepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, hdrDepthView, nullptr);
        hdrDepthView = VK_NULL_HANDLE;
    }
    if (hdrDepthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, hdrDepthImage, hdrDepthAllocation);
        hdrDepthImage = VK_NULL_HANDLE;
        hdrDepthAllocation = VK_NULL_HANDLE;
    }
}

void PostProcessSystem::resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    extent = newExtent;
    destroyHDRResources();
    createHDRRenderTarget();
    createHDRFramebuffer();

    // Update descriptor sets with new image view
    for (size_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrImageInfo.imageView = hdrColorView;
        hdrImageInfo.sampler = hdrSampler;

        VkWriteDescriptorSet imageWrite{};
        imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWrite.dstSet = compositeDescriptorSets[i];
        imageWrite.dstBinding = 0;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &hdrImageInfo;

        vkUpdateDescriptorSets(device, 1, &imageWrite, 0, nullptr);
    }
}

bool PostProcessSystem::createHDRRenderTarget() {
    // Create HDR color image
    VkImageCreateInfo colorImageInfo{};
    colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
    colorImageInfo.extent.width = extent.width;
    colorImageInfo.extent.height = extent.height;
    colorImageInfo.extent.depth = 1;
    colorImageInfo.mipLevels = 1;
    colorImageInfo.arrayLayers = 1;
    colorImageInfo.format = HDR_FORMAT;
    colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_STORAGE_BIT;
    colorImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo colorAllocInfo{};
    colorAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &colorImageInfo, &colorAllocInfo,
                       &hdrColorImage, &hdrColorAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image");
        return false;
    }

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = hdrColorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = HDR_FORMAT;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.baseMipLevel = 0;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.baseArrayLayer = 0;
    colorViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &colorViewInfo, nullptr, &hdrColorView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR color image view");
        return false;
    }

    // Create HDR depth image
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = extent.width;
    depthImageInfo.extent.height = extent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = DEPTH_FORMAT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &depthImageInfo, &depthAllocInfo,
                       &hdrDepthImage, &hdrDepthAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image");
        return false;
    }

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = hdrDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = DEPTH_FORMAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &depthViewInfo, nullptr, &hdrDepthView) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR depth image view");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = HDR_FORMAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = DEPTH_FORMAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &hdrRenderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR render pass");
        return false;
    }

    return true;
}

bool PostProcessSystem::createHDRFramebuffer() {
    std::array<VkImageView, 2> attachments = {hdrColorView, hdrDepthView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = hdrRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &hdrFramebuffer) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR framebuffer");
        return false;
    }

    return true;
}

bool PostProcessSystem::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &hdrSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create HDR sampler");
        return false;
    }

    return true;
}

bool PostProcessSystem::createDescriptorSetLayout() {
    // Binding 0: HDR input texture
    VkDescriptorSetLayoutBinding hdrBinding{};
    hdrBinding.binding = 0;
    hdrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrBinding.descriptorCount = 1;
    hdrBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: Uniform buffer
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 1;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {hdrBinding, uboBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &compositeDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create composite descriptor set layout");
        return false;
    }

    return true;
}

bool PostProcessSystem::createUniformBuffers() {
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(PostProcessUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (size_t i = 0; i < framesInFlight; i++) {
        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &uniformBuffers[i], &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create post-process uniform buffer");
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;

        // Initialize with defaults
        PostProcessUniforms* ubo = static_cast<PostProcessUniforms*>(uniformMappedPtrs[i]);
        ubo->exposure = 0.0f;
        ubo->bloomThreshold = 1.0f;
        ubo->bloomIntensity = 0.5f;
        ubo->autoExposure = 1.0f;  // Enable by default
    }

    return true;
}

bool PostProcessSystem::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, compositeDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    compositeDescriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, compositeDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate composite descriptor sets");
        return false;
    }

    for (size_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrImageInfo.imageView = hdrColorView;
        hdrImageInfo.sampler = hdrSampler;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PostProcessUniforms);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = compositeDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &hdrImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = compositeDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    return true;
}

bool PostProcessSystem::createCompositePipeline() {
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/postprocess.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/postprocess.frag.spv");

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        SDL_Log("Failed to load post-process shaders");
        return false;
    }

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

    // No vertex input (fullscreen triangle generated in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compositeDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &compositePipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create composite pipeline layout");
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = compositePipelineLayout;
    pipelineInfo.renderPass = outputRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create composite graphics pipeline");
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);

    return true;
}

void PostProcessSystem::recordPostProcess(VkCommandBuffer cmd, uint32_t frameIndex,
                                          VkFramebuffer swapchainFB, float deltaTime) {
    // Update uniform buffer
    PostProcessUniforms* ubo = static_cast<PostProcessUniforms*>(uniformMappedPtrs[frameIndex]);
    ubo->exposure = manualExposure;
    ubo->autoExposure = autoExposureEnabled ? 1.0f : 0.0f;
    ubo->previousExposure = lastAutoExposure;
    ubo->deltaTime = deltaTime;
    ubo->adaptationSpeed = 2.0f;  // Smooth adaptation over ~0.5 seconds
    ubo->bloomThreshold = bloomThreshold;
    ubo->bloomIntensity = bloomIntensity;
    ubo->bloomRadius = bloomRadius;

    // Update tracked exposure for next frame (estimate based on current)
    // Note: In fragment shader approach, all pixels compute the same exposure
    // The lastAutoExposure will be updated by the shader's output being read back
    // For now, we just slowly adapt the CPU-side value
    if (autoExposureEnabled) {
        // Simple approximation - let shader handle actual adaptation
        lastAutoExposure = ubo->previousExposure;
    }

    // Begin swapchain render pass for final composite
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = outputRenderPass;
    renderPassInfo.framebuffer = swapchainFB;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout,
                            0, 1, &compositeDescriptorSets[frameIndex], 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Draw fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}
