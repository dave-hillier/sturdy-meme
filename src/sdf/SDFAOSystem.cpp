#include "SDFAOSystem.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "CommandBufferUtils.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>

std::unique_ptr<SDFAOSystem> SDFAOSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<SDFAOSystem>(new SDFAOSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<SDFAOSystem> SDFAOSystem::create(const InitContext& ctx, SDFAtlas* atlas) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.commandPool = ctx.commandPool;
    info.computeQueue = ctx.graphicsQueue;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.extent = ctx.extent;
    info.descriptorPool = ctx.descriptorPool;
    info.sdfAtlas = atlas;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

SDFAOSystem::~SDFAOSystem() {
    cleanup();
}

bool SDFAOSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    computeQueue_ = info.computeQueue;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    extent_ = info.extent;
    descriptorPool_ = info.descriptorPool;
    sdfAtlas_ = info.sdfAtlas;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDFAOSystem requires raiiDevice");
        return false;
    }

    if (!sdfAtlas_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDFAOSystem requires SDFAtlas");
        return false;
    }

    // Use config from atlas
    maxDistance_ = sdfAtlas_->getConfig().maxDistance;
    intensity_ = sdfAtlas_->getConfig().aoIntensity;

    if (!createAOBuffer()) return false;
    if (!createComputePipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("SDFAOSystem initialized: %dx%d", extent_.width, extent_.height);
    return true;
}

void SDFAOSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    descriptorSets_.clear();
    computePipeline_.reset();
    computePipelineLayout_.reset();
    descriptorSetLayout_.reset();
    sampler_.reset();

    if (aoResultView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, aoResultView_, nullptr);
        aoResultView_ = VK_NULL_HANDLE;
    }
    if (aoResult_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, aoResult_, aoAllocation_);
        aoResult_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}

void SDFAOSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent_.width && newExtent.height == extent_.height) {
        return;
    }

    extent_ = newExtent;

    if (aoResultView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, aoResultView_, nullptr);
        aoResultView_ = VK_NULL_HANDLE;
    }
    if (aoResult_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, aoResult_, aoAllocation_);
        aoResult_ = VK_NULL_HANDLE;
    }

    createAOBuffer();
    createDescriptorSets();

    SDL_Log("SDFAOSystem resized to %dx%d", extent_.width, extent_.height);
}

bool SDFAOSystem::createAOBuffer() {
    // Half resolution for performance
    VkExtent2D aoExtent = {extent_.width / 2, extent_.height / 2};
    if (aoExtent.width < 1) aoExtent.width = 1;
    if (aoExtent.height < 1) aoExtent.height = 1;

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8Unorm)
        .setExtent(vk::Extent3D{aoExtent.width, aoExtent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &aoResult_, &aoAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO result image");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(aoResult_)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                          nullptr, &aoResultView_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO result view");
        return false;
    }

    // Create sampler
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxLod(0.0f);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO sampler: %s", e.what());
        return false;
    }

    // Transition to general layout
    {
        CommandScope cmdScope(device_, commandPool_, computeQueue_);
        if (!cmdScope.begin()) return false;
        Barriers::prepareImageForCompute(cmdScope.get(), aoResult_);
        if (!cmdScope.end()) return false;
    }

    return true;
}

bool SDFAOSystem::createComputePipeline() {
    // Descriptor set layout:
    // 0: Depth buffer (sampler2D)
    // 1: Normal buffer (sampler2D)
    // 2: SDF atlas (sampler3D)
    // 3: SDF entries buffer (SSBO)
    // 4: SDF instances buffer (SSBO)
    // 5: AO output (storage image)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device_)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: Depth
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Normal
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 2: SDF atlas
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 3: SDF entries
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 4: SDF instances
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 5: AO output
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(SDFAOPushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **descriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        computePipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO pipeline layout: %s", e.what());
        return false;
    }

    std::string shaderFile = shaderPath_ + "/sdf_ao.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load SDF-AO shader: %s", shaderFile.c_str());
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**computePipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF-AO compute pipeline");
        return false;
    }
    vkDestroyShaderModule(device_, *shaderModule, nullptr);
    computePipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("SDF-AO compute pipeline created");
    return true;
}

bool SDFAOSystem::createDescriptorSets() {
    if (!descriptorPool_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDFAOSystem: descriptor pool is null");
        return false;
    }

    descriptorSets_ = descriptorPool_->allocate(**descriptorSetLayout_, framesInFlight_);
    if (descriptorSets_.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate SDF-AO descriptor sets");
        return false;
    }

    return true;
}

void SDFAOSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                                 VkImageView depthView,
                                 VkImageView normalView,
                                 VkSampler depthSampler,
                                 const glm::mat4& invView, const glm::mat4& invProj,
                                 float nearPlane, float farPlane) {
    if (!enabled_ || descriptorSets_.empty() || sdfAtlas_->getInstanceCount() == 0) {
        return;
    }

    VkExtent2D aoExtent = {extent_.width / 2, extent_.height / 2};
    uint32_t groupsX = (aoExtent.width + 7) / 8;
    uint32_t groupsY = (aoExtent.height + 7) / 8;

    const auto& config = sdfAtlas_->getConfig();

    // Update descriptor set
    DescriptorManager::SetWriter(device_, descriptorSets_[frameIndex])
        .writeImage(0, depthView, depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .writeImage(1, normalView, depthSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeImage(2, sdfAtlas_->getAtlasView(), sdfAtlas_->getSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .writeBuffer(3, sdfAtlas_->getEntryBuffer(), 0, VK_WHOLE_SIZE)
        .writeBuffer(4, sdfAtlas_->getInstanceBuffer(), 0, VK_WHOLE_SIZE)
        .writeStorageImage(5, aoResultView_)
        .update();

    // Push constants
    SDFAOPushConstants pc{};
    pc.invViewMatrix = invView;
    pc.invProjMatrix = invProj;
    pc.screenParams = glm::vec4(
        static_cast<float>(aoExtent.width),
        static_cast<float>(aoExtent.height),
        1.0f / static_cast<float>(aoExtent.width),
        1.0f / static_cast<float>(aoExtent.height)
    );
    pc.aoParams = glm::vec4(
        static_cast<float>(config.numCones),
        static_cast<float>(config.maxSteps),
        config.coneAngle,
        maxDistance_
    );
    pc.aoParams2 = glm::vec4(
        intensity_,
        0.01f,  // bias
        static_cast<float>(config.resolution),
        static_cast<float>(sdfAtlas_->getInstanceCount())
    );
    pc.nearPlane = nearPlane;
    pc.farPlane = farPlane;

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **computePipelineLayout_, 0, vk::DescriptorSet(descriptorSets_[frameIndex]), {});
    vkCmd.pushConstants<SDFAOPushConstants>(
        **computePipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pc);

    vkCmd.dispatch(groupsX, groupsY, 1);

    // Barrier for fragment shader read
    Barriers::transitionImage(cmd, aoResult_,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}
