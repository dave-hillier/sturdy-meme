#include "ScreenSpaceShadowSystem.h"
#include "ShaderLoader.h"
#include "core/ImageBuilder.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "SamplerFactory.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

std::unique_ptr<ScreenSpaceShadowSystem> ScreenSpaceShadowSystem::create(const InitContext& ctx) {
    auto system = std::make_unique<ScreenSpaceShadowSystem>(ConstructToken{});
    if (!system->initInternal(ctx)) {
        return nullptr;
    }
    return system;
}

ScreenSpaceShadowSystem::~ScreenSpaceShadowSystem() {
    cleanup();
}

bool ScreenSpaceShadowSystem::initInternal(const InitContext& ctx) {
    device_ = ctx.device;
    allocator_ = ctx.allocator;
    descriptorPool_ = ctx.descriptorPool;
    raiiDevice_ = ctx.raiiDevice;
    extent_ = ctx.extent;
    shaderPath_ = ctx.shaderPath;
    framesInFlight_ = ctx.framesInFlight;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem requires raiiDevice");
        return false;
    }

    if (!createShadowBuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem: Failed to create shadow buffer");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem: Failed to create pipeline");
        return false;
    }

    if (!createUniformBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem: Failed to create uniform buffers");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("ScreenSpaceShadowSystem: Initialized (%ux%u)", extent_.width, extent_.height);
    return true;
}

void ScreenSpaceShadowSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    descriptorSets_.clear();
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);

    pipeline_.reset();
    pipelineLayout_.reset();
    descSetLayout_.reset();

    shadowBufferSampler_.reset();
    shadowBufferView_.reset();
    shadowBufferImage_.reset();

    device_ = VK_NULL_HANDLE;
}

bool ScreenSpaceShadowSystem::createShadowBuffer() {
    // Create R8_UNORM image for shadow buffer (storage + sampled)
    if (!ImageBuilder(allocator_)
            .setExtent(extent_)
            .setFormat(SHADOW_BUFFER_FORMAT)
            .asStorageImage()
            .build(*raiiDevice_, shadowBufferImage_, shadowBufferView_)) {
        return false;
    }

    // Create sampler for reading shadow buffer in HDR shaders
    shadowBufferSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!shadowBufferSampler_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScreenSpaceShadowSystem: Failed to create sampler");
        return false;
    }

    return true;
}

bool ScreenSpaceShadowSystem::createPipeline() {
    // Descriptor set layout:
    // Binding 0: Shadow buffer output (storage image, write-only)
    // Binding 1: Previous frame depth (combined image sampler)
    // Binding 2: Cascade shadow maps (combined image sampler)
    // Binding 3: Uniforms (UBO)
    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device_)
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)                  // 0: shadow buffer output
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)         // 1: prev depth
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)         // 2: shadow map array
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)                // 3: uniforms
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        return false;
    }
    descSetLayout_.emplace(*raiiDevice_, rawLayout);

    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descSetLayout_)
            .buildInto(pipelineLayout_)) {
        return false;
    }

    return ComputePipelineBuilder(*raiiDevice_)
        .setShader(shaderPath_ + "/shadow_resolve.comp.spv")
        .setPipelineLayout(**pipelineLayout_)
        .buildInto(pipeline_);
}

bool ScreenSpaceShadowSystem::createUniformBuffers() {
    return BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(sizeof(ShadowResolveUBO))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers_);
}

bool ScreenSpaceShadowSystem::createDescriptorSets() {
    descriptorSets_ = descriptorPool_->allocate(**descSetLayout_, framesInFlight_);
    if (descriptorSets_.size() != framesInFlight_) {
        return false;
    }

    // Initial write with shadow buffer (always available)
    // Depth and shadow map sources are written when setDepthSource/setShadowMapSource are called
    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        DescriptorManager::SetWriter(device_, descriptorSets_[i])
            .writeStorageImage(0, **shadowBufferView_)
            .writeBuffer(3, uniformBuffers_.buffers[i], 0, sizeof(ShadowResolveUBO))
            .update();
    }

    return true;
}

void ScreenSpaceShadowSystem::updateDescriptorSets() {
    if (!descriptorsNeedUpdate_) return;
    if (depthView_ == VK_NULL_HANDLE || shadowMapView_ == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        DescriptorManager::SetWriter(device_, descriptorSets_[i])
            .writeStorageImage(0, **shadowBufferView_)
            .writeImage(1, depthView_, depthSampler_,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeImage(2, shadowMapView_, shadowMapSampler_,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeBuffer(3, uniformBuffers_.buffers[i], 0, sizeof(ShadowResolveUBO))
            .update();
    }

    descriptorsNeedUpdate_ = false;
}

void ScreenSpaceShadowSystem::updatePerFrame(uint32_t frameIndex,
                                              const glm::mat4& view,
                                              const glm::mat4& proj,
                                              const glm::mat4 cascadeViewProj[4],
                                              const glm::vec4& cascadeSplits,
                                              const glm::vec3& lightDir,
                                              float shadowMapSize) {
    glm::mat4 currentViewProj = proj * view;

    ShadowResolveUBO ubo{};
    // Use previous frame's inverse viewProj for depth reconstruction
    // On first frame, use current (no temporal data yet)
    ubo.prevInvViewProj = hasPrevFrame_ ? glm::inverse(prevViewProj_) : glm::inverse(currentViewProj);
    ubo.view = view;
    for (int i = 0; i < 4; ++i) {
        ubo.cascadeViewProj[i] = cascadeViewProj[i];
    }
    ubo.cascadeSplits = cascadeSplits;
    ubo.lightDir = glm::vec4(lightDir, shadowMapSize);

    memcpy(uniformBuffers_.mappedPointers[frameIndex], &ubo, sizeof(ShadowResolveUBO));

    // Store for next frame
    prevViewProj_ = currentViewProj;
    hasPrevFrame_ = true;
}

void ScreenSpaceShadowSystem::setDepthSource(VkImageView depthView, VkSampler depthSampler) {
    depthView_ = depthView;
    depthSampler_ = depthSampler;
    descriptorsNeedUpdate_ = true;
}

void ScreenSpaceShadowSystem::setShadowMapSource(VkImageView shadowMapView, VkSampler shadowMapSampler) {
    shadowMapView_ = shadowMapView;
    shadowMapSampler_ = shadowMapSampler;
    descriptorsNeedUpdate_ = true;
}

void ScreenSpaceShadowSystem::record(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (depthView_ == VK_NULL_HANDLE || shadowMapView_ == VK_NULL_HANDLE) {
        return;
    }

    // Update descriptors if sources changed
    updateDescriptorSets();

    vk::CommandBuffer vkCmd(cmd);

    // Transition shadow buffer to General for compute write
    BarrierHelpers::imageToGeneral(vkCmd, shadowBufferImage_.get(),
        vk::ImageAspectFlagBits::eColor, 1);

    // Bind pipeline and descriptor set
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets_[frameIndex]), {});

    // Dispatch
    uint32_t groupsX = (extent_.width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    uint32_t groupsY = (extent_.height + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Transition shadow buffer to shader read for HDR pass
    auto barrier = vk::ImageMemoryBarrier2{}
        .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
        .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
        .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setImage(shadowBufferImage_.get())
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

    vkCmd.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
}

void ScreenSpaceShadowSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent_.width && newExtent.height == extent_.height) {
        return;
    }

    vkDeviceWaitIdle(device_);
    extent_ = newExtent;

    // Recreate shadow buffer with new size
    shadowBufferView_.reset();
    shadowBufferImage_.reset();
    createShadowBuffer();

    // Update descriptors to point to new shadow buffer
    descriptorsNeedUpdate_ = true;
    updateDescriptorSets();
}

VkImageView ScreenSpaceShadowSystem::getShadowBufferView() const {
    return shadowBufferView_ ? **shadowBufferView_ : VK_NULL_HANDLE;
}

VkSampler ScreenSpaceShadowSystem::getShadowBufferSampler() const {
    return shadowBufferSampler_ ? **shadowBufferSampler_ : VK_NULL_HANDLE;
}
