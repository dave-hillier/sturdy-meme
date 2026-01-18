#include "DisplacementSystem.h"
#include "EnvironmentSettings.h"
#include "InitContext.h"
#include "ComputePipelineBuilder.h"
#include "core/ImageBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/vulkan/SamplerFactory.h"
#include <SDL3/SDL.h>

// Uniforms for displacement update compute shader
struct DisplacementUniforms {
    glm::vec4 regionCenter;  // xy = world center, z = region size, w = texel size
    glm::vec4 params;        // x = decay rate, y = max displacement, z = delta time, w = num sources
};

DisplacementSystem::DisplacementSystem(ConstructToken) {}

std::unique_ptr<DisplacementSystem> DisplacementSystem::create(const InitInfo& info) {
    auto system = std::make_unique<DisplacementSystem>(ConstructToken{});
    if (!system->init(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<DisplacementSystem> DisplacementSystem::create(const InitContext& ctx) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

DisplacementSystem::~DisplacementSystem() {
    cleanup();
}

bool DisplacementSystem::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DisplacementSystem requires raiiDevice");
        return false;
    }

    if (!createTexture()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DisplacementSystem: Failed to create texture");
        return false;
    }

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DisplacementSystem: Failed to create buffers");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DisplacementSystem: Failed to create pipeline");
        return false;
    }

    SDL_Log("DisplacementSystem initialized successfully");
    return true;
}

void DisplacementSystem::cleanup() {
    if (!device_) return;

    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();
    sampler_.reset();

    if (imageView_) {
        device_.destroyImageView(imageView_);
        imageView_ = nullptr;
    }
    if (image_ && allocator_) {
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = nullptr;
        allocation_ = VK_NULL_HANDLE;
    }

    BufferUtils::destroyBuffers(allocator_, sourceBuffers_);
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);

    device_ = nullptr;
    raiiDevice_ = nullptr;
}

bool DisplacementSystem::createTexture() {
    // Create displacement texture (RG16F for XZ displacement vectors)
    ManagedImage image;
    VkImageView rawView = VK_NULL_HANDLE;
    if (!ImageBuilder(allocator_)
            .setExtent(GrassConstants::DISPLACEMENT_TEXTURE_SIZE, GrassConstants::DISPLACEMENT_TEXTURE_SIZE)
            .setFormat(VK_FORMAT_R16G16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build(static_cast<VkDevice>(device_), image, rawView)) {
        return false;
    }
    imageView_ = rawView;
    VkImage rawImage = VK_NULL_HANDLE;
    image.releaseToRaw(rawImage, allocation_);
    image_ = rawImage;

    // Create sampler for grass/leaf shaders to sample displacement
    sampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!sampler_) {
        return false;
    }

    return true;
}

bool DisplacementSystem::createBuffers() {
    VkDeviceSize sourceBufferSize = sizeof(DisplacementSource) * GrassConstants::MAX_DISPLACEMENT_SOURCES;
    VkDeviceSize uniformBufferSize = sizeof(DisplacementUniforms);

    BufferUtils::PerFrameBufferBuilder sourceBuilder;
    if (!sourceBuilder.setAllocator(allocator_)
             .setFrameCount(framesInFlight_)
             .setSize(sourceBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .build(sourceBuffers_)) {
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(allocator_)
             .setFrameCount(framesInFlight_)
             .setSize(uniformBufferSize)
             .build(uniformBuffers_)) {
        return false;
    }

    return true;
}

bool DisplacementSystem::createPipeline() {
    // Descriptor set layout:
    // 0: Displacement map (storage image, read-write)
    // 1: Source buffer (SSBO)
    // 2: Displacement uniforms (UBO)
    VkDescriptorSetLayout rawDescSetLayout = DescriptorManager::LayoutBuilder(device_)
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    if (rawDescSetLayout == VK_NULL_HANDLE) {
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawDescSetLayout);

    VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(
        device_, **descriptorSetLayout_);
    if (rawPipelineLayout == VK_NULL_HANDLE) {
        return false;
    }
    pipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

    if (!ComputePipelineBuilder(*raiiDevice_)
            .setShader(shaderPath_ + "/grass_displacement.comp.spv")
            .setPipelineLayout(**pipelineLayout_)
            .buildInto(pipeline_)) {
        return false;
    }

    // Allocate per-frame descriptor sets
    auto rawSets = descriptorPool_->allocate(**descriptorSetLayout_, framesInFlight_);
    if (rawSets.empty()) {
        return false;
    }
    descriptorSets_.resize(rawSets.size());
    for (size_t i = 0; i < rawSets.size(); ++i) {
        descriptorSets_[i] = rawSets[i];
    }

    // Write descriptor sets with image and per-frame buffers
    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        DescriptorManager::SetWriter(device_, descriptorSets_[i])
            .writeStorageImage(0, imageView_)
            .writeBuffer(1, sourceBuffers_.buffers[i], 0,
                         sizeof(DisplacementSource) * GrassConstants::MAX_DISPLACEMENT_SOURCES,
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, uniformBuffers_.buffers[i], 0, sizeof(DisplacementUniforms))
            .update();
    }

    return true;
}

void DisplacementSystem::updateSources(const glm::vec3& playerPos, float playerRadius, float /*deltaTime*/) {
    currentSources_.clear();

    // Add player as displacement source
    DisplacementSource playerSource;
    playerSource.positionAndRadius = glm::vec4(playerPos, playerRadius * 2.0f);  // Influence radius larger than capsule
    playerSource.strengthAndVelocity = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);  // Full strength, no velocity for now
    currentSources_.push_back(playerSource);
}

void DisplacementSystem::addSource(const DisplacementSource& source) {
    if (currentSources_.size() < GrassConstants::MAX_DISPLACEMENT_SOURCES) {
        currentSources_.push_back(source);
    }
}

void DisplacementSystem::updateRegionCenter(const glm::vec3& cameraPos) {
    regionCenter_ = glm::vec2(cameraPos.x, cameraPos.z);
}

void DisplacementSystem::recordUpdate(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Copy displacement sources to per-frame buffer
    memcpy(sourceBuffers_.mappedPointers[frameIndex], currentSources_.data(),
           sizeof(DisplacementSource) * currentSources_.size());

    // Update displacement uniforms
    DisplacementUniforms uniforms;
    uniforms.regionCenter = getRegionVec4();

    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings_ ? *environmentSettings_ : fallbackSettings;
    uniforms.params = glm::vec4(
        settings.grassDisplacementDecay,
        settings.grassMaxDisplacement,
        1.0f / 60.0f,  // deltaTime (assume 60fps for decay calculation)
        static_cast<float>(currentSources_.size())
    );
    memcpy(uniformBuffers_.mappedPointers[frameIndex], &uniforms, sizeof(DisplacementUniforms));

    // Transition displacement image to general layout if needed
    BarrierHelpers::imageToGeneral(cmd, image_);

    // Dispatch displacement update compute shader
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **pipelineLayout_, 0,
                           descriptorSets_[frameIndex], {});

    cmd.dispatch(GrassConstants::DISPLACEMENT_DISPATCH_SIZE, GrassConstants::DISPLACEMENT_DISPATCH_SIZE, 1);

    // Barrier: displacement compute write -> grass/leaf compute read
    BarrierHelpers::imageToShaderRead(cmd, image_, vk::PipelineStageFlagBits::eComputeShader);
}

vk::DescriptorImageInfo DisplacementSystem::getDescriptorInfo() const {
    return vk::DescriptorImageInfo{}
        .setSampler(getSampler())
        .setImageView(imageView_)
        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
}

glm::vec4 DisplacementSystem::getRegionVec4() const {
    return glm::vec4(
        regionCenter_.x,
        regionCenter_.y,
        GrassConstants::DISPLACEMENT_REGION_SIZE,
        GrassConstants::DISPLACEMENT_TEXEL_SIZE
    );
}
