#include "BloomSystem.h"
#include "ShaderLoader.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

using ShaderLoader::loadShaderModule;

bool BloomSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;

    if (!createRenderPass()) return false;
    if (!createMipChain()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createPipelines()) return false;
    if (!createDescriptorSets()) return false;

    return true;
}

void BloomSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyMipChain();

    if (downsamplePipeline) vkDestroyPipeline(device, downsamplePipeline, nullptr);
    if (downsamplePipelineLayout) vkDestroyPipelineLayout(device, downsamplePipelineLayout, nullptr);
    if (downsampleDescSetLayout) vkDestroyDescriptorSetLayout(device, downsampleDescSetLayout, nullptr);

    if (upsamplePipeline) vkDestroyPipeline(device, upsamplePipeline, nullptr);
    if (upsamplePipelineLayout) vkDestroyPipelineLayout(device, upsamplePipelineLayout, nullptr);
    if (upsampleDescSetLayout) vkDestroyDescriptorSetLayout(device, upsampleDescSetLayout, nullptr);

    if (sampler) vkDestroySampler(device, sampler, nullptr);
    if (downsampleRenderPass) vkDestroyRenderPass(device, downsampleRenderPass, nullptr);
    if (upsampleRenderPass) vkDestroyRenderPass(device, upsampleRenderPass, nullptr);

    downsampleDescSets.clear();
    upsampleDescSets.clear();
}

void BloomSystem::resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    extent = newExtent;

    destroyMipChain();

    // Recreate descriptor sets since we need new image views
    downsampleDescSets.clear();
    upsampleDescSets.clear();

    createMipChain();
    createDescriptorSets();
}

bool BloomSystem::createMipChain() {
    uint32_t width = extent.width;
    uint32_t height = extent.height;

    // Create mip chain - each level is half the size of the previous
    for (uint32_t i = 0; i < MAX_MIP_LEVELS && (width > 1 || height > 1); ++i) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);

        MipLevel mip;
        mip.extent = {width, height};

        // Create image
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = BLOOM_FORMAT;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &mip.image, &mip.allocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        // Create image view
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mip.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = BLOOM_FORMAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &mip.imageView) != VK_SUCCESS) {
            return false;
        }

        mipChain.push_back(mip);
    }

    SDL_Log("BloomSystem: Created %zu mip levels, first mip: %ux%u",
            mipChain.size(),
            mipChain.empty() ? 0 : mipChain[0].extent.width,
            mipChain.empty() ? 0 : mipChain[0].extent.height);

    // Create framebuffers for each mip level
    // Use downsampleRenderPass - both render passes have compatible attachments
    for (auto& mip : mipChain) {
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = downsampleRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &mip.imageView;
        fbInfo.width = mip.extent.width;
        fbInfo.height = mip.extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &mip.framebuffer) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool BloomSystem::createRenderPass() {
    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // Downsample render pass - DONT_CARE since we're writing fresh data
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = BLOOM_FORMAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &downsampleRenderPass) != VK_SUCCESS) {
            return false;
        }
    }

    // Upsample render pass - LOAD to preserve downsampled content for additive blending
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = BLOOM_FORMAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &upsampleRenderPass) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool BloomSystem::createSampler() {
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    return vkCreateSampler(device, &samplerInfo, nullptr, &sampler) == VK_SUCCESS;
}

bool BloomSystem::createDescriptorSetLayouts() {
    // Both downsample and upsample use the same descriptor set layout
    // Binding 0: input texture (sampler2D)
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &downsampleDescSetLayout) != VK_SUCCESS) {
        return false;
    }

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &upsampleDescSetLayout) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool BloomSystem::createPipelines() {
    // Load shaders
    auto downsampleVert = loadShaderModule(device, shaderPath + "/postprocess.vert.spv");
    auto downsampleFrag = loadShaderModule(device, shaderPath + "/bloom_downsample.frag.spv");
    auto upsampleVert = loadShaderModule(device, shaderPath + "/postprocess.vert.spv");
    auto upsampleFrag = loadShaderModule(device, shaderPath + "/bloom_upsample.frag.spv");

    if (!downsampleVert || !downsampleFrag || !upsampleVert || !upsampleFrag) {
        return false;
    }

    // Shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> downsampleStages = {};
    downsampleStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    downsampleStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    downsampleStages[0].module = downsampleVert;
    downsampleStages[0].pName = "main";
    downsampleStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    downsampleStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    downsampleStages[1].module = downsampleFrag;
    downsampleStages[1].pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> upsampleStages = {};
    upsampleStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    upsampleStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    upsampleStages[0].module = upsampleVert;
    upsampleStages[0].pName = "main";
    upsampleStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    upsampleStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    upsampleStages[1].module = upsampleFrag;
    upsampleStages[1].pName = "main";

    // Vertex input (empty - fullscreen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    // Color blending (no blending, just replace)
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic states
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for downsample
    VkPushConstantRange downsamplePushConstantRange = {};
    downsamplePushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    downsamplePushConstantRange.offset = 0;
    downsamplePushConstantRange.size = sizeof(DownsamplePushConstants);

    // Downsample pipeline layout
    VkPipelineLayoutCreateInfo downsampleLayoutInfo = {};
    downsampleLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    downsampleLayoutInfo.setLayoutCount = 1;
    downsampleLayoutInfo.pSetLayouts = &downsampleDescSetLayout;
    downsampleLayoutInfo.pushConstantRangeCount = 1;
    downsampleLayoutInfo.pPushConstantRanges = &downsamplePushConstantRange;

    if (vkCreatePipelineLayout(device, &downsampleLayoutInfo, nullptr, &downsamplePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Push constants for upsample
    VkPushConstantRange upsamplePushConstantRange = {};
    upsamplePushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    upsamplePushConstantRange.offset = 0;
    upsamplePushConstantRange.size = sizeof(UpsamplePushConstants);

    // Upsample pipeline layout
    VkPipelineLayoutCreateInfo upsampleLayoutInfo = {};
    upsampleLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    upsampleLayoutInfo.setLayoutCount = 1;
    upsampleLayoutInfo.pSetLayouts = &upsampleDescSetLayout;
    upsampleLayoutInfo.pushConstantRangeCount = 1;
    upsampleLayoutInfo.pPushConstantRanges = &upsamplePushConstantRange;

    if (vkCreatePipelineLayout(device, &upsampleLayoutInfo, nullptr, &upsamplePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create downsample pipeline
    VkGraphicsPipelineCreateInfo downsamplePipelineInfo = {};
    downsamplePipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    downsamplePipelineInfo.stageCount = static_cast<uint32_t>(downsampleStages.size());
    downsamplePipelineInfo.pStages = downsampleStages.data();
    downsamplePipelineInfo.pVertexInputState = &vertexInputInfo;
    downsamplePipelineInfo.pInputAssemblyState = &inputAssembly;
    downsamplePipelineInfo.pViewportState = &viewportState;
    downsamplePipelineInfo.pRasterizationState = &rasterizer;
    downsamplePipelineInfo.pMultisampleState = &multisampling;
    downsamplePipelineInfo.pColorBlendState = &colorBlending;
    downsamplePipelineInfo.pDynamicState = &dynamicState;
    downsamplePipelineInfo.layout = downsamplePipelineLayout;
    downsamplePipelineInfo.renderPass = downsampleRenderPass;
    downsamplePipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &downsamplePipelineInfo, nullptr, &downsamplePipeline) != VK_SUCCESS) {
        return false;
    }

    // Create upsample pipeline (additive blending for accumulation)
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkGraphicsPipelineCreateInfo upsamplePipelineInfo = {};
    upsamplePipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    upsamplePipelineInfo.stageCount = static_cast<uint32_t>(upsampleStages.size());
    upsamplePipelineInfo.pStages = upsampleStages.data();
    upsamplePipelineInfo.pVertexInputState = &vertexInputInfo;
    upsamplePipelineInfo.pInputAssemblyState = &inputAssembly;
    upsamplePipelineInfo.pViewportState = &viewportState;
    upsamplePipelineInfo.pRasterizationState = &rasterizer;
    upsamplePipelineInfo.pMultisampleState = &multisampling;
    upsamplePipelineInfo.pColorBlendState = &colorBlending;
    upsamplePipelineInfo.pDynamicState = &dynamicState;
    upsamplePipelineInfo.layout = upsamplePipelineLayout;
    upsamplePipelineInfo.renderPass = upsampleRenderPass;
    upsamplePipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &upsamplePipelineInfo, nullptr, &upsamplePipeline) != VK_SUCCESS) {
        return false;
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device, downsampleVert, nullptr);
    vkDestroyShaderModule(device, downsampleFrag, nullptr);
    vkDestroyShaderModule(device, upsampleVert, nullptr);
    vkDestroyShaderModule(device, upsampleFrag, nullptr);

    return true;
}

bool BloomSystem::createDescriptorSets() {
    // Allocate descriptor sets for downsample (one per mip level)
    std::vector<VkDescriptorSetLayout> downsampleLayouts(mipChain.size(), downsampleDescSetLayout);

    VkDescriptorSetAllocateInfo downsampleAllocInfo = {};
    downsampleAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    downsampleAllocInfo.descriptorPool = descriptorPool;
    downsampleAllocInfo.descriptorSetCount = static_cast<uint32_t>(downsampleLayouts.size());
    downsampleAllocInfo.pSetLayouts = downsampleLayouts.data();

    downsampleDescSets.resize(mipChain.size());
    if (vkAllocateDescriptorSets(device, &downsampleAllocInfo, downsampleDescSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Allocate descriptor sets for upsample (one per mip level except the smallest)
    if (mipChain.size() > 1) {
        std::vector<VkDescriptorSetLayout> upsampleLayouts(mipChain.size() - 1, upsampleDescSetLayout);

        VkDescriptorSetAllocateInfo upsampleAllocInfo = {};
        upsampleAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        upsampleAllocInfo.descriptorPool = descriptorPool;
        upsampleAllocInfo.descriptorSetCount = static_cast<uint32_t>(upsampleLayouts.size());
        upsampleAllocInfo.pSetLayouts = upsampleLayouts.data();

        upsampleDescSets.resize(mipChain.size() - 1);
        if (vkAllocateDescriptorSets(device, &upsampleAllocInfo, upsampleDescSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

void BloomSystem::destroyMipChain() {
    for (auto& mip : mipChain) {
        if (mip.framebuffer) vkDestroyFramebuffer(device, mip.framebuffer, nullptr);
        if (mip.imageView) vkDestroyImageView(device, mip.imageView, nullptr);
        if (mip.image) vmaDestroyImage(allocator, mip.image, mip.allocation);
    }
    mipChain.clear();
}

void BloomSystem::recordBloomPass(VkCommandBuffer cmd, VkImageView hdrInput) {
    if (mipChain.empty()) return;

    // Transition HDR input to shader read layout
    VkImageMemoryBarrier hdrBarrier = {};
    hdrBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    hdrBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    hdrBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdrBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.image = VK_NULL_HANDLE; // Will be set by caller if needed
    hdrBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    hdrBarrier.subresourceRange.baseMipLevel = 0;
    hdrBarrier.subresourceRange.levelCount = 1;
    hdrBarrier.subresourceRange.baseArrayLayer = 0;
    hdrBarrier.subresourceRange.layerCount = 1;

    // Downsample pass - from HDR to smallest mip
    for (size_t i = 0; i < mipChain.size(); ++i) {
        // Update descriptor set to sample from previous level
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = sampler;
        if (i == 0) {
            imageInfo.imageView = hdrInput;
        } else {
            imageInfo.imageView = mipChain[i - 1].imageView;
        }
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = downsampleDescSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // Begin render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = downsampleRenderPass;
        renderPassInfo.framebuffer = mipChain[i].framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = mipChain[i].extent;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(mipChain[i].extent.width);
        viewport.height = static_cast<float>(mipChain[i].extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = mipChain[i].extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind pipeline and descriptor set
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipelineLayout,
                               0, 1, &downsampleDescSets[i], 0, nullptr);

        // Push constants - use SOURCE resolution for texel size calculation
        DownsamplePushConstants pushConstants = {};
        if (i == 0) {
            // First pass samples from HDR input at full resolution
            pushConstants.resolutionX = static_cast<float>(extent.width);
            pushConstants.resolutionY = static_cast<float>(extent.height);
        } else {
            // Subsequent passes sample from previous mip level
            pushConstants.resolutionX = static_cast<float>(mipChain[i - 1].extent.width);
            pushConstants.resolutionY = static_cast<float>(mipChain[i - 1].extent.height);
        }
        pushConstants.threshold = threshold;
        pushConstants.isFirstPass = (i == 0) ? 1 : 0;

        vkCmdPushConstants(cmd, downsamplePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(DownsamplePushConstants), &pushConstants);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    // Upsample pass - from smallest mip back to largest
    // Blend upsampled results additively into each level
    for (int i = static_cast<int>(mipChain.size()) - 2; i >= 0; --i) {
        // Update descriptor set to sample from smaller mip (i+1)
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = sampler;
        imageInfo.imageView = mipChain[i + 1].imageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = upsampleDescSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // Transition current mip to color attachment for blending
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mipChain[i].image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Begin render pass with LOAD operation to preserve downsampled content
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = upsampleRenderPass;
        renderPassInfo.framebuffer = mipChain[i].framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = mipChain[i].extent;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(mipChain[i].extent.width);
        viewport.height = static_cast<float>(mipChain[i].extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = mipChain[i].extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind pipeline and descriptor set
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upsamplePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upsamplePipelineLayout,
                               0, 1, &upsampleDescSets[i], 0, nullptr);

        // Push constants - use SOURCE resolution (the smaller mip being sampled)
        UpsamplePushConstants pushConstants = {};
        pushConstants.resolutionX = static_cast<float>(mipChain[i + 1].extent.width);
        pushConstants.resolutionY = static_cast<float>(mipChain[i + 1].extent.height);
        pushConstants.filterRadius = 1.0f;

        vkCmdPushConstants(cmd, upsamplePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(UpsamplePushConstants), &pushConstants);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    // Final mip is now in SHADER_READ_ONLY_OPTIMAL and ready for compositing
}
