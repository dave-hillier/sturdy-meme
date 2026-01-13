#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "InitContext.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "VmaResources.h"
#include "DescriptorManager.h"
#include "core/vulkan/BarrierHelpers.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <array>

std::unique_ptr<SnowMaskSystem> SnowMaskSystem::create(const InitInfo& info) {
    std::unique_ptr<SnowMaskSystem> system(new SnowMaskSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::optional<SnowMaskSystem::Bundle> SnowMaskSystem::createWithDependencies(
    const InitContext& ctx,
    VkRenderPass hdrRenderPass
) {
    // Create snow mask system
    InitInfo snowMaskInfo{};
    snowMaskInfo.device = ctx.device;
    snowMaskInfo.allocator = ctx.allocator;
    snowMaskInfo.renderPass = hdrRenderPass;
    snowMaskInfo.descriptorPool = ctx.descriptorPool;
    snowMaskInfo.extent = ctx.extent;
    snowMaskInfo.shaderPath = ctx.shaderPath;
    snowMaskInfo.framesInFlight = ctx.framesInFlight;
    snowMaskInfo.raiiDevice = ctx.raiiDevice;

    auto snowMaskSystem = create(snowMaskInfo);
    if (!snowMaskSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SnowMaskSystem");
        return std::nullopt;
    }

    // Create volumetric snow system
    VolumetricSnowSystem::InitInfo volumetricSnowInfo{};
    volumetricSnowInfo.device = ctx.device;
    volumetricSnowInfo.allocator = ctx.allocator;
    volumetricSnowInfo.renderPass = hdrRenderPass;
    volumetricSnowInfo.descriptorPool = ctx.descriptorPool;
    volumetricSnowInfo.extent = ctx.extent;
    volumetricSnowInfo.shaderPath = ctx.shaderPath;
    volumetricSnowInfo.framesInFlight = ctx.framesInFlight;
    volumetricSnowInfo.raiiDevice = ctx.raiiDevice;

    auto volumetricSnowSystem = VolumetricSnowSystem::create(volumetricSnowInfo);
    if (!volumetricSnowSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VolumetricSnowSystem");
        return std::nullopt;
    }

    return Bundle{
        std::move(snowMaskSystem),
        std::move(volumetricSnowSystem)
    };
}

SnowMaskSystem::~SnowMaskSystem() {
    cleanup();
}

bool SnowMaskSystem::initInternal(const InitInfo& info) {
    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = []() { return true; };  // No graphics pipeline
    hooks.createGraphicsPipeline = []() { return true; };             // No graphics pipeline
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };
    hooks.usesGraphicsPipeline = []() { return false; };  // Compute-only system

    return lifecycle.init(info, hooks);
}

void SnowMaskSystem::cleanup() {
    if (!lifecycle.getDevice()) return;  // Not initialized

    snowMaskSampler_.reset();
    if (snowMaskView) {
        vkDestroyImageView(lifecycle.getDevice(), snowMaskView, nullptr);
        snowMaskView = VK_NULL_HANDLE;
    }
    if (snowMaskImage) {
        vmaDestroyImage(lifecycle.getAllocator(), snowMaskImage, snowMaskAllocation);
        snowMaskImage = VK_NULL_HANDLE;
    }

    lifecycle.destroy(lifecycle.getDevice(), lifecycle.getAllocator());
}

void SnowMaskSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
    BufferUtils::destroyBuffers(alloc, interactionBuffers);
}

bool SnowMaskSystem::createBuffers() {
    VkDeviceSize uniformBufferSize = sizeof(SnowMaskUniforms);
    VkDeviceSize interactionBufferSize = sizeof(SnowInteractionSource) * MAX_INTERACTIONS;

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create snow mask uniform buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder interactionBuilder;
    if (!interactionBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(interactionBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .build(interactionBuffers)) {
        SDL_Log("Failed to create snow interaction buffers");
        return false;
    }

    return createSnowMaskTexture();
}

bool SnowMaskSystem::createSnowMaskTexture() {
    // Create snow mask texture (R16F, single channel for coverage 0-1)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{SNOW_MASK_SIZE, SNOW_MASK_SIZE, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(vk::Format::eR16Sfloat)  // R16F for coverage value
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setSamples(vk::SampleCountFlagBits::e1);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(getAllocator(), reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &snowMaskImage, &snowMaskAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create snow mask image");
        return false;
    }

    // Create image view
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(snowMaskImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(getDevice(), reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &snowMaskView) != VK_SUCCESS) {
        SDL_Log("Failed to create snow mask image view");
        return false;
    }

    // Create sampler for other systems to sample the snow mask
    if (auto* raiiDevice = lifecycle.getRaiiDevice(); raiiDevice) {
        snowMaskSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice);
        if (!snowMaskSampler_) {
            SDL_Log("Failed to create snow mask sampler");
            return false;
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RAII device not available for snow mask sampler");
        return false;
    }

    return true;
}

bool SnowMaskSystem::createComputeDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    // binding 0: snow mask storage image (read/write)
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 1: uniform buffer
           .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 2: interaction sources SSBO
           .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(getComputePipelineHandles().descriptorSetLayout);
}

bool SnowMaskSystem::createComputePipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/snow_accumulation.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

    if (!builder.buildPipelineLayout({getComputePipelineHandles().descriptorSetLayout},
                                      getComputePipelineHandles().pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(getComputePipelineHandles().pipelineLayout,
                                         getComputePipelineHandles().pipeline);
}

bool SnowMaskSystem::createDescriptorSets() {
    // Allocate descriptor sets using managed pool
    computeDescriptorSets = getDescriptorPool()->allocate(
        getComputePipelineHandles().descriptorSetLayout, getFramesInFlight());
    if (computeDescriptorSets.size() != getFramesInFlight()) {
        SDL_Log("Failed to allocate snow mask descriptor sets");
        return false;
    }

    // Update descriptor sets with image binding (same image for all frames)
    for (uint32_t i = 0; i < getFramesInFlight(); i++) {
        DescriptorManager::SetWriter(getDevice(), computeDescriptorSets[i])
            .writeStorageImage(0, snowMaskView)
            .writeBuffer(1, uniformBuffers.buffers[i], 0, sizeof(SnowMaskUniforms))
            .writeBuffer(2, interactionBuffers.buffers[i], 0, sizeof(SnowInteractionSource) * MAX_INTERACTIONS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    return true;
}

void SnowMaskSystem::updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing,
                                     float weatherIntensity, const EnvironmentSettings& settings) {
    maskSize = settings.snowMaskSize;

    float texelSize = maskSize / static_cast<float>(SNOW_MASK_SIZE);

    SnowMaskUniforms uniforms{};
    uniforms.maskRegion = glm::vec4(maskOrigin.x, maskOrigin.y, maskSize, texelSize);
    uniforms.accumulationParams = glm::vec4(
        settings.snowAccumulationRate,
        settings.snowMeltRate,
        deltaTime,
        isSnowing ? 1.0f : 0.0f
    );
    uniforms.snowParams = glm::vec4(
        settings.snowAmount,
        weatherIntensity,
        static_cast<float>(currentInteractions.size()),
        0.0f
    );

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(SnowMaskUniforms));

    // Copy interaction sources to buffer
    if (!currentInteractions.empty()) {
        size_t copySize = sizeof(SnowInteractionSource) * std::min(currentInteractions.size(),
                                                                    static_cast<size_t>(MAX_INTERACTIONS));
        memcpy(interactionBuffers.mappedPointers[frameIndex], currentInteractions.data(), copySize);
    }
}

void SnowMaskSystem::addInteraction(const glm::vec3& position, float radius, float strength) {
    if (currentInteractions.size() >= MAX_INTERACTIONS) {
        return;
    }

    SnowInteractionSource source{};
    source.positionAndRadius = glm::vec4(position, radius);
    source.strengthAndShape = glm::vec4(strength, 0.0f, 0.0f, 0.0f);  // Circle shape

    currentInteractions.push_back(source);
}

void SnowMaskSystem::clearInteractions() {
    currentInteractions.clear();
}

void SnowMaskSystem::setMaskCenter(const glm::vec3& worldPos) {
    // Center the mask on the world position
    maskOrigin = glm::vec2(worldPos.x - maskSize * 0.5f, worldPos.z - maskSize * 0.5f);
}

void SnowMaskSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    // Transition snow mask image to general layout for compute write
    auto prepareBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(isFirstFrame ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(isFirstFrame ? vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(snowMaskImage)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    vkCmd.pipelineBarrier(
        isFirstFrame ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {}, prepareBarrier);

    // Bind compute pipeline and descriptor set
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, getComputePipelineHandles().pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             getComputePipelineHandles().pipelineLayout, 0,
                             vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    // Dispatch: 512x512 / 16x16 = 32x32 workgroups
    uint32_t workgroupCount = SNOW_MASK_SIZE / WORKGROUP_SIZE;
    vkCmd.dispatch(workgroupCount, workgroupCount, 1);

    // Transition snow mask to shader read optimal for fragment shaders
    BarrierHelpers::imageToShaderRead(vkCmd, snowMaskImage, vk::PipelineStageFlagBits::eFragmentShader);

    // Mark first frame as done
    isFirstFrame = false;

    // Clear interactions for next frame
    clearInteractions();
}
