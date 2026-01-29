#include "ComputeBloomSystem.h"
#include "SamplerFactory.h"
#include "DescriptorManager.h"
#include "core/ImageBuilder.h"
#include "core/InitInfoBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "ShaderLoader.h"
#include <vulkan/vulkan.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

std::unique_ptr<ComputeBloomSystem> ComputeBloomSystem::create(const InitInfo& info) {
    auto system = std::make_unique<ComputeBloomSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<ComputeBloomSystem> ComputeBloomSystem::create(const InitContext& ctx) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    return create(info);
}

ComputeBloomSystem::~ComputeBloomSystem() {
    cleanup();
}

bool ComputeBloomSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    raiiDevice_ = info.raiiDevice;
    halfResFirstPass_ = info.halfResFirstPass;
    useAsyncCompute_ = info.useAsyncCompute;
    asyncComputeQueue_ = info.asyncComputeQueue;
    asyncComputeQueueFamily_ = info.asyncComputeQueueFamily;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputeBloomSystem requires raiiDevice");
        return false;
    }

    if (!createSampler()) return false;
    if (!createMipChain()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createPipelines()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("ComputeBloomSystem: Initialized with compute-based bloom");
    SDL_Log("  - Half-res first pass: %s", halfResFirstPass_ ? "enabled" : "disabled");
    SDL_Log("  - Mip levels: %zu", mipChain_.size());
    SDL_Log("  - Async compute: %s", useAsyncCompute_ ? "enabled" : "disabled");
    return true;
}

void ComputeBloomSystem::cleanup() {
    if (!device_) return;

    destroyMipChain();

    upsamplePipeline_.reset();
    upsamplePipelineLayout_.reset();
    upsampleDescSetLayout_.reset();

    downsamplePipeline_.reset();
    downsamplePipelineLayout_.reset();
    downsampleDescSetLayout_.reset();

    sampler_.reset();

    downsampleDescSets_.clear();
    upsampleDescSets_.clear();

    device_ = VK_NULL_HANDLE;
}

void ComputeBloomSystem::resize(VkExtent2D newExtent) {
    extent_ = newExtent;
    destroyMipChain();
    downsampleDescSets_.clear();
    upsampleDescSets_.clear();
    createMipChain();
    createDescriptorSets();
}

bool ComputeBloomSystem::createMipChain() {
    uint32_t width = extent_.width;
    uint32_t height = extent_.height;

    // Half-res first pass: start bloom chain at 1/4 resolution instead of 1/2
    // This reduces pixel shader work by 4x for the first downsample pass
    // Visual quality is nearly identical since bloom is inherently blurry
    if (halfResFirstPass_) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }

    for (uint32_t i = 0; i < MAX_MIP_LEVELS && (width > 1 || height > 1); ++i) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);

        MipLevel mip;
        mip.extent = {width, height};

        // Create image with STORAGE usage for compute shaders
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(static_cast<vk::Format>(BLOOM_FORMAT))
            .setExtent(vk::Extent3D{width, height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &mip.image, &mip.allocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputeBloomSystem: Failed to create mip image");
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(mip.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(static_cast<vk::Format>(BLOOM_FORMAT))
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vk::Device vkDevice(device_);
        auto result = vkDevice.createImageView(viewInfo);
        if (result.result != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputeBloomSystem: Failed to create mip image view");
            return false;
        }
        mip.imageView = result.value;

        mipChain_.push_back(mip);
    }

    SDL_Log("ComputeBloomSystem: Created %zu mip levels (half-res first pass: %s)",
            mipChain_.size(), halfResFirstPass_ ? "yes" : "no");
    if (!mipChain_.empty()) {
        SDL_Log("  First mip: %ux%u, Last mip: %ux%u",
                mipChain_[0].extent.width, mipChain_[0].extent.height,
                mipChain_.back().extent.width, mipChain_.back().extent.height);
    }

    return true;
}

bool ComputeBloomSystem::createSampler() {
    sampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    return sampler_.has_value();
}

bool ComputeBloomSystem::createDescriptorSetLayouts() {
    // Downsample layout: sampler2D input (binding 0), image2D output (binding 1)
    if (!DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::combinedImageSampler(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageImage(1, vk::ShaderStageFlagBits::eCompute))
            .buildInto(*raiiDevice_, downsampleDescSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create downsample descriptor set layout");
        return false;
    }

    // Upsample layout: sampler2D input (binding 0), image2D output (binding 1) for read-modify-write
    if (!DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::combinedImageSampler(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageImage(1, vk::ShaderStageFlagBits::eCompute))
            .buildInto(*raiiDevice_, upsampleDescSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create upsample descriptor set layout");
        return false;
    }

    return true;
}

bool ComputeBloomSystem::createPipelines() {
    // Create downsample compute pipeline
    {
        auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**downsampleDescSetLayout_)
            .addPushConstantRange<DownsamplePushConstants>(vk::ShaderStageFlagBits::eCompute)
            .build();
        if (!layoutOpt) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create downsample pipeline layout");
            return false;
        }
        downsamplePipelineLayout_ = std::move(layoutOpt);

        auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderPath_ + "/bloom_compute.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bloom_compute.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(*shaderModule)
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**downsamplePipelineLayout_);

        try {
            auto result = raiiDevice_->createComputePipeline(nullptr, pipelineInfo);
            downsamplePipeline_.emplace(std::move(result));
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create downsample pipeline: %s", e.what());
            vkDestroyShaderModule(device_, *shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device_, *shaderModule, nullptr);
    }

    // Create upsample compute pipeline
    {
        auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**upsampleDescSetLayout_)
            .addPushConstantRange<UpsamplePushConstants>(vk::ShaderStageFlagBits::eCompute)
            .build();
        if (!layoutOpt) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create upsample pipeline layout");
            return false;
        }
        upsamplePipelineLayout_ = std::move(layoutOpt);

        auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderPath_ + "/bloom_upsample_compute.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bloom_upsample_compute.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(*shaderModule)
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**upsamplePipelineLayout_);

        try {
            auto result = raiiDevice_->createComputePipeline(nullptr, pipelineInfo);
            upsamplePipeline_.emplace(std::move(result));
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create upsample pipeline: %s", e.what());
            vkDestroyShaderModule(device_, *shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device_, *shaderModule, nullptr);
    }

    return true;
}

bool ComputeBloomSystem::createDescriptorSets() {
    // Allocate downsample descriptor sets (one per mip level + one for HDR input)
    downsampleDescSets_ = descriptorPool_->allocate(**downsampleDescSetLayout_, static_cast<uint32_t>(mipChain_.size()));
    if (downsampleDescSets_.size() != mipChain_.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputeBloomSystem: Failed to allocate downsample descriptor sets");
        return false;
    }

    // Allocate upsample descriptor sets (one per mip level except smallest)
    if (mipChain_.size() > 1) {
        upsampleDescSets_ = descriptorPool_->allocate(**upsampleDescSetLayout_, static_cast<uint32_t>(mipChain_.size() - 1));
        if (upsampleDescSets_.size() != mipChain_.size() - 1) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputeBloomSystem: Failed to allocate upsample descriptor sets");
            return false;
        }
    }

    return true;
}

void ComputeBloomSystem::destroyMipChain() {
    for (auto& mip : mipChain_) {
        if (mip.imageView) vkDestroyImageView(device_, mip.imageView, nullptr);
        if (mip.image) vmaDestroyImage(allocator_, mip.image, mip.allocation);
    }
    mipChain_.clear();
}

void ComputeBloomSystem::recordBloomPass(VkCommandBuffer cmd, VkImage hdrImage, VkImageView hdrView) {
    if (mipChain_.empty()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Transition all mip images to GENERAL for compute access
    for (size_t i = 0; i < mipChain_.size(); ++i) {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setImage(mipChain_[i].image)
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);
    }

    // ========== DOWNSAMPLE PASSES ==========
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **downsamplePipeline_);

    for (size_t i = 0; i < mipChain_.size(); ++i) {
        // Update descriptor set
        VkImageView srcView = (i == 0) ? hdrView : mipChain_[i - 1].imageView;
        VkExtent2D srcExtent = (i == 0) ? extent_ : mipChain_[i - 1].extent;

        DescriptorManager::SetWriter(device_, downsampleDescSets_[i])
            .writeImage(0, srcView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .writeStorageImage(1, mipChain_[i].imageView, VK_IMAGE_LAYOUT_GENERAL)
            .update();

        // Barrier: wait for previous mip to be written before reading
        if (i > 0) {
            auto barrier = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setImage(mipChain_[i - 1].image)
                .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

            vkCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier);
        }

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **downsamplePipelineLayout_,
                                 0, vk::DescriptorSet(downsampleDescSets_[i]), {});

        DownsamplePushConstants pc = {};
        pc.srcResolutionX = static_cast<float>(srcExtent.width);
        pc.srcResolutionY = static_cast<float>(srcExtent.height);
        pc.threshold = threshold_;
        pc.isFirstPass = (i == 0) ? 1 : 0;

        vkCmd.pushConstants<DownsamplePushConstants>(
            **downsamplePipelineLayout_,
            vk::ShaderStageFlagBits::eCompute,
            0, pc);

        // Dispatch: one thread per output pixel, workgroup size 8x8
        uint32_t groupsX = (mipChain_[i].extent.width + 7) / 8;
        uint32_t groupsY = (mipChain_[i].extent.height + 7) / 8;
        vkCmd.dispatch(groupsX, groupsY, 1);
    }

    // Barrier before upsample: last mip needs to transition
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImage(mipChain_.back().image)
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);
    }

    // ========== UPSAMPLE PASSES ==========
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **upsamplePipeline_);

    for (int i = static_cast<int>(mipChain_.size()) - 2; i >= 0; --i) {
        // Transition destination to GENERAL for read-modify-write
        {
            auto barrier = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eGeneral)
                .setImage(mipChain_[i].image)
                .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

            vkCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier);
        }

        // Update descriptor set: read from smaller mip, write to larger mip
        DescriptorManager::SetWriter(device_, upsampleDescSets_[i])
            .writeImage(0, mipChain_[i + 1].imageView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .writeStorageImage(1, mipChain_[i].imageView, VK_IMAGE_LAYOUT_GENERAL)
            .update();

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **upsamplePipelineLayout_,
                                 0, vk::DescriptorSet(upsampleDescSets_[i]), {});

        UpsamplePushConstants pc = {};
        pc.srcResolutionX = static_cast<float>(mipChain_[i + 1].extent.width);
        pc.srcResolutionY = static_cast<float>(mipChain_[i + 1].extent.height);
        pc.filterRadius = 1.0f;
        pc.padding = 0.0f;

        vkCmd.pushConstants<UpsamplePushConstants>(
            **upsamplePipelineLayout_,
            vk::ShaderStageFlagBits::eCompute,
            0, pc);

        uint32_t groupsX = (mipChain_[i].extent.width + 7) / 8;
        uint32_t groupsY = (mipChain_[i].extent.height + 7) / 8;
        vkCmd.dispatch(groupsX, groupsY, 1);

        // Barrier: transition source mip for next iteration (or final read)
        if (i > 0) {
            auto barrier = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
                .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setImage(mipChain_[i + 1].image)
                .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

            vkCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier);
        }
    }

    // Final barrier: transition output mip to shader read for postprocess
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImage(mipChain_[0].image)
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier);
    }
}
