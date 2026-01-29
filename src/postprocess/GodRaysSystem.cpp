#include "GodRaysSystem.h"
#include "SamplerFactory.h"
#include "DescriptorManager.h"
#include "core/InitInfoBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "ShaderLoader.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>

std::unique_ptr<GodRaysSystem> GodRaysSystem::create(const InitInfo& info) {
    auto system = std::make_unique<GodRaysSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<GodRaysSystem> GodRaysSystem::create(const InitContext& ctx) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    return create(info);
}

GodRaysSystem::~GodRaysSystem() {
    cleanup();
}

bool GodRaysSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem requires raiiDevice");
        return false;
    }

    // Create linear sampler
    sampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!sampler_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create sampler");
        return false;
    }

    if (!createResources()) return false;
    if (!createPipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("GodRaysSystem: Initialized at quarter resolution (%ux%u)",
            quarterExtent_.width, quarterExtent_.height);
    return true;
}

void GodRaysSystem::cleanup() {
    if (!device_) return;

    destroyResources();

    pipeline_.reset();
    pipelineLayout_.reset();
    descSetLayout_.reset();
    sampler_.reset();

    device_ = VK_NULL_HANDLE;
}

void GodRaysSystem::resize(VkExtent2D newExtent) {
    extent_ = newExtent;
    destroyResources();
    createResources();
    createDescriptorSets();
}

bool GodRaysSystem::createResources() {
    // Quarter resolution
    quarterExtent_.width = std::max(1u, extent_.width / 4);
    quarterExtent_.height = std::max(1u, extent_.height / 4);

    // Create output image
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{quarterExtent_.width, quarterExtent_.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &outputImage_, &outputAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create output image");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(outputImage_)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

    vk::Device vkDevice(device_);
    auto result = vkDevice.createImageView(viewInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create output image view");
        return false;
    }
    outputImageView_ = result.value;

    return true;
}

bool GodRaysSystem::createPipeline() {
    // Descriptor set layout: hdrInput (0), depthInput (1), output (2)
    if (!DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::combinedImageSampler(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::combinedImageSampler(1, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageImage(2, vk::ShaderStageFlagBits::eCompute))
            .buildInto(*raiiDevice_, descSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create descriptor set layout");
        return false;
    }

    // Pipeline layout
    auto pipelineLayoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**descSetLayout_)
        .addPushConstantRange<PushConstants>(vk::ShaderStageFlagBits::eCompute)
        .build();
    if (!pipelineLayoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(pipelineLayoutOpt);

    // Load shader
    auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderPath_ + "/godrays_compute.comp.spv");
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to load godrays_compute.comp.spv");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**pipelineLayout_);

    try {
        auto result = raiiDevice_->createComputePipeline(nullptr, pipelineInfo);
        pipeline_.emplace(std::move(result));
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to create pipeline: %s", e.what());
        vkDestroyShaderModule(device_, *shaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device_, *shaderModule, nullptr);
    return true;
}

bool GodRaysSystem::createDescriptorSets() {
    auto sets = descriptorPool_->allocate(**descSetLayout_, 1);
    if (sets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GodRaysSystem: Failed to allocate descriptor set");
        return false;
    }
    descSet_ = sets[0];
    return true;
}

void GodRaysSystem::destroyResources() {
    if (outputImageView_) {
        vkDestroyImageView(device_, outputImageView_, nullptr);
        outputImageView_ = VK_NULL_HANDLE;
    }
    if (outputImage_) {
        vmaDestroyImage(allocator_, outputImage_, outputAllocation_);
        outputImage_ = VK_NULL_HANDLE;
        outputAllocation_ = VK_NULL_HANDLE;
    }
}

void GodRaysSystem::recordGodRaysPass(VkCommandBuffer cmd, VkImageView hdrView, VkImageView depthView) {
    vk::CommandBuffer vkCmd(cmd);

    // Transition output image to GENERAL
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setImage(outputImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);
    }

    // Update descriptor set
    DescriptorManager::SetWriter(device_, descSet_)
        .writeImage(0, hdrView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeImage(1, depthView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeStorageImage(2, outputImageView_, VK_IMAGE_LAYOUT_GENERAL)
        .update();

    // Bind pipeline and descriptor set
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **pipelineLayout_,
                             0, vk::DescriptorSet(descSet_), {});

    // Push constants
    PushConstants pc = {};
    pc.sunScreenPosX = sunScreenPos_.x;
    pc.sunScreenPosY = sunScreenPos_.y;
    pc.intensity = intensity_;
    pc.decay = decay_;
    pc.nearPlane = nearPlane_;
    pc.farPlane = farPlane_;
    pc.bloomThreshold = bloomThreshold_;
    pc.sampleCount = sampleCount_;

    vkCmd.pushConstants<PushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eCompute,
        0, pc);

    // Dispatch
    uint32_t groupsX = (quarterExtent_.width + 7) / 8;
    uint32_t groupsY = (quarterExtent_.height + 7) / 8;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Transition to SHADER_READ_ONLY for postprocess sampling
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImage(outputImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier);
    }
}
