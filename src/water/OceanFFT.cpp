#include "OceanFFT.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "shaders/bindings.h"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <array>
#include <vulkan/vulkan.hpp>

std::unique_ptr<OceanFFT> OceanFFT::create(const InitInfo& info) {
    std::unique_ptr<OceanFFT> ocean(new OceanFFT());
    if (!ocean->initInternal(info)) {
        return nullptr;
    }
    return ocean;
}

std::unique_ptr<OceanFFT> OceanFFT::create(const InitContext& ctx, const OceanParams& params, bool useCascades) {
    std::unique_ptr<OceanFFT> ocean(new OceanFFT());
    if (!ocean->initInternal(ctx, params, useCascades)) {
        return nullptr;
    }
    return ocean;
}

OceanFFT::~OceanFFT() {
    cleanup();
}

bool OceanFFT::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    params = info.params;

    // Set up cascades based on configuration
    if (info.useCascades) {
        cascadeCount = 3;
        cascades.resize(cascadeCount);

        // Cascade 0: Large swells (long wavelength)
        cascades[0].config = {
            params.oceanSize,           // 256m patch
            params.heightScale,         // Full height
            params.choppiness * 0.8f    // Slightly less choppy
        };

        // Cascade 1: Medium waves
        cascades[1].config = {
            params.oceanSize / 4.0f,    // 64m patch
            params.heightScale * 0.4f,  // Smaller waves
            params.choppiness
        };

        // Cascade 2: Small ripples (high frequency detail)
        cascades[2].config = {
            params.oceanSize / 16.0f,   // 16m patch
            params.heightScale * 0.15f, // Tiny ripples
            params.choppiness * 1.5f    // More choppy for detail
        };
    } else {
        cascadeCount = 1;
        cascades.resize(1);
        cascades[0].config = {
            params.oceanSize,
            params.heightScale,
            params.choppiness
        };
    }

    // Create sampler for output textures
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)  // Tiling ocean
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_TRUE)
        .setMaxAnisotropy(16.0f)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    if (!ManagedSampler::create(device, *reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo), sampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create sampler");
        return false;
    }

    // Create compute pipelines
    if (!createComputePipelines()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create compute pipelines");
        return false;
    }

    // Create cascades
    for (int i = 0; i < cascadeCount; i++) {
        if (!createCascade(cascades[i], cascades[i].config)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create cascade %d", i);
            return false;
        }
    }

    // Create descriptor sets
    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("OceanFFT: Initialized with %d cascades, resolution %d", cascadeCount, params.resolution);
    return true;
}

bool OceanFFT::initInternal(const InitContext& ctx, const OceanParams& oceanParams, bool useCascades) {
    device = ctx.device;
    physicalDevice = ctx.physicalDevice;
    allocator = ctx.allocator;
    commandPool = ctx.commandPool;
    computeQueue = ctx.graphicsQueue;  // Use graphics queue for compute
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    params = oceanParams;

    // Set up cascades based on configuration
    if (useCascades) {
        cascadeCount = 3;
        cascades.resize(cascadeCount);

        // Cascade 0: Large swells (long wavelength)
        cascades[0].config = {
            params.oceanSize,           // 256m patch
            params.heightScale,         // Full height
            params.choppiness * 0.8f    // Slightly less choppy
        };

        // Cascade 1: Medium waves
        cascades[1].config = {
            params.oceanSize / 4.0f,    // 64m patch
            params.heightScale * 0.4f,  // Smaller waves
            params.choppiness
        };

        // Cascade 2: Small ripples (high frequency detail)
        cascades[2].config = {
            params.oceanSize / 16.0f,   // 16m patch
            params.heightScale * 0.15f, // Tiny ripples
            params.choppiness * 1.5f    // More choppy for detail
        };
    } else {
        cascadeCount = 1;
        cascades.resize(1);
        cascades[0].config = {
            params.oceanSize,
            params.heightScale,
            params.choppiness
        };
    }

    // Create sampler for output textures
    auto samplerInfo2 = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)  // Tiling ocean
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_TRUE)
        .setMaxAnisotropy(16.0f)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    if (!ManagedSampler::create(device, *reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo2), sampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create sampler");
        return false;
    }

    // Create compute pipelines
    if (!createComputePipelines()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create compute pipelines");
        return false;
    }

    // Create cascades
    for (int i = 0; i < cascadeCount; i++) {
        if (!createCascade(cascades[i], cascades[i].config)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create cascade %d", i);
            return false;
        }
    }

    // Create descriptor sets
    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("OceanFFT: Initialized with %d cascades, resolution %d", cascadeCount, params.resolution);
    return true;
}

void OceanFFT::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Clear cascades (RAII handles cleanup)
    cascades.clear();

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Clear UBOs (RAII handles cleanup)
    spectrumUBOs.clear();
    spectrumUBOMapped.clear();

    // Clear pipelines and layouts (RAII handles cleanup)
    spectrumPipeline = ManagedPipeline();
    spectrumPipelineLayout = ManagedPipelineLayout();
    spectrumDescLayout = ManagedDescriptorSetLayout();

    timeEvolutionPipeline = ManagedPipeline();
    timeEvolutionPipelineLayout = ManagedPipelineLayout();
    timeEvolutionDescLayout = ManagedDescriptorSetLayout();

    fftPipeline = ManagedPipeline();
    fftPipelineLayout = ManagedPipelineLayout();
    fftDescLayout = ManagedDescriptorSetLayout();

    displacementPipeline = ManagedPipeline();
    displacementPipelineLayout = ManagedPipelineLayout();
    displacementDescLayout = ManagedDescriptorSetLayout();

    // Clear sampler (RAII handles cleanup)
    sampler = ManagedSampler();

    device = VK_NULL_HANDLE;
}

bool OceanFFT::createImage(ManagedImage& image, ManagedImageView& view,
                           VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage) {
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setExtent(vk::Extent3D{width, height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(static_cast<vk::ImageUsageFlags>(usage))
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedImage::create(allocator, *reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), allocInfo, image)) {
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(image.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (!ManagedImageView::create(device, *reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), view)) {
        return false;
    }

    return true;
}

bool OceanFFT::createCascade(Cascade& cascade, const CascadeConfig& config) {
    uint32_t res = static_cast<uint32_t>(params.resolution);

    // Spectrum textures (RGBA32F for complex H0 + conjugate)
    if (!createImage(cascade.h0Spectrum, cascade.h0SpectrumView,
                     VK_FORMAT_R32G32B32A32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Angular frequency (R32F)
    if (!createImage(cascade.omegaSpectrum, cascade.omegaSpectrumView,
                     VK_FORMAT_R32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Time-evolved spectra (RG32F for complex values)
    if (!createImage(cascade.hktDy, cascade.hktDyView,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }
    if (!createImage(cascade.hktDx, cascade.hktDxView,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }
    if (!createImage(cascade.hktDz, cascade.hktDzView,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // FFT ping-pong buffers (RG32F)
    if (!createImage(cascade.fftPing, cascade.fftPingView,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (!createImage(cascade.fftPong, cascade.fftPongView,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }

    // Output textures
    // Displacement: RGBA16F (xyz = displacement, w = jacobian)
    if (!createImage(cascade.displacementMap, cascade.displacementMapView,
                     VK_FORMAT_R16G16B16A16_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Normal: RGBA16F (xyz = normal)
    if (!createImage(cascade.normalMap, cascade.normalMapView,
                     VK_FORMAT_R16G16B16A16_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Foam: R16F
    if (!createImage(cascade.foamMap, cascade.foamMapView,
                     VK_FORMAT_R16_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    return true;
}

bool OceanFFT::createComputePipelines() {
    // =========================================================================
    // Spectrum Generation Pipeline
    // =========================================================================
    {
        VkDescriptorSetLayout rawDescLayout = DescriptorManager::LayoutBuilder(device)
            .addBinding(Bindings::OCEAN_SPECTRUM_H0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_SPECTRUM_OMEGA, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_SPECTRUM_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
        if (rawDescLayout == VK_NULL_HANDLE) {
            return false;
        }
        spectrumDescLayout = ManagedDescriptorSetLayout::fromRaw(device, rawDescLayout);

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, spectrumDescLayout.get());
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        spectrumPipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

        auto shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_spectrum.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_spectrum.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(spectrumPipelineLayout.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), spectrumPipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

        if (!success) {
            return false;
        }
    }

    // =========================================================================
    // Time Evolution Pipeline
    // =========================================================================
    {
        VkDescriptorSetLayout rawDescLayout = DescriptorManager::LayoutBuilder(device)
            .addBinding(Bindings::OCEAN_HKT_DY, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_HKT_DX, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_HKT_DZ, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_H0_INPUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_OMEGA_INPUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
        if (rawDescLayout == VK_NULL_HANDLE) {
            return false;
        }
        timeEvolutionDescLayout = ManagedDescriptorSetLayout::fromRaw(device, rawDescLayout);

        // Push constants for time and parameters
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanTimeEvolutionPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, timeEvolutionDescLayout.get(),
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        timeEvolutionPipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

        auto shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_time_evolution.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_time_evolution.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(timeEvolutionPipelineLayout.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), timeEvolutionPipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

        if (!success) {
            return false;
        }
    }

    // =========================================================================
    // FFT Pipeline
    // =========================================================================
    {
        VkDescriptorSetLayout rawDescLayout = DescriptorManager::LayoutBuilder(device)
            .addBinding(Bindings::OCEAN_FFT_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_FFT_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
        if (rawDescLayout == VK_NULL_HANDLE) {
            return false;
        }
        fftDescLayout = ManagedDescriptorSetLayout::fromRaw(device, rawDescLayout);

        // Push constants: stage, direction, resolution, inverse
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanFFTPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, fftDescLayout.get(),
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        fftPipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

        auto shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_fft.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_fft.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(fftPipelineLayout.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), fftPipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

        if (!success) {
            return false;
        }
    }

    // =========================================================================
    // Displacement Generation Pipeline
    // =========================================================================
    {
        VkDescriptorSetLayout rawDescLayout = DescriptorManager::LayoutBuilder(device)
            .addBinding(Bindings::OCEAN_DISP_DY, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_DISP_DX, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_DISP_DZ, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_DISP_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_NORMAL_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(Bindings::OCEAN_FOAM_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
        if (rawDescLayout == VK_NULL_HANDLE) {
            return false;
        }
        displacementDescLayout = ManagedDescriptorSetLayout::fromRaw(device, rawDescLayout);

        // Push constants
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanDisplacementPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, displacementDescLayout.get(),
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        displacementPipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

        auto shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_displacement.comp.spv");
        if (!shaderModule) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_displacement.comp.spv");
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(displacementPipelineLayout.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), displacementPipeline);
        vkDestroyShaderModule(device, *shaderModule, nullptr);

        if (!success) {
            return false;
        }
    }

    return true;
}

bool OceanFFT::createDescriptorSets() {
    // Calculate total descriptor sets needed
    // Per cascade: 1 spectrum + 1 time evolution + 2 FFT ping-pong * 3 components * 2 directions + 1 displacement
    // Simplified: 1 spectrum + 1 time evolution + multiple FFT sets + 1 displacement
    uint32_t setsPerCascade = 4;  // spectrum, time evolution, fft (reuse), displacement
    uint32_t totalSets = cascadeCount * setsPerCascade + cascadeCount * 12;  // Extra FFT sets for ping-pong

    // Create descriptor pool
    std::array<vk::DescriptorPoolSize, 3> poolSizes = {
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(totalSets * 6),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(totalSets * 2),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(cascadeCount)
    };

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(totalSets)
        .setPoolSizes(poolSizes);

    if (vkCreateDescriptorPool(device, reinterpret_cast<const VkDescriptorPoolCreateInfo*>(&poolInfo),
            nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    // Create UBOs for spectrum parameters
    spectrumUBOs.resize(cascadeCount);
    spectrumUBOMapped.resize(cascadeCount);

    for (int i = 0; i < cascadeCount; i++) {
        if (!VulkanResourceFactory::createUniformBuffer(allocator, sizeof(SpectrumUBO), spectrumUBOs[i])) {
            return false;
        }
        spectrumUBOMapped[i] = spectrumUBOs[i].map();
        if (!spectrumUBOMapped[i]) {
            return false;
        }
    }

    // Allocate descriptor sets for each cascade
    spectrumDescSets.resize(cascadeCount);
    timeEvolutionDescSets.resize(cascadeCount);
    displacementDescSets.resize(cascadeCount);

    for (int i = 0; i < cascadeCount; i++) {
        // Spectrum descriptor set
        {
            VkDescriptorSetLayout layout = spectrumDescLayout.get();
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;

            if (vkAllocateDescriptorSets(device, &allocInfo, &spectrumDescSets[i]) != VK_SUCCESS) {
                return false;
            }

            DescriptorManager::SetWriter(device, spectrumDescSets[i])
                .writeStorageImage(Bindings::OCEAN_SPECTRUM_H0, cascades[i].h0SpectrumView.get())
                .writeStorageImage(Bindings::OCEAN_SPECTRUM_OMEGA, cascades[i].omegaSpectrumView.get())
                .writeBuffer(Bindings::OCEAN_SPECTRUM_PARAMS, spectrumUBOs[i].get(), 0, sizeof(SpectrumUBO))
                .update();
        }

        // Time evolution descriptor set
        {
            VkDescriptorSetLayout layout = timeEvolutionDescLayout.get();
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;

            if (vkAllocateDescriptorSets(device, &allocInfo, &timeEvolutionDescSets[i]) != VK_SUCCESS) {
                return false;
            }

            DescriptorManager::SetWriter(device, timeEvolutionDescSets[i])
                .writeStorageImage(Bindings::OCEAN_HKT_DY, cascades[i].hktDyView.get())
                .writeStorageImage(Bindings::OCEAN_HKT_DX, cascades[i].hktDxView.get())
                .writeStorageImage(Bindings::OCEAN_HKT_DZ, cascades[i].hktDzView.get())
                .writeImage(Bindings::OCEAN_H0_INPUT, cascades[i].h0SpectrumView.get(), sampler.get())
                .writeImage(Bindings::OCEAN_OMEGA_INPUT, cascades[i].omegaSpectrumView.get(), sampler.get())
                .update();
        }

        // Displacement descriptor set
        {
            VkDescriptorSetLayout layout = displacementDescLayout.get();
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;

            if (vkAllocateDescriptorSets(device, &allocInfo, &displacementDescSets[i]) != VK_SUCCESS) {
                return false;
            }

            // Note: These will be updated dynamically based on which FFT buffer has the result
        }
    }

    return true;
}

void OceanFFT::update(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    if (!enabled) return;

    // Regenerate spectrum if parameters changed
    if (spectrumDirty) {
        regenerateSpectrum(cmd);
        spectrumDirty = false;
    }

    // Process each cascade
    for (int i = 0; i < cascadeCount; i++) {
        Cascade& cascade = cascades[i];

        // Time evolution
        recordTimeEvolution(cmd, cascade, time);

        // Insert barrier between time evolution and FFT
        Barriers::computeToCompute(cmd);

        // FFT for each displacement component
        recordFFT(cmd, cascade, cascade.hktDy.get(), cascade.hktDyView.get(),
                  cascade.fftPing.get(), cascade.fftPingView.get());
        recordFFT(cmd, cascade, cascade.hktDx.get(), cascade.hktDxView.get(),
                  cascade.fftPong.get(), cascade.fftPongView.get());
        recordFFT(cmd, cascade, cascade.hktDz.get(), cascade.hktDzView.get(),
                  cascade.fftPing.get(), cascade.fftPingView.get());

        // Barrier before displacement generation
        Barriers::computeToCompute(cmd);

        // Generate final displacement/normal/foam maps
        recordDisplacementGeneration(cmd, cascade);
    }

    // Final barrier before water shader can sample
    Barriers::BarrierBatch(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .memoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .submit();
}

void OceanFFT::regenerateSpectrum(VkCommandBuffer cmd) {
    for (int i = 0; i < cascadeCount; i++) {
        // Update UBO with current parameters
        SpectrumUBO ubo{};
        ubo.resolution = params.resolution;
        ubo.oceanSize = cascades[i].config.oceanSize;
        ubo.windSpeed = params.windSpeed;
        ubo.windDirection = glm::normalize(params.windDirection);
        ubo.amplitude = params.amplitude;
        ubo.gravity = params.gravity;
        ubo.smallWaveCutoff = params.smallWaveCutoff;
        ubo.alignment = params.alignment;
        ubo.seed = static_cast<uint32_t>(i * 12345 + 67890);  // Different seed per cascade

        memcpy(spectrumUBOMapped[i], &ubo, sizeof(SpectrumUBO));

        recordSpectrumGeneration(cmd, cascades[i], ubo.seed);
    }

    // Barrier after spectrum generation
    Barriers::computeToCompute(cmd);
}

void OceanFFT::recordSpectrumGeneration(VkCommandBuffer cmd, Cascade& cascade, uint32_t seed) {
    // Transition images to general layout for compute writes
    Barriers::BarrierBatch(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .imageTransition(cascade.h0Spectrum.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .imageTransition(cascade.omegaSpectrum.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .submit();

    // Bind pipeline and descriptor set
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spectrumPipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spectrumPipelineLayout.get(),
                            0, 1, &spectrumDescSets[cascadeIndex], 0, nullptr);

    // Dispatch
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupCount, groupCount, 1);

    // Transition to shader read for time evolution
    Barriers::BarrierBatch(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .imageTransition(cascade.h0Spectrum.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .imageTransition(cascade.omegaSpectrum.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .submit();
}

void OceanFFT::recordTimeEvolution(VkCommandBuffer cmd, Cascade& cascade, float time) {
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    // Transition output images to general layout
    Barriers::BarrierBatch(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .imageTransition(cascade.hktDy.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .imageTransition(cascade.hktDx.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .imageTransition(cascade.hktDz.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .submit();

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, timeEvolutionPipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, timeEvolutionPipelineLayout.get(),
                            0, 1, &timeEvolutionDescSets[cascadeIndex], 0, nullptr);

    // Push constants
    OceanTimeEvolutionPushConstants pushConstants = {
        time,
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.choppiness
    };

    vkCmdPushConstants(cmd, timeEvolutionPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    // Dispatch
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupCount, groupCount, 1);
}

void OceanFFT::recordFFT(VkCommandBuffer cmd, Cascade& cascade,
                         VkImage input, VkImageView inputView,
                         VkImage output, VkImageView outputView) {
    // Simplified FFT using ping-pong between input and ping/pong buffers
    // For a proper implementation, we'd need to do horizontal then vertical FFT
    // Each direction requires log2(N) butterfly passes

    int numStages = static_cast<int>(std::log2(params.resolution));
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;

    // Allocate temporary FFT descriptor sets dynamically
    VkDescriptorSet fftDescSet;
    VkDescriptorSetLayout layout = fftDescLayout.get();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &fftDescSet) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate FFT descriptor set");
        return;
    }

    // Track which buffer has current data
    VkImage currentInput = input;
    VkImageView currentInputView = inputView;
    VkImage currentOutput = cascade.fftPing.get();
    VkImageView currentOutputView = cascade.fftPingView.get();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipeline.get());

    // Horizontal FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        DescriptorManager::SetWriter(device, fftDescSet)
            .writeStorageImage(Bindings::OCEAN_FFT_INPUT, currentInputView)
            .writeStorageImage(Bindings::OCEAN_FFT_OUTPUT, currentOutputView)
            .update();

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipelineLayout.get(),
                                0, 1, &fftDescSet, 0, nullptr);

        // Push constants: stage, direction (0=horizontal), resolution, inverse (1=IFFT)
        OceanFFTPushConstants pushData = {stage, 0, params.resolution, 1};
        vkCmdPushConstants(cmd, fftPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushData), &pushData);

        vkCmdDispatch(cmd, groupCount, groupCount, 1);

        // Barrier between stages
        Barriers::computeToCompute(cmd);

        // Swap buffers for ping-pong
        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        // Use ping/pong buffers for subsequent stages
        if (stage == 0) {
            currentOutput = cascade.fftPong.get();
            currentOutputView = cascade.fftPongView.get();
        } else {
            if (currentOutput == cascade.fftPing.get()) {
                currentOutput = cascade.fftPong.get();
                currentOutputView = cascade.fftPongView.get();
            } else {
                currentOutput = cascade.fftPing.get();
                currentOutputView = cascade.fftPingView.get();
            }
        }
    }

    // Vertical FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        DescriptorManager::SetWriter(device, fftDescSet)
            .writeStorageImage(Bindings::OCEAN_FFT_INPUT, currentInputView)
            .writeStorageImage(Bindings::OCEAN_FFT_OUTPUT, currentOutputView)
            .update();

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipelineLayout.get(),
                                0, 1, &fftDescSet, 0, nullptr);

        // Push constants: stage, direction (1=vertical), resolution, inverse
        OceanFFTPushConstants pushData = {stage, 1, params.resolution, 1};
        vkCmdPushConstants(cmd, fftPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushData), &pushData);

        vkCmdDispatch(cmd, groupCount, groupCount, 1);

        Barriers::computeToCompute(cmd);

        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        if (currentOutput == cascade.fftPing.get()) {
            currentOutput = cascade.fftPong.get();
            currentOutputView = cascade.fftPongView.get();
        } else {
            currentOutput = cascade.fftPing.get();
            currentOutputView = cascade.fftPingView.get();
        }
    }
}

void OceanFFT::recordDisplacementGeneration(VkCommandBuffer cmd, Cascade& cascade) {
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    // Transition output images to general layout for compute writes
    Barriers::BarrierBatch(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .imageTransition(cascade.displacementMap.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .imageTransition(cascade.normalMap.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .imageTransition(cascade.foamMap.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         0, VK_ACCESS_SHADER_WRITE_BIT)
        .submit();

    // Update displacement descriptor set with current FFT results
    DescriptorManager::SetWriter(device, displacementDescSets[cascadeIndex])
        .writeStorageImage(Bindings::OCEAN_DISP_DY, cascade.fftPingView.get())
        .writeStorageImage(Bindings::OCEAN_DISP_DX, cascade.fftPongView.get())
        .writeStorageImage(Bindings::OCEAN_DISP_DZ, cascade.fftPingView.get())
        .writeStorageImage(Bindings::OCEAN_DISP_OUTPUT, cascade.displacementMapView.get())
        .writeStorageImage(Bindings::OCEAN_NORMAL_OUTPUT, cascade.normalMapView.get())
        .writeStorageImage(Bindings::OCEAN_FOAM_OUTPUT, cascade.foamMapView.get())
        .update();

    // Bind and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipelineLayout.get(),
                            0, 1, &displacementDescSets[cascadeIndex], 0, nullptr);

    // Push constants
    OceanDisplacementPushConstants pushConstants = {
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.heightScale,
        params.foamThreshold,
        0.9f,  // foam decay
        params.normalStrength
    };

    vkCmdPushConstants(cmd, displacementPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupCount, groupCount, 1);

    // Transition outputs to shader read
    Barriers::BarrierBatch(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .imageTransition(cascade.displacementMap.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .imageTransition(cascade.normalMap.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .imageTransition(cascade.foamMap.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
        .submit();
}

VkImageView OceanFFT::getDisplacementView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].displacementMapView.get();
}

VkImageView OceanFFT::getNormalView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].normalMapView.get();
}

VkImageView OceanFFT::getFoamView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].foamMapView.get();
}

void OceanFFT::setParams(const OceanParams& newParams) {
    bool needsRegen = (newParams.resolution != params.resolution ||
                       newParams.oceanSize != params.oceanSize ||
                       newParams.windSpeed != params.windSpeed ||
                       newParams.windDirection != params.windDirection ||
                       newParams.amplitude != params.amplitude ||
                       newParams.smallWaveCutoff != params.smallWaveCutoff ||
                       newParams.alignment != params.alignment);

    params = newParams;

    if (needsRegen) {
        spectrumDirty = true;
    }
}

void OceanFFT::setWindSpeed(float speed) {
    if (speed != params.windSpeed) {
        params.windSpeed = speed;
        spectrumDirty = true;
    }
}

void OceanFFT::setWindDirection(const glm::vec2& dir) {
    glm::vec2 normalized = glm::normalize(dir);
    if (normalized != params.windDirection) {
        params.windDirection = normalized;
        spectrumDirty = true;
    }
}

void OceanFFT::setAmplitude(float amp) {
    if (amp != params.amplitude) {
        params.amplitude = amp;
        spectrumDirty = true;
    }
}

void OceanFFT::setChoppiness(float chop) {
    params.choppiness = chop;
    // Update cascade configs
    if (cascadeCount >= 1) cascades[0].config.choppiness = chop * 0.8f;
    if (cascadeCount >= 2) cascades[1].config.choppiness = chop;
    if (cascadeCount >= 3) cascades[2].config.choppiness = chop * 1.5f;
}

void OceanFFT::setHeightScale(float scale) {
    params.heightScale = scale;
    // Update cascade configs
    if (cascadeCount >= 1) cascades[0].config.heightScale = scale;
    if (cascadeCount >= 2) cascades[1].config.heightScale = scale * 0.4f;
    if (cascadeCount >= 3) cascades[2].config.heightScale = scale * 0.15f;
}

void OceanFFT::setFoamThreshold(float threshold) {
    params.foamThreshold = threshold;
}
