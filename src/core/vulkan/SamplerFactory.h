#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <SDL3/SDL_log.h>

// ============================================================================
// Sampler Factory Functions
// ============================================================================

namespace SamplerFactory {

inline std::optional<vk::raii::Sampler> createSamplerNearestClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeat(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeatAnisotropic(
        const vk::raii::Device& device, float maxAnisotropy, float maxLod = VK_LOD_CLAMP_NONE) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setAnisotropyEnable(vk::True)
        .setMaxAnisotropy(maxAnisotropy)
        .setMinLod(0.0f)
        .setMaxLod(maxLod);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerShadowComparison(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
        .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
        .setCompareEnable(vk::True)
        .setCompareOp(vk::CompareOp::eLess);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Nearest sampler with mipmap support (for Hi-Z pyramid access)
inline std::optional<vk::raii::Sampler> createSamplerNearestMipmap(
        const vk::raii::Device& device, uint32_t maxMipLevel) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(static_cast<float>(maxMipLevel))
        .setBorderColor(vk::BorderColor::eFloatOpaqueWhite);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Linear sampler with limited mip range (for SSR and similar effects)
inline std::optional<vk::raii::Sampler> createSamplerLinearClampLimitedMip(
        const vk::raii::Device& device, float maxLod = 1.0f) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(maxLod);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Linear sampler with clamp to border (useful for water effects)
inline std::optional<vk::raii::Sampler> createSamplerLinearBorder(
        const vk::raii::Device& device,
        vk::BorderColor borderColor = vk::BorderColor::eFloatTransparentBlack) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
        .setBorderColor(borderColor)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Linear sampler with clamp and anisotropy (for textures that need high quality sampling)
inline std::optional<vk::raii::Sampler> createSamplerLinearClampAnisotropic(
        const vk::raii::Device& device, float maxAnisotropy, float maxLod = VK_LOD_CLAMP_NONE) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setAnisotropyEnable(vk::True)
        .setMaxAnisotropy(maxAnisotropy)
        .setMinLod(0.0f)
        .setMaxLod(maxLod);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Nearest sampler with repeat (for solid color textures and similar)
inline std::optional<vk::raii::Sampler> createSamplerNearestRepeat(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

// Linear sampler with repeat and limited mip range (for simple textures without mipmaps)
inline std::optional<vk::raii::Sampler> createSamplerLinearRepeatLimitedMip(
        const vk::raii::Device& device, float maxLod = 0.0f) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(maxLod);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

} // namespace SamplerFactory
