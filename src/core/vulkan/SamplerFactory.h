#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <SDL3/SDL_log.h>

// ============================================================================
// Sampler Builder - Fluent API for creating samplers
// ============================================================================

class SamplerBuilder {
public:
    explicit SamplerBuilder(const vk::raii::Device& device)
        : device_(&device) {}

    // Filter settings
    SamplerBuilder& nearest() {
        info_.setMagFilter(vk::Filter::eNearest)
             .setMinFilter(vk::Filter::eNearest)
             .setMipmapMode(vk::SamplerMipmapMode::eNearest);
        return *this;
    }

    SamplerBuilder& linear() {
        info_.setMagFilter(vk::Filter::eLinear)
             .setMinFilter(vk::Filter::eLinear)
             .setMipmapMode(vk::SamplerMipmapMode::eLinear);
        return *this;
    }

    // Address mode settings
    SamplerBuilder& clamp() {
        info_.setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
             .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
             .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
        return *this;
    }

    SamplerBuilder& repeat() {
        info_.setAddressModeU(vk::SamplerAddressMode::eRepeat)
             .setAddressModeV(vk::SamplerAddressMode::eRepeat)
             .setAddressModeW(vk::SamplerAddressMode::eRepeat);
        return *this;
    }

    SamplerBuilder& border(vk::BorderColor color = vk::BorderColor::eFloatTransparentBlack) {
        info_.setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
             .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
             .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
             .setBorderColor(color);
        return *this;
    }

    // LOD settings
    SamplerBuilder& noMip() {
        info_.setMinLod(0.0f).setMaxLod(0.0f);
        return *this;
    }

    SamplerBuilder& mipmap(float maxLod = VK_LOD_CLAMP_NONE) {
        info_.setMinLod(0.0f).setMaxLod(maxLod);
        return *this;
    }

    SamplerBuilder& mipLevels(uint32_t maxLevel) {
        info_.setMinLod(0.0f).setMaxLod(static_cast<float>(maxLevel));
        return *this;
    }

    // Anisotropy
    SamplerBuilder& anisotropy(float maxAnisotropy) {
        info_.setAnisotropyEnable(vk::True)
             .setMaxAnisotropy(maxAnisotropy);
        return *this;
    }

    // Shadow comparison
    SamplerBuilder& compareOp(vk::CompareOp op) {
        info_.setCompareEnable(vk::True)
             .setCompareOp(op);
        return *this;
    }

    // Build the sampler
    std::optional<vk::raii::Sampler> build() const {
        try {
            return vk::raii::Sampler(*device_, info_);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
            return std::nullopt;
        }
    }

private:
    const vk::raii::Device* device_;
    vk::SamplerCreateInfo info_{};
};

// ============================================================================
// Sampler Factory - Convenience functions for common sampler configurations
// ============================================================================

namespace SamplerFactory {

inline std::optional<vk::raii::Sampler> createSamplerNearestClamp(const vk::raii::Device& device) {
    return SamplerBuilder(device).nearest().clamp().noMip().build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearClamp(const vk::raii::Device& device) {
    return SamplerBuilder(device).linear().clamp().mipmap().build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeat(const vk::raii::Device& device) {
    return SamplerBuilder(device).linear().repeat().mipmap().build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeatAnisotropic(
        const vk::raii::Device& device, float maxAnisotropy, float maxLod = VK_LOD_CLAMP_NONE) {
    return SamplerBuilder(device).linear().repeat().mipmap(maxLod).anisotropy(maxAnisotropy).build();
}

inline std::optional<vk::raii::Sampler> createSamplerShadowComparison(const vk::raii::Device& device) {
    return SamplerBuilder(device)
        .linear()
        .border(vk::BorderColor::eFloatOpaqueWhite)
        .compareOp(vk::CompareOp::eLess)
        .build();
}

inline std::optional<vk::raii::Sampler> createSamplerNearestMipmap(
        const vk::raii::Device& device, uint32_t maxMipLevel) {
    return SamplerBuilder(device).nearest().clamp().mipLevels(maxMipLevel).build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearClampLimitedMip(
        const vk::raii::Device& device, float maxLod = 1.0f) {
    return SamplerBuilder(device).linear().clamp().mipmap(maxLod).build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearBorder(
        const vk::raii::Device& device,
        vk::BorderColor borderColor = vk::BorderColor::eFloatTransparentBlack) {
    return SamplerBuilder(device).linear().border(borderColor).mipmap().build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearClampAnisotropic(
        const vk::raii::Device& device, float maxAnisotropy, float maxLod = VK_LOD_CLAMP_NONE) {
    return SamplerBuilder(device).linear().clamp().mipmap(maxLod).anisotropy(maxAnisotropy).build();
}

inline std::optional<vk::raii::Sampler> createSamplerNearestRepeat(const vk::raii::Device& device) {
    return SamplerBuilder(device).nearest().repeat().noMip().build();
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeatLimitedMip(
        const vk::raii::Device& device, float maxLod = 0.0f) {
    return SamplerBuilder(device).linear().repeat().mipmap(maxLod).build();
}

} // namespace SamplerFactory
