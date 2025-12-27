#include "BloomSystem.h"
#include "GraphicsPipelineFactory.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include "core/ImageBuilder.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

using namespace vk;  // Vulkan-Hpp type-safe wrappers

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

    // Reset RAII members before device is destroyed
    upsamplePipeline_ = ManagedPipeline();
    upsamplePipelineLayout_ = ManagedPipelineLayout();
    upsampleDescSetLayout_ = ManagedDescriptorSetLayout();

    downsamplePipeline_ = ManagedPipeline();
    downsamplePipelineLayout_ = ManagedPipelineLayout();
    downsampleDescSetLayout_ = ManagedDescriptorSetLayout();

    sampler_ = ManagedSampler();
    upsampleRenderPass_ = ManagedRenderPass();
    downsampleRenderPass_ = ManagedRenderPass();

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
        ManagedImageView managedView;
        if (!ImageBuilder(allocator)
                .setExtent(width, height)
                .setFormat(BLOOM_FORMAT)
                .asColorAttachment()
                .setGpuOnly()
                .build(device, managedImage, managedView)) {
            return false;
        }

        // Release to raw handles (BloomSystem uses raw handles for mip chain)
        managedImage.releaseToRaw(mip.image, mip.allocation);
        mip.imageView = managedView.release();

        mipChain.push_back(mip);
    }

    SDL_Log("BloomSystem: Created %zu mip levels, first mip: %ux%u",
            mipChain.size(),
            mipChain.empty() ? 0 : mipChain[0].extent.width,
            mipChain.empty() ? 0 : mipChain[0].extent.height);

    // Create framebuffers for each mip level
    // Use downsampleRenderPass - both render passes have compatible attachments
    for (auto& mip : mipChain) {
        FramebufferCreateInfo fbInfo{
            {},                                      // flags
            downsampleRenderPass_.get(),
            1, reinterpret_cast<const ImageView*>(&mip.imageView),
            mip.extent.width,
            mip.extent.height,
            1                                        // layers
        };

        auto vkFbInfo = static_cast<VkFramebufferCreateInfo>(fbInfo);
        if (vkCreateFramebuffer(device, &vkFbInfo, nullptr, &mip.framebuffer) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool BloomSystem::createRenderPass() {
    AttachmentReference colorRef{0, ImageLayout::eColorAttachmentOptimal};

    SubpassDescription subpass{
        {},                              // flags
        PipelineBindPoint::eGraphics,
        0, nullptr,                      // inputAttachmentCount, pInputAttachments
        1, &colorRef,                    // colorAttachmentCount, pColorAttachments
        nullptr,                         // pResolveAttachments
        nullptr                          // pDepthStencilAttachment
    };

    SubpassDependency dependency{
        VK_SUBPASS_EXTERNAL, 0,          // srcSubpass, dstSubpass
        PipelineStageFlagBits::eColorAttachmentOutput,
        PipelineStageFlagBits::eFragmentShader,
        AccessFlagBits::eColorAttachmentWrite,
        AccessFlagBits::eShaderRead
    };

    // Downsample render pass - DONT_CARE since we're writing fresh data
    {
        AttachmentDescription colorAttachment{
            {},                                  // flags
            static_cast<Format>(BLOOM_FORMAT),
            SampleCountFlagBits::e1,
            AttachmentLoadOp::eDontCare,
            AttachmentStoreOp::eStore,
            AttachmentLoadOp::eDontCare,         // stencilLoadOp
            AttachmentStoreOp::eDontCare,        // stencilStoreOp
            ImageLayout::eUndefined,
            ImageLayout::eShaderReadOnlyOptimal
        };

        RenderPassCreateInfo renderPassInfo{
            {},                              // flags
            colorAttachment,
            subpass,
            dependency
        };

        auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
        if (!ManagedRenderPass::create(device, vkRenderPassInfo, downsampleRenderPass_)) {
            return false;
        }
    }

    // Upsample render pass - LOAD to preserve downsampled content for additive blending
    {
        AttachmentDescription colorAttachment{
            {},                                  // flags
            static_cast<Format>(BLOOM_FORMAT),
            SampleCountFlagBits::e1,
            AttachmentLoadOp::eLoad,
            AttachmentStoreOp::eStore,
            AttachmentLoadOp::eDontCare,         // stencilLoadOp
            AttachmentStoreOp::eDontCare,        // stencilStoreOp
            ImageLayout::eColorAttachmentOptimal,
            ImageLayout::eShaderReadOnlyOptimal
        };

        RenderPassCreateInfo renderPassInfo{
            {},                              // flags
            colorAttachment,
            subpass,
            dependency
        };

        auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
        if (!ManagedRenderPass::create(device, vkRenderPassInfo, upsampleRenderPass_)) {
            return false;
        }
    }

    return true;
}

bool BloomSystem::createSampler() {
    // Use the convenience factory for linear clamp sampler
    return VulkanResourceFactory::createSamplerLinearClamp(device, sampler_);
}

bool BloomSystem::createDescriptorSetLayouts() {
    // Both downsample and upsample use the same descriptor set layout
    // Binding 0: input texture (sampler2D)
    DescriptorSetLayoutBinding binding{
        0,                                       // binding
        DescriptorType::eCombinedImageSampler,
        1,                                       // descriptorCount
        ShaderStageFlagBits::eFragment
    };

    DescriptorSetLayoutCreateInfo layoutInfo{
        {},                                      // flags
        binding
    };

    auto vkLayoutInfo = static_cast<VkDescriptorSetLayoutCreateInfo>(layoutInfo);
    if (!ManagedDescriptorSetLayout::create(device, vkLayoutInfo, downsampleDescSetLayout_)) {
        return false;
    }

    if (!ManagedDescriptorSetLayout::create(device, vkLayoutInfo, upsampleDescSetLayout_)) {
        return false;
    }

    return true;
}

bool BloomSystem::createPipelines() {
    // Create pipeline layouts with push constants
    PushConstantRange downsamplePushConstantRange{
        ShaderStageFlagBits::eFragment,
        0,
        sizeof(DownsamplePushConstants)
    };

    VkDescriptorSetLayout downsampleLayout = downsampleDescSetLayout_.get();
    PipelineLayoutCreateInfo downsampleLayoutInfo{
        {},                                      // flags
        1, reinterpret_cast<const DescriptorSetLayout*>(&downsampleLayout),
        1, &downsamplePushConstantRange
    };

    auto vkDownsampleLayoutInfo = static_cast<VkPipelineLayoutCreateInfo>(downsampleLayoutInfo);
    if (!ManagedPipelineLayout::create(device, vkDownsampleLayoutInfo, downsamplePipelineLayout_)) {
        return false;
    }

    PushConstantRange upsamplePushConstantRange{
        ShaderStageFlagBits::eFragment,
        0,
        sizeof(UpsamplePushConstants)
    };

    VkDescriptorSetLayout upsampleLayout = upsampleDescSetLayout_.get();
    PipelineLayoutCreateInfo upsampleLayoutInfo{
        {},                                      // flags
        1, reinterpret_cast<const DescriptorSetLayout*>(&upsampleLayout),
        1, &upsamplePushConstantRange
    };

    auto vkUpsampleLayoutInfo = static_cast<VkPipelineLayoutCreateInfo>(upsampleLayoutInfo);
    if (!ManagedPipelineLayout::create(device, vkUpsampleLayoutInfo, upsamplePipelineLayout_)) {
        return false;
    }

    // Create downsample pipeline using factory
    GraphicsPipelineFactory factory(device);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_downsample.frag.spv")
        .setRenderPass(downsampleRenderPass_.get())
        .setPipelineLayout(downsamplePipelineLayout_.get())
        .setDynamicViewport(true)
        .build(rawPipeline);

    if (!success) {
        return false;
    }
    downsamplePipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);

    // Create upsample pipeline with additive blending
    rawPipeline = VK_NULL_HANDLE;
    success = factory.reset()
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/postprocess.vert.spv", shaderPath + "/bloom_upsample.frag.spv")
        .setRenderPass(upsampleRenderPass_.get())
        .setPipelineLayout(upsamplePipelineLayout_.get())
        .setDynamicViewport(true)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Additive)
        .build(rawPipeline);

    if (!success) {
        return false;
    }
    upsamplePipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);

    return true;
}

bool BloomSystem::createDescriptorSets() {
    // Allocate descriptor sets for downsample (one per mip level) using managed pool
    downsampleDescSets = descriptorPool->allocate(downsampleDescSetLayout_.get(), static_cast<uint32_t>(mipChain.size()));
    if (downsampleDescSets.size() != mipChain.size()) {
        SDL_Log("BloomSystem: Failed to allocate downsample descriptor sets");
        return false;
    }

    // Allocate descriptor sets for upsample (one per mip level except the smallest)
    if (mipChain.size() > 1) {
        upsampleDescSets = descriptorPool->allocate(upsampleDescSetLayout_.get(), static_cast<uint32_t>(mipChain.size() - 1));
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
        VkImageView sourceView = (i == 0) ? hdrInput : mipChain[i - 1].imageView;
        DescriptorManager::SetWriter(device, downsampleDescSets[i])
            .writeImage(0, sourceView, sampler_.get())
            .update();

        // Begin render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = downsampleRenderPass_.get();
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
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipeline_.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipelineLayout_.get(),
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

        vkCmdPushConstants(cmd, downsamplePipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(DownsamplePushConstants), &pushConstants);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    // Upsample pass - from smallest mip back to largest
    // Blend upsampled results additively into each level
    for (int i = static_cast<int>(mipChain.size()) - 2; i >= 0; --i) {
        // Update descriptor set to sample from smaller mip (i+1)
        DescriptorManager::SetWriter(device, upsampleDescSets[i])
            .writeImage(0, mipChain[i + 1].imageView, sampler_.get())
            .update();

        // Transition current mip to color attachment for blending
        Barriers::transitionImage(cmd, mipChain[i].image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        // Begin render pass with LOAD operation to preserve downsampled content
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = upsampleRenderPass_.get();
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
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upsamplePipeline_.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upsamplePipelineLayout_.get(),
                               0, 1, &upsampleDescSets[i], 0, nullptr);

        // Push constants - use SOURCE resolution (the smaller mip being sampled)
        UpsamplePushConstants pushConstants = {};
        pushConstants.resolutionX = static_cast<float>(mipChain[i + 1].extent.width);
        pushConstants.resolutionY = static_cast<float>(mipChain[i + 1].extent.height);
        pushConstants.filterRadius = 1.0f;

        vkCmdPushConstants(cmd, upsamplePipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(UpsamplePushConstants), &pushConstants);

        // Draw fullscreen triangle
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    // Final mip is now in SHADER_READ_ONLY_OPTIMAL and ready for compositing
}
