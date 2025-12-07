#include "AtmosphereLUTSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <SDL3/SDL_log.h>
#include <array>
#include <vector>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool AtmosphereLUTSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createTransmittanceLUT()) return false;
    if (!createMultiScatterLUT()) return false;
    if (!createSkyViewLUT()) return false;
    if (!createIrradianceLUTs()) return false;
    if (!createCloudMapLUT()) return false;
    if (!createLUTSampler()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipelines()) return false;

    SDL_Log("Atmosphere LUT System initialized");
    return true;
}

void AtmosphereLUTSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyLUTResources();

    if (uniformBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, uniformBuffer, uniformAllocation);
        uniformBuffer = VK_NULL_HANDLE;
    }

    // Destroy per-frame uniform buffers
    BufferUtils::destroyBuffers(allocator, skyViewUniformBuffers);
    BufferUtils::destroyBuffers(allocator, cloudMapUniformBuffers);

    if (transmittancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, transmittancePipeline, nullptr);
        transmittancePipeline = VK_NULL_HANDLE;
    }
    if (multiScatterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, multiScatterPipeline, nullptr);
        multiScatterPipeline = VK_NULL_HANDLE;
    }
    if (skyViewPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skyViewPipeline, nullptr);
        skyViewPipeline = VK_NULL_HANDLE;
    }
    if (irradiancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, irradiancePipeline, nullptr);
        irradiancePipeline = VK_NULL_HANDLE;
    }
    if (cloudMapPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, cloudMapPipeline, nullptr);
        cloudMapPipeline = VK_NULL_HANDLE;
    }

    if (transmittancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, transmittancePipelineLayout, nullptr);
        transmittancePipelineLayout = VK_NULL_HANDLE;
    }
    if (multiScatterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, multiScatterPipelineLayout, nullptr);
        multiScatterPipelineLayout = VK_NULL_HANDLE;
    }
    if (skyViewPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skyViewPipelineLayout, nullptr);
        skyViewPipelineLayout = VK_NULL_HANDLE;
    }
    if (irradiancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, irradiancePipelineLayout, nullptr);
        irradiancePipelineLayout = VK_NULL_HANDLE;
    }
    if (cloudMapPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, cloudMapPipelineLayout, nullptr);
        cloudMapPipelineLayout = VK_NULL_HANDLE;
    }

    if (transmittanceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, transmittanceDescriptorSetLayout, nullptr);
        transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (multiScatterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, multiScatterDescriptorSetLayout, nullptr);
        multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (skyViewDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skyViewDescriptorSetLayout, nullptr);
        skyViewDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (irradianceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, irradianceDescriptorSetLayout, nullptr);
        irradianceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (cloudMapDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, cloudMapDescriptorSetLayout, nullptr);
        cloudMapDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (lutSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, lutSampler, nullptr);
        lutSampler = VK_NULL_HANDLE;
    }
}

void AtmosphereLUTSystem::destroyLUTResources() {
    if (transmittanceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, transmittanceLUTView, nullptr);
        transmittanceLUTView = VK_NULL_HANDLE;
    }
    if (transmittanceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, transmittanceLUT, transmittanceLUTAllocation);
        transmittanceLUT = VK_NULL_HANDLE;
    }

    if (multiScatterLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, multiScatterLUTView, nullptr);
        multiScatterLUTView = VK_NULL_HANDLE;
    }
    if (multiScatterLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, multiScatterLUT, multiScatterLUTAllocation);
        multiScatterLUT = VK_NULL_HANDLE;
    }

    if (skyViewLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, skyViewLUTView, nullptr);
        skyViewLUTView = VK_NULL_HANDLE;
    }
    if (skyViewLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, skyViewLUT, skyViewLUTAllocation);
        skyViewLUT = VK_NULL_HANDLE;
    }

    if (rayleighIrradianceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, rayleighIrradianceLUTView, nullptr);
        rayleighIrradianceLUTView = VK_NULL_HANDLE;
    }
    if (rayleighIrradianceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, rayleighIrradianceLUT, rayleighIrradianceLUTAllocation);
        rayleighIrradianceLUT = VK_NULL_HANDLE;
    }

    if (mieIrradianceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, mieIrradianceLUTView, nullptr);
        mieIrradianceLUTView = VK_NULL_HANDLE;
    }
    if (mieIrradianceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, mieIrradianceLUT, mieIrradianceLUTAllocation);
        mieIrradianceLUT = VK_NULL_HANDLE;
    }

    if (cloudMapLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, cloudMapLUTView, nullptr);
        cloudMapLUTView = VK_NULL_HANDLE;
    }
    if (cloudMapLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, cloudMapLUT, cloudMapLUTAllocation);
        cloudMapLUT = VK_NULL_HANDLE;
    }
}

bool AtmosphereLUTSystem::createTransmittanceLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &transmittanceLUT, &transmittanceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = transmittanceLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &transmittanceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createMultiScatterLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.extent = {MULTISCATTER_SIZE, MULTISCATTER_SIZE, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &multiScatterLUT, &multiScatterLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = multiScatterLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &multiScatterLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createSkyViewLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {SKYVIEW_WIDTH, SKYVIEW_HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &skyViewLUT, &skyViewLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = skyViewLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &skyViewLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createIrradianceLUTs() {
    // Create Rayleigh Irradiance LUT (64×16, RGBA16F)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &rayleighIrradianceLUT, &rayleighIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = rayleighIrradianceLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &rayleighIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT view");
        return false;
    }

    // Create Mie Irradiance LUT (same dimensions and format)
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &mieIrradianceLUT, &mieIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }

    viewInfo.image = mieIrradianceLUT;
    if (vkCreateImageView(device, &viewInfo, nullptr, &mieIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createCloudMapLUT() {
    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {CLOUDMAP_SIZE, CLOUDMAP_SIZE, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &cloudMapLUT, &cloudMapLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cloudMapLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &cloudMapLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createLUTSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &lutSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create LUT sampler");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createUniformBuffer() {
    // Create atmosphere LUT uniform buffer (single buffer for one-time LUT computations)
    VkDeviceSize bufferSize = sizeof(AtmosphereUniforms);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &uniformBuffer, &uniformAllocation, &allocResult) != VK_SUCCESS) {
        SDL_Log("Failed to create atmosphere uniform buffer");
        return false;
    }

    uniformMappedPtr = allocResult.pMappedData;

    // Create per-frame uniform buffers for sky view LUT updates (double-buffered)
    BufferUtils::PerFrameBufferBuilder skyViewBuilder;
    if (!skyViewBuilder.setAllocator(allocator)
             .setFrameCount(framesInFlight)
             .setSize(sizeof(AtmosphereUniforms))
             .build(skyViewUniformBuffers)) {
        SDL_Log("Failed to create sky view per-frame uniform buffers");
        return false;
    }

    // Create per-frame uniform buffers for cloud map LUT updates (double-buffered)
    BufferUtils::PerFrameBufferBuilder cloudMapBuilder;
    if (!cloudMapBuilder.setAllocator(allocator)
             .setFrameCount(framesInFlight)
             .setSize(sizeof(CloudMapUniforms))
             .build(cloudMapUniformBuffers)) {
        SDL_Log("Failed to create cloud map per-frame uniform buffers");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSetLayouts() {
    // Transmittance LUT descriptor set layout (just output image and uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {outputImage, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &transmittanceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &transmittanceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &transmittancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline layout");
            return false;
        }
    }

    // Multi-scatter LUT descriptor set layout (transmittance input, output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {outputImage, transmittanceInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &multiScatterDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &multiScatterDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &multiScatterPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline layout");
            return false;
        }
    }

    // Sky-view LUT descriptor set layout (transmittance + multiscatter inputs, output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto multiScatterInput = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(3)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {outputImage, transmittanceInput, multiScatterInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyViewDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &skyViewDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyViewPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline layout");
            return false;
        }
    }

    // Irradiance LUT descriptor set layout (Phase 4.1.9)
    // Two output images (Rayleigh and Mie), transmittance input, uniform buffer
    {
        auto rayleighOutput = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto mieOutput = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto transmittanceInput = BindingBuilder()
            .setBinding(2)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(3)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {rayleighOutput, mieOutput, transmittanceInput, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &irradianceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &irradianceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance pipeline layout");
            return false;
        }
    }

    // Cloud Map LUT descriptor set layout (output image, uniform buffer)
    {
        auto outputImage = BindingBuilder()
            .setBinding(0)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        auto uniformBuffer = BindingBuilder()
            .setBinding(1)
            .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {outputImage, uniformBuffer};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cloudMapDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &cloudMapDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &cloudMapPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map pipeline layout");
            return false;
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSets() {
    // Allocate transmittance descriptor set using managed pool
    {
        transmittanceDescriptorSet = descriptorPool->allocateSingle(transmittanceDescriptorSetLayout);
        if (transmittanceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate transmittance descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 2> writes{};

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = transmittanceLUTView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = transmittanceDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = transmittanceDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate multi-scatter descriptor set using managed pool
    {
        multiScatterDescriptorSet = descriptorPool->allocateSingle(multiScatterDescriptorSetLayout);
        if (multiScatterDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate multi-scatter descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 3> writes{};

        VkDescriptorImageInfo outputImageInfo{};
        outputImageInfo.imageView = multiScatterLUTView;
        outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = multiScatterDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &outputImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = multiScatterDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &transmittanceImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = multiScatterDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate per-frame sky-view descriptor sets (double-buffered) using managed pool
    {
        skyViewDescriptorSets = descriptorPool->allocate(skyViewDescriptorSetLayout, framesInFlight);
        if (skyViewDescriptorSets.empty()) {
            SDL_Log("Failed to allocate sky-view descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            std::array<VkWriteDescriptorSet, 4> writes{};

            VkDescriptorImageInfo outputImageInfo{};
            outputImageInfo.imageView = skyViewLUTView;
            outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = skyViewDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &outputImageInfo;

            VkDescriptorImageInfo transmittanceImageInfo{};
            transmittanceImageInfo.imageView = transmittanceLUTView;
            transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            transmittanceImageInfo.sampler = lutSampler;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = skyViewDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &transmittanceImageInfo;

            VkDescriptorImageInfo multiScatterImageInfo{};
            multiScatterImageInfo.imageView = multiScatterLUTView;
            multiScatterImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            multiScatterImageInfo.sampler = lutSampler;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = skyViewDescriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &multiScatterImageInfo;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = skyViewUniformBuffers.buffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(AtmosphereUniforms);

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = skyViewDescriptorSets[i];
            writes[3].dstBinding = 3;
            writes[3].dstArrayElement = 0;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Allocate irradiance descriptor set using managed pool
    {
        irradianceDescriptorSet = descriptorPool->allocateSingle(irradianceDescriptorSetLayout);
        if (irradianceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate irradiance descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 4> writes{};

        VkDescriptorImageInfo rayleighImageInfo{};
        rayleighImageInfo.imageView = rayleighIrradianceLUTView;
        rayleighImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = irradianceDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &rayleighImageInfo;

        VkDescriptorImageInfo mieImageInfo{};
        mieImageInfo.imageView = mieIrradianceLUTView;
        mieImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = irradianceDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &mieImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = irradianceDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &transmittanceImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereUniforms);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = irradianceDescriptorSet;
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate per-frame cloud map descriptor sets (double-buffered) using managed pool
    {
        cloudMapDescriptorSets = descriptorPool->allocate(cloudMapDescriptorSetLayout, framesInFlight);
        if (cloudMapDescriptorSets.empty()) {
            SDL_Log("Failed to allocate cloud map descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            std::array<VkWriteDescriptorSet, 2> writes{};

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = cloudMapLUTView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = cloudMapDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &imageInfo;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = cloudMapUniformBuffers.buffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CloudMapUniforms);

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = cloudMapDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createComputePipelines() {
    // Create transmittance pipeline
    {
        std::string shaderFile = shaderPath + "/transmittance_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = transmittancePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transmittancePipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create multi-scatter pipeline
    {
        std::string shaderFile = shaderPath + "/multiscatter_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = multiScatterPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &multiScatterPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create sky-view pipeline
    {
        std::string shaderFile = shaderPath + "/skyview_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = skyViewPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyViewPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create irradiance pipeline
    {
        std::string shaderFile = shaderPath + "/irradiance_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = irradiancePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &irradiancePipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create cloud map pipeline
    {
        std::string shaderFile = shaderPath + "/cloudmap_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = cloudMapPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cloudMapPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    return true;
}

void AtmosphereLUTSystem::computeTransmittanceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = transmittanceLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipelineLayout,
                           0, 1, &transmittanceDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (TRANSMITTANCE_WIDTH + 15) / 16;
    uint32_t groupCountY = (TRANSMITTANCE_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in later stages
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed transmittance LUT (%dx%d)", TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeMultiScatterLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = multiScatterLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipelineLayout,
                           0, 1, &multiScatterDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (MULTISCATTER_SIZE + 7) / 8;
    uint32_t groupCountY = (MULTISCATTER_SIZE + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed multi-scatter LUT (%dx%d)", MULTISCATTER_SIZE, MULTISCATTER_SIZE);
}

void AtmosphereLUTSystem::computeIrradianceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereUniforms));

    // Transition both irradiance LUTs to GENERAL layout for compute write
    std::array<VkImageMemoryBarrier, 2> barriers{};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = rayleighIrradianceLUT;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    barriers[1] = barriers[0];
    barriers[1].image = mieIrradianceLUT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irradiancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irradiancePipelineLayout,
                           0, 1, &irradianceDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (IRRADIANCE_WIDTH + 7) / 8;
    uint32_t groupCountY = (IRRADIANCE_HEIGHT + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition both LUTs to SHADER_READ for sampling
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].image = rayleighIrradianceLUT;

    barriers[1] = barriers[0];
    barriers[1].image = mieIrradianceLUT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());

    SDL_Log("Computed irradiance LUTs (%dx%d)", IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir,
                                            const glm::vec3& cameraPos, float cameraAltitude) {
    // Update uniform buffer (use frame 0's per-frame buffer for startup computation)
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.sunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[0], &uniforms, sizeof(AtmosphereUniforms));

    // Transition to GENERAL layout for compute write (from UNDEFINED at startup)
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = skyViewLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipelineLayout,
                           0, 1, &skyViewDescriptorSets[0], 0, nullptr);

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed sky-view LUT (%dx%d)", SKYVIEW_WIDTH, SKYVIEW_HEIGHT);
}

void AtmosphereLUTSystem::updateSkyViewLUT(VkCommandBuffer cmd, uint32_t frameIndex,
                                           const glm::vec3& sunDir,
                                           const glm::vec3& cameraPos, float cameraAltitude) {
    // Update per-frame uniform buffer with new sun direction (double-buffered)
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.sunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(skyViewUniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(AtmosphereUniforms));

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    // (LUT was already in read-only from previous frame)
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = skyViewLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipelineLayout,
                           0, 1, &skyViewDescriptorSets[frameIndex], 0, nullptr);

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void AtmosphereLUTSystem::computeCloudMapLUT(VkCommandBuffer cmd, const glm::vec3& windOffset, float time) {
    // Update cloud map uniform buffer (use frame 0's per-frame buffer for startup computation)
    CloudMapUniforms uniforms{};
    uniforms.windOffset = glm::vec4(windOffset, time);
    uniforms.coverage = 0.6f;      // 60% cloud coverage
    uniforms.density = 1.0f;       // Full density multiplier
    uniforms.sharpness = 0.3f;     // Coverage transition sharpness
    uniforms.detailScale = 2.5f;   // Detail noise scale
    memcpy(cloudMapUniformBuffers.mappedPointers[0], &uniforms, sizeof(CloudMapUniforms));

    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = cloudMapLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch (use frame 0's descriptor set for startup computation)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipelineLayout,
                           0, 1, &cloudMapDescriptorSets[0], 0, nullptr);

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in sky.frag
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed cloud map LUT (%dx%d)", CLOUDMAP_SIZE, CLOUDMAP_SIZE);
}

void AtmosphereLUTSystem::updateCloudMapLUT(VkCommandBuffer cmd, uint32_t frameIndex,
                                            const glm::vec3& windOffset, float time) {
    // Update per-frame cloud map uniform buffer (double-buffered)
    CloudMapUniforms uniforms{};
    uniforms.windOffset = glm::vec4(windOffset, time);
    uniforms.coverage = cloudCoverage;    // From UI controls
    uniforms.density = cloudDensity;      // From UI controls
    uniforms.sharpness = 0.3f;            // Coverage transition sharpness
    uniforms.detailScale = 2.5f;          // Detail noise scale
    memcpy(cloudMapUniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(CloudMapUniforms));

    // Transition from SHADER_READ_ONLY to GENERAL for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = cloudMapLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and per-frame descriptor set (double-buffered)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cloudMapPipelineLayout,
                           0, 1, &cloudMapDescriptorSets[frameIndex], 0, nullptr);

    uint32_t groupCountX = (CLOUDMAP_SIZE + 15) / 16;
    uint32_t groupCountY = (CLOUDMAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition back to SHADER_READ for sampling in sky.frag
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool AtmosphereLUTSystem::exportImageToPNG(VkImage image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename) {
    // Determine channel count from format
    uint32_t channelCount = 0;
    switch (format) {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            channelCount = 4;
            break;
        case VK_FORMAT_R16G16_SFLOAT:
            channelCount = 2;
            break;
        case VK_FORMAT_R16_SFLOAT:
            channelCount = 1;
            break;
        default:
            SDL_Log("Unsupported format for PNG export: %d", format);
            return false;
    }

    // Create staging buffer with correct size based on actual format
    VkDeviceSize imageSize = width * height * channelCount * sizeof(uint16_t);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create staging buffer for PNG export");
        return false;
    }

    // Create command buffer for the copy
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assuming graphics queue family 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo2{};
    allocInfo2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo2.commandPool = commandPool;
    allocInfo2.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo2, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition image to TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition back
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Helper lambda to convert FP16 to float32
    auto fp16ToFloat = [](uint16_t h) -> float {
        uint32_t sign = (h & 0x8000) << 16;
        uint32_t exponent = (h & 0x7C00) >> 10;
        uint32_t mantissa = (h & 0x03FF);

        if (exponent == 0) {
            if (mantissa == 0) {
                // Zero
                uint32_t f = sign;
                return *reinterpret_cast<float*>(&f);
            } else {
                // Denormalized
                exponent = 1;
                while ((mantissa & 0x0400) == 0) {
                    mantissa <<= 1;
                    exponent--;
                }
                mantissa &= 0x03FF;
            }
        } else if (exponent == 0x1F) {
            // Infinity or NaN
            uint32_t f = sign | 0x7F800000 | (mantissa << 13);
            return *reinterpret_cast<float*>(&f);
        }

        // Normalized
        exponent = exponent + (127 - 15);
        mantissa = mantissa << 13;
        uint32_t f = sign | (exponent << 23) | mantissa;
        return *reinterpret_cast<float*>(&f);
    };

    // Map and convert to 8-bit RGBA for PNG
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);

    std::vector<uint8_t> rgba8(width * height * 4);
    uint16_t* src = static_cast<uint16_t*>(data);

    // Convert with proper FP16 decoding and per-format stride
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t srcIdx = (y * width + x) * channelCount;
            uint32_t dstIdx = (y * width + x) * 4;

            // Read available channels and convert from FP16 to float
            float channels[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Default: black with alpha=1
            for (uint32_t c = 0; c < channelCount; c++) {
                channels[c] = fp16ToFloat(src[srcIdx + c]);
            }

            // For RG formats, copy R to all RGB channels for grayscale visualization
            if (channelCount == 2) {
                // Use R for luminance, G for alpha (common for multi-scatter LUT)
                channels[2] = channels[0]; // B = R
                channels[3] = channels[1]; // A = G
                channels[1] = channels[0]; // G = R
            } else if (channelCount == 1) {
                channels[1] = channels[0];
                channels[2] = channels[0];
                channels[3] = 1.0f;
            }

            // Simple tonemapping: clamp to [0,1] and convert to 8-bit
            // No arbitrary scaling - the LUT values are already in the correct range
            for (uint32_t c = 0; c < 4; c++) {
                float val = glm::clamp(channels[c], 0.0f, 1.0f);
                rgba8[dstIdx + c] = static_cast<uint8_t>(val * 255.0f);
            }
        }
    }

    vmaUnmapMemory(allocator, stagingAllocation);

    // Write PNG
    int result = stbi_write_png(filename.c_str(), width, height, 4, rgba8.data(), width * 4);

    // Cleanup
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    vkDestroyCommandPool(device, commandPool, nullptr);

    if (result == 0) {
        SDL_Log("Failed to write PNG: %s", filename.c_str());
        return false;
    }

    SDL_Log("Exported LUT to: %s (%d channels)", filename.c_str(), channelCount);
    return true;
}

bool AtmosphereLUTSystem::exportLUTsAsPNG(const std::string& outputDir) {
    SDL_Log("Exporting atmosphere LUTs as PNG...");

    bool success = true;
    success &= exportImageToPNG(transmittanceLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT,
                                outputDir + "/transmittance_lut.png");

    success &= exportImageToPNG(multiScatterLUT, VK_FORMAT_R16G16_SFLOAT,
                                MULTISCATTER_SIZE, MULTISCATTER_SIZE,
                                outputDir + "/multiscatter_lut.png");

    success &= exportImageToPNG(skyViewLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                SKYVIEW_WIDTH, SKYVIEW_HEIGHT,
                                outputDir + "/skyview_lut.png");

    success &= exportImageToPNG(cloudMapLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                CLOUDMAP_SIZE, CLOUDMAP_SIZE,
                                outputDir + "/cloudmap_lut.png");

    return success;
}

void AtmosphereLUTSystem::recomputeStaticLUTs(VkCommandBuffer cmd) {
    if (!paramsDirty) return;

    // Update uniform buffer with new atmosphere parameters
    AtmosphereUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereUniforms));

    // Recompute the static LUTs that depend on atmosphere parameters
    computeTransmittanceLUT(cmd);
    computeMultiScatterLUT(cmd);
    computeIrradianceLUT(cmd);

    paramsDirty = false;
    SDL_Log("Atmosphere LUTs recomputed with new parameters");
}
