#include "BloomSystem.h"
#include "GraphicsPipelineFactory.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include "core/ImageBuilder.h"
#include <vulkan/vulkan.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

std::unique_ptr<BloomSystem> BloomSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<BloomSystem>(new BloomSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<BloomSystem> BloomSystem::create(const InitContext& ctx) {
    InitInfo info;
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

BloomSystem::~BloomSystem() {
    cleanup();
}

bool BloomSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "BloomSystem requires raiiDevice");
        return false;
    }

    if (!createRenderPass()) return false;
    if (!createMipChain()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createPipelines()) return false;
    if (!createDescriptorSets()) return false;

    return true;
}

void BloomSystem::cleanup() {
    if (!device) return;  // Not initialized

    destroyMipChain();

    // Reset RAII members (order matters - pipelines before layouts)
    upsamplePipeline_.reset();
    upsamplePipelineLayout_.reset();
    upsampleDescSetLayout_.reset();

    downsamplePipeline_.reset();
    downsamplePipelineLayout_.reset();
    downsampleDescSetLayout_.reset();

    sampler_.reset();
    upsampleRenderPass_.reset();
    downsampleRenderPass_.reset();

    downsampleDescSets.clear();
    upsampleDescSets.clear();

    device = VK_NULL_HANDLE;
}

void BloomSystem::resize(VkExtent2D newExtent) {
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

        // Create image using ImageBuilder
        ManagedImage managedImage;
        VkImageView imageView;
        if (!ImageBuilder(allocator)
                .setExtent(width, height)
                .setFormat(BLOOM_FORMAT)
                .asColorAttachment()
                .setGpuOnly()
                .build(device, managedImage, imageView)) {
            return false;
        }

        // Release to raw handles (BloomSystem uses raw handles for mip chain)
        managedImage.releaseToRaw(mip.image, mip.allocation);
        mip.imageView = imageView;

        mipChain.push_back(mip);
    }

    SDL_Log("BloomSystem: Created %zu mip levels, first mip: %ux%u",
            mipChain.size(),
            mipChain.empty() ? 0 : mipChain[0].extent.width,
            mipChain.empty() ? 0 : mipChain[0].extent.height);

    // Create framebuffers for each mip level using vulkan-hpp builder
    // Use downsampleRenderPass - both render passes have compatible attachments
    for (auto& mip : mipChain) {
        auto fbInfo = vk::FramebufferCreateInfo{}
            .setRenderPass(**downsampleRenderPass_)
            .setAttachmentCount(1)
            .setPAttachments(reinterpret_cast<const vk::ImageView*>(&mip.imageView))
            .setWidth(mip.extent.width)
            .setHeight(mip.extent.height)
            .setLayers(1);

        if (vkCreateFramebuffer(device, reinterpret_cast<const VkFramebufferCreateInfo*>(&fbInfo), nullptr, &mip.framebuffer) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool BloomSystem::createRenderPass() {
    // Using vulkan-hpp builders for render pass creation
    auto colorRef = vk::AttachmentReference{}
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(1)
        .setPColorAttachments(&colorRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setDstStageMask(vk::PipelineStageFlagBits::eFragmentShader)
        .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    // Downsample render pass - DONT_CARE since we're writing fresh data
    {
        auto colorAttachment = vk::AttachmentDescription{}
            .setFormat(static_cast<vk::Format>(BLOOM_FORMAT))
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        auto renderPassInfo = vk::RenderPassCreateInfo{}
            .setAttachmentCount(1)
            .setPAttachments(&colorAttachment)
            .setSubpassCount(1)
            .setPSubpasses(&subpass)
            .setDependencyCount(1)
            .setPDependencies(&dependency);

        try {
            downsampleRenderPass_.emplace(*raiiDevice_, renderPassInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create downsample render pass: %s", e.what());
            return false;
        }
    }

    // Upsample render pass - LOAD to preserve downsampled content for additive blending
    {
        auto colorAttachment = vk::AttachmentDescription{}
            .setFormat(static_cast<vk::Format>(BLOOM_FORMAT))
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

        auto renderPassInfo = vk::RenderPassCreateInfo{}
            .setAttachmentCount(1)
            .setPAttachments(&colorAttachment)
            .setSubpassCount(1)
            .setPSubpasses(&subpass)
            .setDependencyCount(1)
            .setPDependencies(&dependency);

        try {
            upsampleRenderPass_.emplace(*raiiDevice_, renderPassInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create upsample render pass: %s", e.what());
            return false;
        }
    }

    return true;
}

bool BloomSystem::createSampler() {
    // Use the convenience factory for linear clamp sampler
    sampler_ = VulkanResourceFactory::createSamplerLinearClamp(*raiiDevice_);
    return sampler_.has_value();
}

bool BloomSystem::createDescriptorSetLayouts() {
    // Both downsample and upsample use the same descriptor set layout
    // Binding 0: input texture (sampler2D) - using vulkan-hpp builder
    auto binding = vk::DescriptorSetLayoutBinding{}
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindingCount(1)
        .setPBindings(&binding);

    try {
        downsampleDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
        upsampleDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create descriptor set layouts: %s", e.what());
        return false;
    }

    return true;
}

bool BloomSystem::createPipelines() {
    // Create pipeline layouts with push constants using vulkan-hpp builders
    auto downsamplePushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(DownsamplePushConstants));

    vk::DescriptorSetLayout downsampleLayout = **downsampleDescSetLayout_;
    auto downsampleLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayoutCount(1)
        .setPSetLayouts(&downsampleLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&downsamplePushConstantRange);

    try {
        downsamplePipelineLayout_.emplace(*raiiDevice_, downsampleLayoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create downsample pipeline layout: %s", e.what());
        return false;
    }

    auto upsamplePushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(UpsamplePushConstants));

    vk::DescriptorSetLayout upsampleLayout = **upsampleDescSetLayout_;
    auto upsampleLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayoutCount(1)
        .setPSetLayouts(&upsampleLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&upsamplePushConstantRange);

    try {
        upsamplePipelineLayout_.emplace(*raiiDevice_, upsampleLayoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create upsample pipeline layout: %s", e.what());
        return false;
    }

    // Create downsample pipeline using factory
    GraphicsPipelineFactory factory(device);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_downsample.frag.spv")
        .setRenderPass(**downsampleRenderPass_)
        .setPipelineLayout(**downsamplePipelineLayout_)
        .setDynamicViewport(true)
        .build(rawPipeline);

    if (!success) {
        return false;
    }
    // Adopt the raw pipeline into RAII wrapper
    downsamplePipeline_.emplace(*raiiDevice_, rawPipeline);

    // Create upsample pipeline with additive blending
    rawPipeline = VK_NULL_HANDLE;
    success = factory.reset()
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_upsample.frag.spv")
        .setRenderPass(**upsampleRenderPass_)
        .setPipelineLayout(**upsamplePipelineLayout_)
        .setDynamicViewport(true)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Additive)
        .build(rawPipeline);

    if (!success) {
        return false;
    }
    // Adopt the raw pipeline into RAII wrapper
    upsamplePipeline_.emplace(*raiiDevice_, rawPipeline);

    return true;
}

bool BloomSystem::createDescriptorSets() {
    // Allocate descriptor sets for downsample (one per mip level) using managed pool
    downsampleDescSets = descriptorPool->allocate(**downsampleDescSetLayout_, static_cast<uint32_t>(mipChain.size()));
    if (downsampleDescSets.size() != mipChain.size()) {
        SDL_Log("BloomSystem: Failed to allocate downsample descriptor sets");
        return false;
    }

    // Allocate descriptor sets for upsample (one per mip level except the smallest)
    if (mipChain.size() > 1) {
        upsampleDescSets = descriptorPool->allocate(**upsampleDescSetLayout_, static_cast<uint32_t>(mipChain.size() - 1));
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

    vk::CommandBuffer vkCmd(cmd);

    // Downsample pass - from HDR to smallest mip
    for (size_t i = 0; i < mipChain.size(); ++i) {
        // Update descriptor set to sample from previous level
        VkImageView sourceView = (i == 0) ? hdrInput : mipChain[i - 1].imageView;
        DescriptorManager::SetWriter(device, downsampleDescSets[i])
            .writeImage(0, sourceView, **sampler_)
            .update();

        // Begin render pass
        vk::Extent2D mipExtent{mipChain[i].extent.width, mipChain[i].extent.height};
        auto renderPassInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(**downsampleRenderPass_)
            .setFramebuffer(mipChain[i].framebuffer)
            .setRenderArea(vk::Rect2D{{0, 0}, mipExtent});

        vkCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Set viewport and scissor
        auto viewport = vk::Viewport{}
            .setX(0.0f)
            .setY(0.0f)
            .setWidth(static_cast<float>(mipChain[i].extent.width))
            .setHeight(static_cast<float>(mipChain[i].extent.height))
            .setMinDepth(0.0f)
            .setMaxDepth(1.0f);
        vkCmd.setViewport(0, viewport);

        auto scissor = vk::Rect2D{}
            .setOffset({0, 0})
            .setExtent(mipExtent);
        vkCmd.setScissor(0, scissor);

        // Bind pipeline and descriptor set
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **downsamplePipeline_);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **downsamplePipelineLayout_,
                                 0, vk::DescriptorSet(downsampleDescSets[i]), {});

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

        vkCmd.pushConstants<DownsamplePushConstants>(
            **downsamplePipelineLayout_,
            vk::ShaderStageFlagBits::eFragment,
            0, pushConstants);

        // Draw fullscreen triangle
        vkCmd.draw(3, 1, 0, 0);

        vkCmd.endRenderPass();
    }

    // Upsample pass - from smallest mip back to largest
    // Blend upsampled results additively into each level
    for (int i = static_cast<int>(mipChain.size()) - 2; i >= 0; --i) {
        // Update descriptor set to sample from smaller mip (i+1)
        DescriptorManager::SetWriter(device, upsampleDescSets[i])
            .writeImage(0, mipChain[i + 1].imageView, **sampler_)
            .update();

        // Transition current mip to color attachment for blending
        Barriers::transitionImage(cmd, mipChain[i].image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        // Begin render pass with LOAD operation to preserve downsampled content
        vk::Extent2D mipExtent{mipChain[i].extent.width, mipChain[i].extent.height};
        auto renderPassInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(**upsampleRenderPass_)
            .setFramebuffer(mipChain[i].framebuffer)
            .setRenderArea(vk::Rect2D{{0, 0}, mipExtent});

        vkCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Set viewport and scissor
        auto viewport = vk::Viewport{}
            .setX(0.0f)
            .setY(0.0f)
            .setWidth(static_cast<float>(mipChain[i].extent.width))
            .setHeight(static_cast<float>(mipChain[i].extent.height))
            .setMinDepth(0.0f)
            .setMaxDepth(1.0f);
        vkCmd.setViewport(0, viewport);

        auto scissor = vk::Rect2D{}
            .setOffset({0, 0})
            .setExtent(mipExtent);
        vkCmd.setScissor(0, scissor);

        // Bind pipeline and descriptor set
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **upsamplePipeline_);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **upsamplePipelineLayout_,
                                 0, vk::DescriptorSet(upsampleDescSets[i]), {});

        // Push constants - use SOURCE resolution (the smaller mip being sampled)
        UpsamplePushConstants pushConstants = {};
        pushConstants.resolutionX = static_cast<float>(mipChain[i + 1].extent.width);
        pushConstants.resolutionY = static_cast<float>(mipChain[i + 1].extent.height);
        pushConstants.filterRadius = 1.0f;

        vkCmd.pushConstants<UpsamplePushConstants>(
            **upsamplePipelineLayout_,
            vk::ShaderStageFlagBits::eFragment,
            0, pushConstants);

        // Draw fullscreen triangle
        vkCmd.draw(3, 1, 0, 0);

        vkCmd.endRenderPass();
    }

    // Final mip is now in SHADER_READ_ONLY_OPTIMAL and ready for compositing
}
