#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <SDL3/SDL_log.h>

/**
 * SamplerBuilder - Immutable fluent builder for Vulkan samplers
 *
 * This builder uses an immutable pattern where each setter returns a new
 * builder instance. This allows for creating "stereotypes" (predefined
 * configurations) that can be further customized without affecting the original.
 *
 * Example usage:
 *   // Using a stereotype directly
 *   auto sampler = SamplerBuilder::linearRepeat().build(device);
 *
 *   // Customizing a stereotype
 *   auto sampler = SamplerBuilder::linearRepeat()
 *       .maxAnisotropy(16.0f)
 *       .maxLod(10.0f)
 *       .build(device);
 *
 *   // Using stereotypes as templates
 *   static const auto terrainSamplerConfig = SamplerBuilder::linearRepeat()
 *       .maxAnisotropy(16.0f);
 *   // Later, create multiple samplers from the same config
 *   auto sampler1 = terrainSamplerConfig.build(device);
 *   auto sampler2 = terrainSamplerConfig.maxLod(5.0f).build(device);
 */
class SamplerBuilder {
public:
    // Default constructor - creates a basic linear sampler
    constexpr SamplerBuilder() = default;

    // ========================================================================
    // Filter settings (return new builder)
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder magFilter(vk::Filter filter) const {
        SamplerBuilder copy = *this;
        copy.magFilter_ = filter;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder minFilter(vk::Filter filter) const {
        SamplerBuilder copy = *this;
        copy.minFilter_ = filter;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder mipmapMode(vk::SamplerMipmapMode mode) const {
        SamplerBuilder copy = *this;
        copy.mipmapMode_ = mode;
        return copy;
    }

    // Convenience: set both mag and min filter
    [[nodiscard]] constexpr SamplerBuilder filter(vk::Filter filter) const {
        return magFilter(filter).minFilter(filter);
    }

    // ========================================================================
    // Address mode settings
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder addressModeU(vk::SamplerAddressMode mode) const {
        SamplerBuilder copy = *this;
        copy.addressModeU_ = mode;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder addressModeV(vk::SamplerAddressMode mode) const {
        SamplerBuilder copy = *this;
        copy.addressModeV_ = mode;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder addressModeW(vk::SamplerAddressMode mode) const {
        SamplerBuilder copy = *this;
        copy.addressModeW_ = mode;
        return copy;
    }

    // Convenience: set all address modes at once
    [[nodiscard]] constexpr SamplerBuilder addressMode(vk::SamplerAddressMode mode) const {
        return addressModeU(mode).addressModeV(mode).addressModeW(mode);
    }

    // ========================================================================
    // LOD settings
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder minLod(float lod) const {
        SamplerBuilder copy = *this;
        copy.minLod_ = lod;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder maxLod(float lod) const {
        SamplerBuilder copy = *this;
        copy.maxLod_ = lod;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder mipLodBias(float bias) const {
        SamplerBuilder copy = *this;
        copy.mipLodBias_ = bias;
        return copy;
    }

    // ========================================================================
    // Anisotropy settings
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder anisotropyEnable(bool enable) const {
        SamplerBuilder copy = *this;
        copy.anisotropyEnable_ = enable;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder maxAnisotropy(float anisotropy) const {
        SamplerBuilder copy = *this;
        copy.anisotropyEnable_ = true;
        copy.maxAnisotropy_ = anisotropy;
        return copy;
    }

    // ========================================================================
    // Compare settings (for depth/shadow samplers)
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder compareEnable(bool enable) const {
        SamplerBuilder copy = *this;
        copy.compareEnable_ = enable;
        return copy;
    }

    [[nodiscard]] constexpr SamplerBuilder compareOp(vk::CompareOp op) const {
        SamplerBuilder copy = *this;
        copy.compareEnable_ = true;
        copy.compareOp_ = op;
        return copy;
    }

    // ========================================================================
    // Border color (for ClampToBorder address mode)
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder borderColor(vk::BorderColor color) const {
        SamplerBuilder copy = *this;
        copy.borderColor_ = color;
        return copy;
    }

    // ========================================================================
    // Unnormalized coordinates
    // ========================================================================

    [[nodiscard]] constexpr SamplerBuilder unnormalizedCoordinates(bool unnormalized) const {
        SamplerBuilder copy = *this;
        copy.unnormalizedCoordinates_ = unnormalized;
        return copy;
    }

    // ========================================================================
    // Stereotypes - predefined common configurations
    // ========================================================================

    // Nearest filtering with clamp to edge - good for data textures, Hi-Z
    static constexpr SamplerBuilder nearestClamp() {
        return SamplerBuilder()
            .filter(vk::Filter::eNearest)
            .mipmapMode(vk::SamplerMipmapMode::eNearest)
            .addressMode(vk::SamplerAddressMode::eClampToEdge)
            .maxLod(0.0f);
    }

    // Nearest filtering with repeat - good for pixel-art textures
    static constexpr SamplerBuilder nearestRepeat() {
        return SamplerBuilder()
            .filter(vk::Filter::eNearest)
            .mipmapMode(vk::SamplerMipmapMode::eNearest)
            .addressMode(vk::SamplerAddressMode::eRepeat)
            .maxLod(0.0f);
    }

    // Nearest with mipmap support - good for Hi-Z pyramid access
    static constexpr SamplerBuilder nearestMipmap() {
        return SamplerBuilder()
            .filter(vk::Filter::eNearest)
            .mipmapMode(vk::SamplerMipmapMode::eNearest)
            .addressMode(vk::SamplerAddressMode::eClampToEdge)
            .borderColor(vk::BorderColor::eFloatOpaqueWhite)
            .maxLod(VK_LOD_CLAMP_NONE);
    }

    // Linear filtering with clamp - good for post-processing, UI
    static constexpr SamplerBuilder linearClamp() {
        return SamplerBuilder()
            .filter(vk::Filter::eLinear)
            .mipmapMode(vk::SamplerMipmapMode::eLinear)
            .addressMode(vk::SamplerAddressMode::eClampToEdge)
            .maxLod(VK_LOD_CLAMP_NONE);
    }

    // Linear filtering with repeat - good for tiling textures
    static constexpr SamplerBuilder linearRepeat() {
        return SamplerBuilder()
            .filter(vk::Filter::eLinear)
            .mipmapMode(vk::SamplerMipmapMode::eLinear)
            .addressMode(vk::SamplerAddressMode::eRepeat)
            .maxLod(VK_LOD_CLAMP_NONE);
    }

    // Linear with border color - good for water effects, decals
    static constexpr SamplerBuilder linearBorder(vk::BorderColor color = vk::BorderColor::eFloatTransparentBlack) {
        return SamplerBuilder()
            .filter(vk::Filter::eLinear)
            .mipmapMode(vk::SamplerMipmapMode::eLinear)
            .addressMode(vk::SamplerAddressMode::eClampToBorder)
            .borderColor(color)
            .maxLod(VK_LOD_CLAMP_NONE);
    }

    // Shadow comparison sampler - for PCF shadow mapping
    static constexpr SamplerBuilder shadowComparison() {
        return SamplerBuilder()
            .filter(vk::Filter::eLinear)
            .mipmapMode(vk::SamplerMipmapMode::eNearest)
            .addressMode(vk::SamplerAddressMode::eClampToBorder)
            .borderColor(vk::BorderColor::eFloatOpaqueWhite)
            .compareOp(vk::CompareOp::eLess);
    }

    // Anisotropic repeat - good for terrain, world textures
    static constexpr SamplerBuilder anisotropicRepeat(float maxAniso = 16.0f) {
        return linearRepeat().maxAnisotropy(maxAniso);
    }

    // Anisotropic clamp - good for detail textures
    static constexpr SamplerBuilder anisotropicClamp(float maxAniso = 16.0f) {
        return linearClamp().maxAnisotropy(maxAniso);
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] std::optional<vk::raii::Sampler> build(const vk::raii::Device& device) const {
        auto samplerInfo = vk::SamplerCreateInfo{}
            .setMagFilter(magFilter_)
            .setMinFilter(minFilter_)
            .setMipmapMode(mipmapMode_)
            .setAddressModeU(addressModeU_)
            .setAddressModeV(addressModeV_)
            .setAddressModeW(addressModeW_)
            .setMinLod(minLod_)
            .setMaxLod(maxLod_)
            .setMipLodBias(mipLodBias_)
            .setAnisotropyEnable(anisotropyEnable_ ? vk::True : vk::False)
            .setMaxAnisotropy(maxAnisotropy_)
            .setCompareEnable(compareEnable_ ? vk::True : vk::False)
            .setCompareOp(compareOp_)
            .setBorderColor(borderColor_)
            .setUnnormalizedCoordinates(unnormalizedCoordinates_ ? vk::True : vk::False);

        try {
            return vk::raii::Sampler(device, samplerInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SamplerBuilder: Failed to create sampler: %s", e.what());
            return std::nullopt;
        }
    }

    // Build into an optional member (for placement in class members)
    bool buildInto(const vk::raii::Device& device, std::optional<vk::raii::Sampler>& outSampler) const {
        auto result = build(device);
        if (result) {
            outSampler = std::move(result);
            return true;
        }
        return false;
    }

    // ========================================================================
    // Accessors (for inspection)
    // ========================================================================

    [[nodiscard]] constexpr vk::Filter getMagFilter() const { return magFilter_; }
    [[nodiscard]] constexpr vk::Filter getMinFilter() const { return minFilter_; }
    [[nodiscard]] constexpr vk::SamplerMipmapMode getMipmapMode() const { return mipmapMode_; }
    [[nodiscard]] constexpr float getMaxLod() const { return maxLod_; }
    [[nodiscard]] constexpr bool getAnisotropyEnable() const { return anisotropyEnable_; }
    [[nodiscard]] constexpr float getMaxAnisotropy() const { return maxAnisotropy_; }

private:
    vk::Filter magFilter_ = vk::Filter::eLinear;
    vk::Filter minFilter_ = vk::Filter::eLinear;
    vk::SamplerMipmapMode mipmapMode_ = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode addressModeU_ = vk::SamplerAddressMode::eClampToEdge;
    vk::SamplerAddressMode addressModeV_ = vk::SamplerAddressMode::eClampToEdge;
    vk::SamplerAddressMode addressModeW_ = vk::SamplerAddressMode::eClampToEdge;
    float minLod_ = 0.0f;
    float maxLod_ = VK_LOD_CLAMP_NONE;
    float mipLodBias_ = 0.0f;
    bool anisotropyEnable_ = false;
    float maxAnisotropy_ = 1.0f;
    bool compareEnable_ = false;
    vk::CompareOp compareOp_ = vk::CompareOp::eNever;
    vk::BorderColor borderColor_ = vk::BorderColor::eFloatTransparentBlack;
    bool unnormalizedCoordinates_ = false;
};
