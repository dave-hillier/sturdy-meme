#include "OceanFFT.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "shaders/bindings.h"
#include <SDL_log.h>
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
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: raiiDevice is required");
        return false;
    }

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

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create sampler: %s", e.what());
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
    raiiDevice_ = ctx.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: raiiDevice is required");
        return false;
    }

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

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo2);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create sampler: %s", e.what());
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

    vk::Device vkDevice(device);
    vkDevice.waitIdle();

    // Clear cascades (RAII handles cleanup)
    cascades.clear();

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDevice.destroyDescriptorPool(descriptorPool);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Clear UBOs (RAII handles cleanup)
    spectrumUBOs.clear();
    spectrumUBOMapped.clear();

    // Clear pipelines and layouts (RAII handles cleanup)
    spectrumPipeline_.reset();
    spectrumPipelineLayout_.reset();
    spectrumDescLayout_.reset();

    timeEvolutionPipeline_.reset();
    timeEvolutionPipelineLayout_.reset();
    timeEvolutionDescLayout_.reset();

    fftPipeline_.reset();
    fftPipelineLayout_.reset();
    fftDescLayout_.reset();

    displacementPipeline_.reset();
    displacementPipelineLayout_.reset();
    displacementDescLayout_.reset();

    // Clear sampler (RAII handles cleanup)
    sampler_.reset();

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;
}

bool OceanFFT::createImage(ManagedImage& image, std::optional<vk::raii::ImageView>& view,
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

    try {
        view.emplace(*raiiDevice_, viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create image view: %s", e.what());
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
        spectrumDescLayout_.emplace(*raiiDevice_, rawDescLayout);

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, **spectrumDescLayout_);
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        spectrumPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

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
            .setLayout(**spectrumPipelineLayout_);

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
                nullptr, &rawPipeline) != VK_SUCCESS) {
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        spectrumPipeline_.emplace(*raiiDevice_, rawPipeline);
        vk::Device(device).destroyShaderModule(*shaderModule);
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
        timeEvolutionDescLayout_.emplace(*raiiDevice_, rawDescLayout);

        // Push constants for time and parameters
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanTimeEvolutionPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, **timeEvolutionDescLayout_,
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        timeEvolutionPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

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
            .setLayout(**timeEvolutionPipelineLayout_);

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
                nullptr, &rawPipeline) != VK_SUCCESS) {
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        timeEvolutionPipeline_.emplace(*raiiDevice_, rawPipeline);
        vk::Device(device).destroyShaderModule(*shaderModule);
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
        fftDescLayout_.emplace(*raiiDevice_, rawDescLayout);

        // Push constants: stage, direction, resolution, inverse
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanFFTPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, **fftDescLayout_,
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        fftPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

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
            .setLayout(**fftPipelineLayout_);

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
                nullptr, &rawPipeline) != VK_SUCCESS) {
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        fftPipeline_.emplace(*raiiDevice_, rawPipeline);
        vk::Device(device).destroyShaderModule(*shaderModule);
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
        displacementDescLayout_.emplace(*raiiDevice_, rawDescLayout);

        // Push constants
        auto pushRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(OceanDisplacementPushConstants));

        VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, **displacementDescLayout_,
            {*reinterpret_cast<const VkPushConstantRange*>(&pushRange)});
        if (rawPipelineLayout == VK_NULL_HANDLE) {
            return false;
        }
        displacementPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

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
            .setLayout(**displacementPipelineLayout_);

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
                nullptr, &rawPipeline) != VK_SUCCESS) {
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        displacementPipeline_.emplace(*raiiDevice_, rawPipeline);
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    return true;
}

bool OceanFFT::createDescriptorSets() {
    // Calculate total descriptor sets needed
    // Per cascade: 1 spectrum + 1 time evolution + 2 FFT ping-pong * 3 components * 2 directions + 1 displacement
    // Simplified: 1 spectrum + 1 time evolution + multiple FFT sets + 1 displacement
    uint32_t setsPerCascade = 4;  // spectrum, time evolution, fft (reuse), displacement
    uint32_t totalSets = cascadeCount * setsPerCascade + cascadeCount * 12;  // Extra FFT sets for ping-pong

    vk::Device vkDevice(device);

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

    try {
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create descriptor pool: %s", e.what());
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
            vk::DescriptorSetLayout spectrumLayout(**spectrumDescLayout_);
            auto allocInfo = vk::DescriptorSetAllocateInfo{}
                .setDescriptorPool(descriptorPool)
                .setSetLayouts(spectrumLayout);

            try {
                auto sets = vkDevice.allocateDescriptorSets(allocInfo);
                spectrumDescSets[i] = sets[0];
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate spectrum descriptor set: %s", e.what());
                return false;
            }

            DescriptorManager::SetWriter(device, spectrumDescSets[i])
                .writeStorageImage(Bindings::OCEAN_SPECTRUM_H0, **cascades[i].h0SpectrumView)
                .writeStorageImage(Bindings::OCEAN_SPECTRUM_OMEGA, **cascades[i].omegaSpectrumView)
                .writeBuffer(Bindings::OCEAN_SPECTRUM_PARAMS, spectrumUBOs[i].get(), 0, sizeof(SpectrumUBO))
                .update();
        }

        // Time evolution descriptor set
        {
            vk::DescriptorSetLayout timeLayout(**timeEvolutionDescLayout_);
            auto allocInfo = vk::DescriptorSetAllocateInfo{}
                .setDescriptorPool(descriptorPool)
                .setSetLayouts(timeLayout);

            try {
                auto sets = vkDevice.allocateDescriptorSets(allocInfo);
                timeEvolutionDescSets[i] = sets[0];
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate time evolution descriptor set: %s", e.what());
                return false;
            }

            DescriptorManager::SetWriter(device, timeEvolutionDescSets[i])
                .writeStorageImage(Bindings::OCEAN_HKT_DY, **cascades[i].hktDyView)
                .writeStorageImage(Bindings::OCEAN_HKT_DX, **cascades[i].hktDxView)
                .writeStorageImage(Bindings::OCEAN_HKT_DZ, **cascades[i].hktDzView)
                .writeImage(Bindings::OCEAN_H0_INPUT, **cascades[i].h0SpectrumView, **sampler_)
                .writeImage(Bindings::OCEAN_OMEGA_INPUT, **cascades[i].omegaSpectrumView, **sampler_)
                .update();
        }

        // Displacement descriptor set
        {
            vk::DescriptorSetLayout dispLayout(**displacementDescLayout_);
            auto allocInfo = vk::DescriptorSetAllocateInfo{}
                .setDescriptorPool(descriptorPool)
                .setSetLayouts(dispLayout);

            try {
                auto sets = vkDevice.allocateDescriptorSets(allocInfo);
                displacementDescSets[i] = sets[0];
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate displacement descriptor set: %s", e.what());
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
        vk::CommandBuffer vkCmd(cmd);
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                              {}, barrier, {}, {});

        // FFT for each displacement component
        recordFFT(cmd, cascade, cascade.hktDy.get(), **cascade.hktDyView,
                  cascade.fftPing.get(), **cascade.fftPingView);
        recordFFT(cmd, cascade, cascade.hktDx.get(), **cascade.hktDxView,
                  cascade.fftPong.get(), **cascade.fftPongView);
        recordFFT(cmd, cascade, cascade.hktDz.get(), **cascade.hktDzView,
                  cascade.fftPing.get(), **cascade.fftPingView);

        // Barrier before displacement generation
        {
            vk::CommandBuffer vkCmd2(cmd);
            auto barrier2 = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
            vkCmd2.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, barrier2, {}, {});
        }

        // Generate final displacement/normal/foam maps
        recordDisplacementGeneration(cmd, cascade);
    }

    // Final barrier before water shader can sample
    {
        vk::CommandBuffer vkCmd3(cmd);
        auto barrier3 = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        vkCmd3.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader,
                              {}, barrier3, {}, {});
    }
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
    vk::CommandBuffer vkCmd(cmd);
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, barrier, {}, {});
}

void OceanFFT::recordSpectrumGeneration(VkCommandBuffer cmd, Cascade& cascade, uint32_t seed) {
    vk::CommandBuffer vkCmd(cmd);

    // Transition images to general layout for compute writes
    std::array<vk::ImageMemoryBarrier, 2> barriers = {
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.h0Spectrum.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.omegaSpectrum.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
    };
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, barriers);

    // Bind pipeline and descriptor set
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **spectrumPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **spectrumPipelineLayout_,
                             0, spectrumDescSets[cascadeIndex], {});

    // Dispatch
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmd.dispatch(groupCount, groupCount, 1);

    // Transition to shader read for time evolution
    std::array<vk::ImageMemoryBarrier, 2> readBarriers = {
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.h0Spectrum.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.omegaSpectrum.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
    };
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, readBarriers);
}

void OceanFFT::recordTimeEvolution(VkCommandBuffer cmd, Cascade& cascade, float time) {
    vk::CommandBuffer vkCmd(cmd);
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    // Transition output images to general layout
    std::array<vk::ImageMemoryBarrier, 3> barriers = {
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.hktDy.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.hktDx.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.hktDz.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
    };
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, barriers);

    // Bind pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **timeEvolutionPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **timeEvolutionPipelineLayout_,
                             0, timeEvolutionDescSets[cascadeIndex], {});

    // Push constants
    OceanTimeEvolutionPushConstants pushConstants = {
        time,
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.choppiness
    };

    vkCmd.pushConstants<OceanTimeEvolutionPushConstants>(
        **timeEvolutionPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    // Dispatch
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmd.dispatch(groupCount, groupCount, 1);
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
    vk::DescriptorSetLayout fftLayout(**fftDescLayout_);
    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(descriptorPool)
        .setSetLayouts(fftLayout);

    try {
        auto sets = vk::Device(device).allocateDescriptorSets(allocInfo);
        fftDescSet = sets[0];
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate FFT descriptor set: %s", e.what());
        return;
    }

    // Track which buffer has current data
    VkImage currentInput = input;
    VkImageView currentInputView = inputView;
    VkImage currentOutput = cascade.fftPing.get();
    VkImageView currentOutputView = **cascade.fftPingView;

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **fftPipeline_);

    // Horizontal FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        DescriptorManager::SetWriter(device, fftDescSet)
            .writeStorageImage(Bindings::OCEAN_FFT_INPUT, currentInputView)
            .writeStorageImage(Bindings::OCEAN_FFT_OUTPUT, currentOutputView)
            .update();

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **fftPipelineLayout_,
                                 0, fftDescSet, {});

        // Push constants: stage, direction (0=horizontal), resolution, inverse (1=IFFT)
        OceanFFTPushConstants pushData = {stage, 0, params.resolution, 1};
        vkCmd.pushConstants<OceanFFTPushConstants>(
            **fftPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushData);

        vkCmd.dispatch(groupCount, groupCount, 1);

        // Barrier between stages
        {
            auto barrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, barrier, {}, {});
        }

        // Swap buffers for ping-pong
        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        // Use ping/pong buffers for subsequent stages
        if (stage == 0) {
            currentOutput = cascade.fftPong.get();
            currentOutputView = **cascade.fftPongView;
        } else {
            if (currentOutput == cascade.fftPing.get()) {
                currentOutput = cascade.fftPong.get();
                currentOutputView = **cascade.fftPongView;
            } else {
                currentOutput = cascade.fftPing.get();
                currentOutputView = **cascade.fftPingView;
            }
        }
    }

    // Vertical FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        DescriptorManager::SetWriter(device, fftDescSet)
            .writeStorageImage(Bindings::OCEAN_FFT_INPUT, currentInputView)
            .writeStorageImage(Bindings::OCEAN_FFT_OUTPUT, currentOutputView)
            .update();

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **fftPipelineLayout_,
                                 0, fftDescSet, {});

        // Push constants: stage, direction (1=vertical), resolution, inverse
        OceanFFTPushConstants pushData = {stage, 1, params.resolution, 1};
        vkCmd.pushConstants<OceanFFTPushConstants>(
            **fftPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushData);

        vkCmd.dispatch(groupCount, groupCount, 1);

        {
            auto barrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, barrier, {}, {});
        }

        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        if (currentOutput == cascade.fftPing.get()) {
            currentOutput = cascade.fftPong.get();
            currentOutputView = **cascade.fftPongView;
        } else {
            currentOutput = cascade.fftPing.get();
            currentOutputView = **cascade.fftPingView;
        }
    }
}

void OceanFFT::recordDisplacementGeneration(VkCommandBuffer cmd, Cascade& cascade) {
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    vk::CommandBuffer vkCmd(cmd);

    // Transition output images to general layout for compute writes
    std::array<vk::ImageMemoryBarrier, 3> barriers = {
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.displacementMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.normalMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.foamMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
    };
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, barriers);

    // Update displacement descriptor set with current FFT results
    DescriptorManager::SetWriter(device, displacementDescSets[cascadeIndex])
        .writeStorageImage(Bindings::OCEAN_DISP_DY, **cascade.fftPingView)
        .writeStorageImage(Bindings::OCEAN_DISP_DX, **cascade.fftPongView)
        .writeStorageImage(Bindings::OCEAN_DISP_DZ, **cascade.fftPingView)
        .writeStorageImage(Bindings::OCEAN_DISP_OUTPUT, **cascade.displacementMapView)
        .writeStorageImage(Bindings::OCEAN_NORMAL_OUTPUT, **cascade.normalMapView)
        .writeStorageImage(Bindings::OCEAN_FOAM_OUTPUT, **cascade.foamMapView)
        .update();

    // Bind and dispatch
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **displacementPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **displacementPipelineLayout_,
                             0, displacementDescSets[cascadeIndex], {});

    // Push constants
    OceanDisplacementPushConstants pushConstants = {
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.heightScale,
        params.foamThreshold,
        0.9f,  // foam decay
        params.normalStrength
    };

    vkCmd.pushConstants<OceanDisplacementPushConstants>(
        **displacementPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmd.dispatch(groupCount, groupCount, 1);

    // Transition outputs to shader read
    std::array<vk::ImageMemoryBarrier, 3> readBarriers = {
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.displacementMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.normalMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1)),
        vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascade.foamMap.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
    };
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, readBarriers);
}

VkImageView OceanFFT::getDisplacementView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].displacementMapView ? **cascades[cascade].displacementMapView : VK_NULL_HANDLE;
}

VkImageView OceanFFT::getNormalView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].normalMapView ? **cascades[cascade].normalMapView : VK_NULL_HANDLE;
}

VkImageView OceanFFT::getFoamView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].foamMapView ? **cascades[cascade].foamMapView : VK_NULL_HANDLE;
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
