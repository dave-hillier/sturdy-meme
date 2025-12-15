#include "BloomSystem.h"
#include "GraphicsPipelineFactory.h"
#include "VulkanBarriers.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

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

bool BloomSystem::init(const InitContext& ctx) {
    device = ctx.device;
    allocator = ctx.allocator;
    descriptorPool = ctx.descriptorPool;
    extent = ctx.extent;
    shaderPath = ctx.shaderPath;

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
    VkDescriptorSetLayoutBinding binding{};
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
    // Create pipeline layouts with push constants
    VkPushConstantRange downsamplePushConstantRange = {};
    downsamplePushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    downsamplePushConstantRange.offset = 0;
    downsamplePushConstantRange.size = sizeof(DownsamplePushConstants);

    VkPipelineLayoutCreateInfo downsampleLayoutInfo = {};
    downsampleLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    downsampleLayoutInfo.setLayoutCount = 1;
    downsampleLayoutInfo.pSetLayouts = &downsampleDescSetLayout;
    downsampleLayoutInfo.pushConstantRangeCount = 1;
    downsampleLayoutInfo.pPushConstantRanges = &downsamplePushConstantRange;

    if (vkCreatePipelineLayout(device, &downsampleLayoutInfo, nullptr, &downsamplePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange upsamplePushConstantRange = {};
    upsamplePushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    upsamplePushConstantRange.offset = 0;
    upsamplePushConstantRange.size = sizeof(UpsamplePushConstants);

    VkPipelineLayoutCreateInfo upsampleLayoutInfo = {};
    upsampleLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    upsampleLayoutInfo.setLayoutCount = 1;
    upsampleLayoutInfo.pSetLayouts = &upsampleDescSetLayout;
    upsampleLayoutInfo.pushConstantRangeCount = 1;
    upsampleLayoutInfo.pPushConstantRanges = &upsamplePushConstantRange;

    if (vkCreatePipelineLayout(device, &upsampleLayoutInfo, nullptr, &upsamplePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create downsample pipeline using factory
    GraphicsPipelineFactory factory(device);

    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_downsample.frag.spv")
        .setRenderPass(downsampleRenderPass)
        .setPipelineLayout(downsamplePipelineLayout)
        .setDynamicViewport(true)
        .build(downsamplePipeline);

    if (!success) {
        return false;
    }

    // Create upsample pipeline with additive blending
    success = factory.reset()
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_upsample.frag.spv")
        .setRenderPass(upsampleRenderPass)
        .setPipelineLayout(upsamplePipelineLayout)
        .setDynamicViewport(true)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Additive)
        .build(upsamplePipeline);

    if (!success) {
        return false;
    }

    return true;
}

bool BloomSystem::createDescriptorSets() {
    // Allocate descriptor sets for downsample (one per mip level) using managed pool
    downsampleDescSets = descriptorPool->allocate(downsampleDescSetLayout, static_cast<uint32_t>(mipChain.size()));
    if (downsampleDescSets.size() != mipChain.size()) {
        SDL_Log("BloomSystem: Failed to allocate downsample descriptor sets");
        return false;
    }

    // Allocate descriptor sets for upsample (one per mip level except the smallest)
    if (mipChain.size() > 1) {
        upsampleDescSets = descriptorPool->allocate(upsampleDescSetLayout, static_cast<uint32_t>(mipChain.size() - 1));
        if (upsampleDescSets.size() != mipChain.size() - 1) {
            SDL_Log("BloomSystem: Failed to allocate upsample descriptor sets");
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
        Barriers::transitionImage(cmd, mipChain[i].image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

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
