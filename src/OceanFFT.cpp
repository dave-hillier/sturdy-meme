#include "OceanFFT.h"
#include "ShaderLoader.h"
#include "shaders/bindings.h"
#include <SDL_log.h>
#include <cmath>
#include <array>

bool OceanFFT::init(const InitInfo& info) {
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
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // Tiling ocean
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
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

bool OceanFFT::init(const InitContext& ctx, const OceanParams& oceanParams, bool useCascades) {
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
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // Tiling ocean
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
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

void OceanFFT::destroy() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy cascades
    for (auto& cascade : cascades) {
        destroyCascade(cascade);
    }
    cascades.clear();

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Destroy UBOs
    for (size_t i = 0; i < spectrumUBOs.size(); i++) {
        if (spectrumUBOs[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, spectrumUBOs[i], spectrumUBOAllocations[i]);
        }
    }
    spectrumUBOs.clear();
    spectrumUBOAllocations.clear();
    spectrumUBOMapped.clear();

    // Destroy pipelines
    if (spectrumPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, spectrumPipeline, nullptr);
        spectrumPipeline = VK_NULL_HANDLE;
    }
    if (spectrumPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, spectrumPipelineLayout, nullptr);
        spectrumPipelineLayout = VK_NULL_HANDLE;
    }
    if (spectrumDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, spectrumDescLayout, nullptr);
        spectrumDescLayout = VK_NULL_HANDLE;
    }

    if (timeEvolutionPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, timeEvolutionPipeline, nullptr);
        timeEvolutionPipeline = VK_NULL_HANDLE;
    }
    if (timeEvolutionPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, timeEvolutionPipelineLayout, nullptr);
        timeEvolutionPipelineLayout = VK_NULL_HANDLE;
    }
    if (timeEvolutionDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, timeEvolutionDescLayout, nullptr);
        timeEvolutionDescLayout = VK_NULL_HANDLE;
    }

    if (fftPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, fftPipeline, nullptr);
        fftPipeline = VK_NULL_HANDLE;
    }
    if (fftPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, fftPipelineLayout, nullptr);
        fftPipelineLayout = VK_NULL_HANDLE;
    }
    if (fftDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, fftDescLayout, nullptr);
        fftDescLayout = VK_NULL_HANDLE;
    }

    if (displacementPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, displacementPipeline, nullptr);
        displacementPipeline = VK_NULL_HANDLE;
    }
    if (displacementPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, displacementPipelineLayout, nullptr);
        displacementPipelineLayout = VK_NULL_HANDLE;
    }
    if (displacementDescLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, displacementDescLayout, nullptr);
        displacementDescLayout = VK_NULL_HANDLE;
    }

    // Destroy sampler
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
}

void OceanFFT::destroyCascade(Cascade& cascade) {
    auto destroyImage = [this](VkImage& img, VkImageView& view, VmaAllocation& alloc) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (img != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, img, alloc);
            img = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
        }
    };

    destroyImage(cascade.h0Spectrum, cascade.h0SpectrumView, cascade.h0Allocation);
    destroyImage(cascade.omegaSpectrum, cascade.omegaSpectrumView, cascade.omegaAllocation);
    destroyImage(cascade.hktDy, cascade.hktDyView, cascade.hktDyAllocation);
    destroyImage(cascade.hktDx, cascade.hktDxView, cascade.hktDxAllocation);
    destroyImage(cascade.hktDz, cascade.hktDzView, cascade.hktDzAllocation);
    destroyImage(cascade.fftPing, cascade.fftPingView, cascade.fftPingAllocation);
    destroyImage(cascade.fftPong, cascade.fftPongView, cascade.fftPongAllocation);
    destroyImage(cascade.displacementMap, cascade.displacementMapView, cascade.displacementAllocation);
    destroyImage(cascade.normalMap, cascade.normalMapView, cascade.normalAllocation);
    destroyImage(cascade.foamMap, cascade.foamMapView, cascade.foamAllocation);
}

bool OceanFFT::createImage(VkImage& image, VkImageView& view, VmaAllocation& allocation,
                           VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(allocator, image, allocation);
        return false;
    }

    return true;
}

bool OceanFFT::createCascade(Cascade& cascade, const CascadeConfig& config) {
    uint32_t res = static_cast<uint32_t>(params.resolution);

    // Spectrum textures (RGBA32F for complex H0 + conjugate)
    if (!createImage(cascade.h0Spectrum, cascade.h0SpectrumView, cascade.h0Allocation,
                     VK_FORMAT_R32G32B32A32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Angular frequency (R32F)
    if (!createImage(cascade.omegaSpectrum, cascade.omegaSpectrumView, cascade.omegaAllocation,
                     VK_FORMAT_R32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Time-evolved spectra (RG32F for complex values)
    if (!createImage(cascade.hktDy, cascade.hktDyView, cascade.hktDyAllocation,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }
    if (!createImage(cascade.hktDx, cascade.hktDxView, cascade.hktDxAllocation,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }
    if (!createImage(cascade.hktDz, cascade.hktDzView, cascade.hktDzAllocation,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // FFT ping-pong buffers (RG32F)
    if (!createImage(cascade.fftPing, cascade.fftPingView, cascade.fftPingAllocation,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (!createImage(cascade.fftPong, cascade.fftPongView, cascade.fftPongAllocation,
                     VK_FORMAT_R32G32_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }

    // Output textures
    // Displacement: RGBA16F (xyz = displacement, w = jacobian)
    if (!createImage(cascade.displacementMap, cascade.displacementMapView, cascade.displacementAllocation,
                     VK_FORMAT_R16G16B16A16_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Normal: RGBA16F (xyz = normal)
    if (!createImage(cascade.normalMap, cascade.normalMapView, cascade.normalAllocation,
                     VK_FORMAT_R16G16B16A16_SFLOAT, res, res,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    // Foam: R16F
    if (!createImage(cascade.foamMap, cascade.foamMapView, cascade.foamAllocation,
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
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        // H0 spectrum output
        bindings[0].binding = Bindings::OCEAN_SPECTRUM_H0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Omega spectrum output
        bindings[1].binding = Bindings::OCEAN_SPECTRUM_OMEGA;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Parameters UBO
        bindings[2].binding = Bindings::OCEAN_SPECTRUM_PARAMS;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &spectrumDescLayout) != VK_SUCCESS) {
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &spectrumDescLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &spectrumPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_spectrum.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_spectrum.comp.spv");
            return false;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = spectrumPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &spectrumPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            return false;
        }
    }

    // =========================================================================
    // Time Evolution Pipeline
    // =========================================================================
    {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

        // Output spectra
        bindings[0].binding = Bindings::OCEAN_HKT_DY;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = Bindings::OCEAN_HKT_DX;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = Bindings::OCEAN_HKT_DZ;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Input spectra
        bindings[3].binding = Bindings::OCEAN_H0_INPUT;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[4].binding = Bindings::OCEAN_OMEGA_INPUT;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &timeEvolutionDescLayout) != VK_SUCCESS) {
            return false;
        }

        // Push constants for time and parameters
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 4;  // time, resolution, oceanSize, choppiness

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &timeEvolutionDescLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &timeEvolutionPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_time_evolution.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_time_evolution.comp.spv");
            return false;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = timeEvolutionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &timeEvolutionPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            return false;
        }
    }

    // =========================================================================
    // FFT Pipeline
    // =========================================================================
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = Bindings::OCEAN_FFT_INPUT;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = Bindings::OCEAN_FFT_OUTPUT;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &fftDescLayout) != VK_SUCCESS) {
            return false;
        }

        // Push constants: stage, direction, resolution, inverse
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(int) * 4;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &fftDescLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &fftPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_fft.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_fft.comp.spv");
            return false;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = fftPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &fftPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            return false;
        }
    }

    // =========================================================================
    // Displacement Generation Pipeline
    // =========================================================================
    {
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

        // Input FFT results
        bindings[0].binding = Bindings::OCEAN_DISP_DY;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = Bindings::OCEAN_DISP_DX;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = Bindings::OCEAN_DISP_DZ;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Outputs
        bindings[3].binding = Bindings::OCEAN_DISP_OUTPUT;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[4].binding = Bindings::OCEAN_NORMAL_OUTPUT;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[5].binding = Bindings::OCEAN_FOAM_OUTPUT;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &displacementDescLayout) != VK_SUCCESS) {
            return false;
        }

        // Push constants
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 6;  // resolution, oceanSize, heightScale, foamThreshold, foamDecay, normalStrength

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &displacementDescLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &displacementPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderPath + "/ocean_displacement.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to load ocean_displacement.comp.spv");
            return false;
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = displacementPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &displacementPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
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
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = totalSets * 6;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = totalSets * 2;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = cascadeCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = totalSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    // Create UBOs for spectrum parameters
    spectrumUBOs.resize(cascadeCount);
    spectrumUBOAllocations.resize(cascadeCount);
    spectrumUBOMapped.resize(cascadeCount);

    for (int i = 0; i < cascadeCount; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(SpectrumUBO);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &spectrumUBOs[i],
                            &spectrumUBOAllocations[i], &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        spectrumUBOMapped[i] = allocationInfo.pMappedData;
    }

    // Allocate descriptor sets for each cascade
    spectrumDescSets.resize(cascadeCount);
    timeEvolutionDescSets.resize(cascadeCount);
    displacementDescSets.resize(cascadeCount);

    for (int i = 0; i < cascadeCount; i++) {
        // Spectrum descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &spectrumDescLayout;

            if (vkAllocateDescriptorSets(device, &allocInfo, &spectrumDescSets[i]) != VK_SUCCESS) {
                return false;
            }

            // Update spectrum descriptor set
            VkDescriptorImageInfo h0Info{};
            h0Info.imageView = cascades[i].h0SpectrumView;
            h0Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo omegaInfo{};
            omegaInfo.imageView = cascades[i].omegaSpectrumView;
            omegaInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = spectrumUBOs[i];
            uboInfo.offset = 0;
            uboInfo.range = sizeof(SpectrumUBO);

            std::array<VkWriteDescriptorSet, 3> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = spectrumDescSets[i];
            writes[0].dstBinding = Bindings::OCEAN_SPECTRUM_H0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo = &h0Info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = spectrumDescSets[i];
            writes[1].dstBinding = Bindings::OCEAN_SPECTRUM_OMEGA;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &omegaInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = spectrumDescSets[i];
            writes[2].dstBinding = Bindings::OCEAN_SPECTRUM_PARAMS;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].pBufferInfo = &uboInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // Time evolution descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &timeEvolutionDescLayout;

            if (vkAllocateDescriptorSets(device, &allocInfo, &timeEvolutionDescSets[i]) != VK_SUCCESS) {
                return false;
            }

            VkDescriptorImageInfo hktDyInfo{};
            hktDyInfo.imageView = cascades[i].hktDyView;
            hktDyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo hktDxInfo{};
            hktDxInfo.imageView = cascades[i].hktDxView;
            hktDxInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo hktDzInfo{};
            hktDzInfo.imageView = cascades[i].hktDzView;
            hktDzInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo h0SamplerInfo{};
            h0SamplerInfo.sampler = sampler;
            h0SamplerInfo.imageView = cascades[i].h0SpectrumView;
            h0SamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo omegaSamplerInfo{};
            omegaSamplerInfo.sampler = sampler;
            omegaSamplerInfo.imageView = cascades[i].omegaSpectrumView;
            omegaSamplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 5> writes{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = timeEvolutionDescSets[i];
            writes[0].dstBinding = Bindings::OCEAN_HKT_DY;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo = &hktDyInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = timeEvolutionDescSets[i];
            writes[1].dstBinding = Bindings::OCEAN_HKT_DX;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &hktDxInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = timeEvolutionDescSets[i];
            writes[2].dstBinding = Bindings::OCEAN_HKT_DZ;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[2].pImageInfo = &hktDzInfo;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = timeEvolutionDescSets[i];
            writes[3].dstBinding = Bindings::OCEAN_H0_INPUT;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].pImageInfo = &h0SamplerInfo;

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = timeEvolutionDescSets[i];
            writes[4].dstBinding = Bindings::OCEAN_OMEGA_INPUT;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].pImageInfo = &omegaSamplerInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // Displacement descriptor set
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &displacementDescLayout;

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
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // FFT for each displacement component
        recordFFT(cmd, cascade, cascade.hktDy, cascade.hktDyView,
                  cascade.fftPing, cascade.fftPingView);
        recordFFT(cmd, cascade, cascade.hktDx, cascade.hktDxView,
                  cascade.fftPong, cascade.fftPongView);
        recordFFT(cmd, cascade, cascade.hktDz, cascade.hktDzView,
                  cascade.fftPing, cascade.fftPingView);

        // Barrier before displacement generation
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // Generate final displacement/normal/foam maps
        recordDisplacementGeneration(cmd, cascade);
    }

    // Final barrier before water shader can sample
    VkMemoryBarrier finalBarrier{};
    finalBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         1, &finalBarrier, 0, nullptr, 0, nullptr);
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
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void OceanFFT::recordSpectrumGeneration(VkCommandBuffer cmd, Cascade& cascade, uint32_t seed) {
    // Transition images to general layout
    std::array<VkImageMemoryBarrier, 2> barriers{};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].image = cascade.h0Spectrum;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].image = cascade.omegaSpectrum;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());

    // Bind pipeline and descriptor set
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spectrumPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spectrumPipelineLayout,
                            0, 1, &spectrumDescSets[cascadeIndex], 0, nullptr);

    // Dispatch
    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupCount, groupCount, 1);

    // Transition to shader read for time evolution
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());
}

void OceanFFT::recordTimeEvolution(VkCommandBuffer cmd, Cascade& cascade, float time) {
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    // Transition output images to general layout
    std::array<VkImageMemoryBarrier, 3> barriers{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].image = cascade.hktDy;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1] = barriers[0];
    barriers[1].image = cascade.hktDx;

    barriers[2] = barriers[0];
    barriers[2].image = cascade.hktDz;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, timeEvolutionPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, timeEvolutionPipelineLayout,
                            0, 1, &timeEvolutionDescSets[cascadeIndex], 0, nullptr);

    // Push constants
    struct {
        float time;
        int resolution;
        float oceanSize;
        float choppiness;
    } pushConstants = {
        time,
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.choppiness
    };

    vkCmdPushConstants(cmd, timeEvolutionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
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
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &fftDescLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &fftDescSet) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate FFT descriptor set");
        return;
    }

    // Track which buffer has current data
    VkImage currentInput = input;
    VkImageView currentInputView = inputView;
    VkImage currentOutput = cascade.fftPing;
    VkImageView currentOutputView = cascade.fftPingView;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipeline);

    // Horizontal FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        // Update descriptor set
        VkDescriptorImageInfo inputInfo{};
        inputInfo.imageView = currentInputView;
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = currentOutputView;
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = fftDescSet;
        writes[0].dstBinding = Bindings::OCEAN_FFT_INPUT;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = fftDescSet;
        writes[1].dstBinding = Bindings::OCEAN_FFT_OUTPUT;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipelineLayout,
                                0, 1, &fftDescSet, 0, nullptr);

        // Push constants: stage, direction (0=horizontal), resolution, inverse (1=IFFT)
        int pushData[4] = {stage, 0, params.resolution, 1};
        vkCmdPushConstants(cmd, fftPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushData), pushData);

        vkCmdDispatch(cmd, groupCount, groupCount, 1);

        // Barrier between stages
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        // Swap buffers for ping-pong
        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        // Use ping/pong buffers for subsequent stages
        if (stage == 0) {
            currentOutput = cascade.fftPong;
            currentOutputView = cascade.fftPongView;
        } else {
            if (currentOutput == cascade.fftPing) {
                currentOutput = cascade.fftPong;
                currentOutputView = cascade.fftPongView;
            } else {
                currentOutput = cascade.fftPing;
                currentOutputView = cascade.fftPingView;
            }
        }
    }

    // Vertical FFT passes
    for (int stage = 0; stage < numStages; stage++) {
        VkDescriptorImageInfo inputInfo{};
        inputInfo.imageView = currentInputView;
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = currentOutputView;
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = fftDescSet;
        writes[0].dstBinding = Bindings::OCEAN_FFT_INPUT;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = fftDescSet;
        writes[1].dstBinding = Bindings::OCEAN_FFT_OUTPUT;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fftPipelineLayout,
                                0, 1, &fftDescSet, 0, nullptr);

        // Push constants: stage, direction (1=vertical), resolution, inverse
        int pushData[4] = {stage, 1, params.resolution, 1};
        vkCmdPushConstants(cmd, fftPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushData), pushData);

        vkCmdDispatch(cmd, groupCount, groupCount, 1);

        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);

        std::swap(currentInput, currentOutput);
        std::swap(currentInputView, currentOutputView);

        if (currentOutput == cascade.fftPing) {
            currentOutput = cascade.fftPong;
            currentOutputView = cascade.fftPongView;
        } else {
            currentOutput = cascade.fftPing;
            currentOutputView = cascade.fftPingView;
        }
    }
}

void OceanFFT::recordDisplacementGeneration(VkCommandBuffer cmd, Cascade& cascade) {
    int cascadeIndex = static_cast<int>(&cascade - &cascades[0]);

    // Transition output images
    std::array<VkImageMemoryBarrier, 3> barriers{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].image = cascade.displacementMap;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1] = barriers[0];
    barriers[1].image = cascade.normalMap;

    barriers[2] = barriers[0];
    barriers[2].image = cascade.foamMap;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());

    // Update displacement descriptor set with current FFT results
    VkDescriptorImageInfo dyInfo{};
    dyInfo.imageView = cascade.fftPingView;  // Final result location
    dyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo dxInfo{};
    dxInfo.imageView = cascade.fftPongView;
    dxInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo dzInfo{};
    dzInfo.imageView = cascade.fftPingView;
    dzInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo dispOutInfo{};
    dispOutInfo.imageView = cascade.displacementMapView;
    dispOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo normalOutInfo{};
    normalOutInfo.imageView = cascade.normalMapView;
    normalOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo foamOutInfo{};
    foamOutInfo.imageView = cascade.foamMapView;
    foamOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = displacementDescSets[cascadeIndex];
    writes[0].dstBinding = Bindings::OCEAN_DISP_DY;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &dyInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = displacementDescSets[cascadeIndex];
    writes[1].dstBinding = Bindings::OCEAN_DISP_DX;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &dxInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = displacementDescSets[cascadeIndex];
    writes[2].dstBinding = Bindings::OCEAN_DISP_DZ;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &dzInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = displacementDescSets[cascadeIndex];
    writes[3].dstBinding = Bindings::OCEAN_DISP_OUTPUT;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].pImageInfo = &dispOutInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = displacementDescSets[cascadeIndex];
    writes[4].dstBinding = Bindings::OCEAN_NORMAL_OUTPUT;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].pImageInfo = &normalOutInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = displacementDescSets[cascadeIndex];
    writes[5].dstBinding = Bindings::OCEAN_FOAM_OUTPUT;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].pImageInfo = &foamOutInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Bind and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipelineLayout,
                            0, 1, &displacementDescSets[cascadeIndex], 0, nullptr);

    // Push constants
    struct {
        int resolution;
        float oceanSize;
        float heightScale;
        float foamThreshold;
        float foamDecay;
        float normalStrength;
    } pushConstants = {
        params.resolution,
        cascade.config.oceanSize,
        cascade.config.heightScale,
        params.foamThreshold,
        0.9f,  // foam decay
        params.normalStrength
    };

    vkCmdPushConstants(cmd, displacementPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    uint32_t groupSize = 16;
    uint32_t groupCount = (params.resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupCount, groupCount, 1);

    // Transition outputs to shader read
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barriers[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[2].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(barriers.size()), barriers.data());
}

VkImageView OceanFFT::getDisplacementView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].displacementMapView;
}

VkImageView OceanFFT::getNormalView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].normalMapView;
}

VkImageView OceanFFT::getFoamView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].foamMapView;
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
