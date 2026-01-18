#include "AtmosphereLUTSystem.h"
#include "VmaImage.h"
#include "SamplerFactory.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

bool AtmosphereLUTSystem::createTransmittanceLUT() {
    ManagedImage image;
    if (!ImageBuilder(allocator)
            .setExtent(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, image, transmittanceLUTView)) {
        SDL_Log("Failed to create transmittance LUT");
        return false;
    }
    image.releaseToRaw(transmittanceLUT, transmittanceLUTAllocation);
    return true;
}

bool AtmosphereLUTSystem::createMultiScatterLUT() {
    ManagedImage image;
    if (!ImageBuilder(allocator)
            .setExtent(MULTISCATTER_SIZE, MULTISCATTER_SIZE)
            .setFormat(VK_FORMAT_R16G16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, image, multiScatterLUTView)) {
        SDL_Log("Failed to create multi-scatter LUT");
        return false;
    }
    image.releaseToRaw(multiScatterLUT, multiScatterLUTAllocation);
    return true;
}

bool AtmosphereLUTSystem::createSkyViewLUT() {
    ManagedImage image;
    if (!ImageBuilder(allocator)
            .setExtent(SKYVIEW_WIDTH, SKYVIEW_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, image, skyViewLUTView)) {
        SDL_Log("Failed to create sky-view LUT");
        return false;
    }
    image.releaseToRaw(skyViewLUT, skyViewLUTAllocation);
    return true;
}

bool AtmosphereLUTSystem::createIrradianceLUTs() {
    // Create Rayleigh Irradiance LUT (64×16, RGBA16F)
    ManagedImage rayleighImage;
    if (!ImageBuilder(allocator)
            .setExtent(IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, rayleighImage, rayleighIrradianceLUTView)) {
        SDL_Log("Failed to create Rayleigh irradiance LUT");
        return false;
    }
    rayleighImage.releaseToRaw(rayleighIrradianceLUT, rayleighIrradianceLUTAllocation);

    // Create Mie Irradiance LUT (same dimensions and format)
    ManagedImage mieImage;
    if (!ImageBuilder(allocator)
            .setExtent(IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, mieImage, mieIrradianceLUTView)) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }
    mieImage.releaseToRaw(mieIrradianceLUT, mieIrradianceLUTAllocation);

    return true;
}

bool AtmosphereLUTSystem::createCloudMapLUT() {
    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    ManagedImage image;
    if (!ImageBuilder(allocator)
            .setExtent(CLOUDMAP_SIZE, CLOUDMAP_SIZE)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(device, image, cloudMapLUTView)) {
        SDL_Log("Failed to create cloud map LUT");
        return false;
    }
    image.releaseToRaw(cloudMapLUT, cloudMapLUTAllocation);
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
