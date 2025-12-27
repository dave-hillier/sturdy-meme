#include "AtmosphereLUTSystem.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

bool AtmosphereLUTSystem::createTransmittanceLUT() {
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &transmittanceLUT, &transmittanceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(transmittanceLUT)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &transmittanceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createMultiScatterLUT() {
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16Sfloat)
        .setExtent(vk::Extent3D{MULTISCATTER_SIZE, MULTISCATTER_SIZE, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &multiScatterLUT, &multiScatterLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(multiScatterLUT)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &multiScatterLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createSkyViewLUT() {
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{SKYVIEW_WIDTH, SKYVIEW_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &skyViewLUT, &skyViewLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(skyViewLUT)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &skyViewLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createIrradianceLUTs() {
    // Create Rayleigh Irradiance LUT (64×16, RGBA16F)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &rayleighIrradianceLUT, &rayleighIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(rayleighIrradianceLUT)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &rayleighIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT view");
        return false;
    }

    // Create Mie Irradiance LUT (same dimensions and format)
    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &mieIrradianceLUT, &mieIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }

    viewInfo.setImage(mieIrradianceLUT);
    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &mieIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createCloudMapLUT() {
    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{CLOUDMAP_SIZE, CLOUDMAP_SIZE, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &cloudMapLUT, &cloudMapLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(cloudMapLUT)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &cloudMapLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createLUTSampler() {
    if (!VulkanResourceFactory::createSamplerLinearClamp(device, lutSampler)) {
        SDL_Log("Failed to create LUT sampler");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createUniformBuffer() {
    // Create static uniform buffer for one-time LUT computations (frame count of 1 for consistency)
    BufferUtils::PerFrameBufferBuilder staticBuilder;
    if (!staticBuilder.setAllocator(allocator)
             .setFrameCount(1)
             .setSize(sizeof(AtmosphereUniforms))
             .build(staticUniformBuffers)) {
        SDL_Log("Failed to create static atmosphere uniform buffer");
        return false;
    }

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
