#include "AtmosphereLUTSystem.h"
#include "VmaResources.h"
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

    try {
        transmittanceLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create transmittance LUT view: %s", e.what());
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

    try {
        multiScatterLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create multi-scatter LUT view: %s", e.what());
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

    try {
        skyViewLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create sky-view LUT view: %s", e.what());
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

    try {
        rayleighIrradianceLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create Rayleigh irradiance LUT view: %s", e.what());
        return false;
    }

    // Create Mie Irradiance LUT (same dimensions and format)
    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &mieIrradianceLUT, &mieIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }

    viewInfo.setImage(mieIrradianceLUT);
    try {
        mieIrradianceLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create Mie irradiance LUT view: %s", e.what());
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

    try {
        cloudMapLUTView = vk::Device(device).createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create cloud map LUT view: %s", e.what());
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createLUTSampler() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }

    lutSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!lutSampler_) {
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
    vk::Device vkDevice(device);

    auto destroyView = [&](VkImageView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDevice.destroyImageView(view);
            view = VK_NULL_HANDLE;
        }
    };

    auto destroyImage = [&](VkImage& image, VmaAllocation& alloc) {
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image, alloc);
            image = VK_NULL_HANDLE;
        }
    };

    destroyView(transmittanceLUTView);
    destroyImage(transmittanceLUT, transmittanceLUTAllocation);

    destroyView(multiScatterLUTView);
    destroyImage(multiScatterLUT, multiScatterLUTAllocation);

    destroyView(skyViewLUTView);
    destroyImage(skyViewLUT, skyViewLUTAllocation);

    destroyView(rayleighIrradianceLUTView);
    destroyImage(rayleighIrradianceLUT, rayleighIrradianceLUTAllocation);

    destroyView(mieIrradianceLUTView);
    destroyImage(mieIrradianceLUT, mieIrradianceLUTAllocation);

    destroyView(cloudMapLUTView);
    destroyImage(cloudMapLUT, cloudMapLUTAllocation);
}
