#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <vector>
#include <SDL3/SDL_log.h>

/**
 * AttachmentBuilder - Immutable builder for render pass attachment descriptions
 *
 * Example:
 *   auto color = AttachmentBuilder::colorAttachment(vk::Format::eB8G8R8A8Srgb);
 *   auto depth = AttachmentBuilder::depthAttachment(vk::Format::eD32Sfloat);
 *   auto shadow = AttachmentBuilder::depthAttachment(vk::Format::eD32Sfloat)
 *       .finalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
 */
class AttachmentBuilder {
public:
    constexpr AttachmentBuilder() = default;

    // ========================================================================
    // Setters (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] constexpr AttachmentBuilder format(vk::Format fmt) const {
        AttachmentBuilder copy = *this;
        copy.format_ = fmt;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder samples(vk::SampleCountFlagBits s) const {
        AttachmentBuilder copy = *this;
        copy.samples_ = s;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder loadOp(vk::AttachmentLoadOp op) const {
        AttachmentBuilder copy = *this;
        copy.loadOp_ = op;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder storeOp(vk::AttachmentStoreOp op) const {
        AttachmentBuilder copy = *this;
        copy.storeOp_ = op;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder stencilLoadOp(vk::AttachmentLoadOp op) const {
        AttachmentBuilder copy = *this;
        copy.stencilLoadOp_ = op;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder stencilStoreOp(vk::AttachmentStoreOp op) const {
        AttachmentBuilder copy = *this;
        copy.stencilStoreOp_ = op;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder initialLayout(vk::ImageLayout layout) const {
        AttachmentBuilder copy = *this;
        copy.initialLayout_ = layout;
        return copy;
    }

    [[nodiscard]] constexpr AttachmentBuilder finalLayout(vk::ImageLayout layout) const {
        AttachmentBuilder copy = *this;
        copy.finalLayout_ = layout;
        return copy;
    }

    // Convenience: set both layouts
    [[nodiscard]] constexpr AttachmentBuilder layouts(vk::ImageLayout initial, vk::ImageLayout final_) const {
        return initialLayout(initial).finalLayout(final_);
    }

    // Convenience: don't clear
    [[nodiscard]] constexpr AttachmentBuilder load() const {
        return loadOp(vk::AttachmentLoadOp::eLoad);
    }

    // Convenience: don't store
    [[nodiscard]] constexpr AttachmentBuilder dontStore() const {
        return storeOp(vk::AttachmentStoreOp::eDontCare);
    }

    // ========================================================================
    // Generic factories - for custom configurations
    // ========================================================================

    // Generic color attachment (clear, store, undefined->color attachment optimal)
    // Use when you need full control via builder methods
    static constexpr AttachmentBuilder color(vk::Format fmt) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::eColorAttachmentOptimal);
    }

    // Generic depth attachment (clear, store, undefined->depth attachment optimal)
    // Use when you need full control via builder methods
    static constexpr AttachmentBuilder depth(vk::Format fmt) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    // ========================================================================
    // Stereotypes - predefined attachment configurations
    // ========================================================================

    // Standard color attachment (clear, store, for presentation)
    static constexpr AttachmentBuilder colorPresent(vk::Format fmt = vk::Format::eB8G8R8A8Srgb) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::ePresentSrcKHR);
    }

    // Color attachment for offscreen rendering (ends in shader read)
    static constexpr AttachmentBuilder colorOffscreen(vk::Format fmt = vk::Format::eR8G8B8A8Unorm) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    // Color attachment starting already in color attachment layout
    static constexpr AttachmentBuilder colorFromAttachment(vk::Format fmt = vk::Format::eR8G8B8A8Unorm) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .finalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    // HDR color attachment
    static constexpr AttachmentBuilder colorHDR(vk::Format fmt = vk::Format::eR16G16B16A16Sfloat) {
        return colorOffscreen(fmt);
    }

    // Depth attachment (transient - don't store)
    static constexpr AttachmentBuilder depthTransient(vk::Format fmt = vk::Format::eD32Sfloat) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eDontCare)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    // Depth attachment (stored - for shadow maps or depth prepass)
    static constexpr AttachmentBuilder depthStored(vk::Format fmt = vk::Format::eD32Sfloat) {
        return AttachmentBuilder()
            .format(fmt)
            .loadOp(vk::AttachmentLoadOp::eClear)
            .storeOp(vk::AttachmentStoreOp::eStore)
            .stencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .stencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .initialLayout(vk::ImageLayout::eUndefined)
            .finalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    // Shadow map depth attachment
    static constexpr AttachmentBuilder shadowMap(vk::Format fmt = vk::Format::eD32Sfloat) {
        return depthStored(fmt);
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] constexpr vk::AttachmentDescription build() const {
        return vk::AttachmentDescription{}
            .setFormat(format_)
            .setSamples(samples_)
            .setLoadOp(loadOp_)
            .setStoreOp(storeOp_)
            .setStencilLoadOp(stencilLoadOp_)
            .setStencilStoreOp(stencilStoreOp_)
            .setInitialLayout(initialLayout_)
            .setFinalLayout(finalLayout_);
    }

    // Implicit conversion
    [[nodiscard]] constexpr operator vk::AttachmentDescription() const {
        return build();
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] constexpr vk::Format getFormat() const { return format_; }
    [[nodiscard]] constexpr vk::ImageLayout getFinalLayout() const { return finalLayout_; }

private:
    vk::Format format_ = vk::Format::eR8G8B8A8Unorm;
    vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1;
    vk::AttachmentLoadOp loadOp_ = vk::AttachmentLoadOp::eClear;
    vk::AttachmentStoreOp storeOp_ = vk::AttachmentStoreOp::eStore;
    vk::AttachmentLoadOp stencilLoadOp_ = vk::AttachmentLoadOp::eDontCare;
    vk::AttachmentStoreOp stencilStoreOp_ = vk::AttachmentStoreOp::eDontCare;
    vk::ImageLayout initialLayout_ = vk::ImageLayout::eUndefined;
    vk::ImageLayout finalLayout_ = vk::ImageLayout::eColorAttachmentOptimal;
};

/**
 * RenderPassBuilder - Immutable builder for Vulkan render passes
 *
 * Supports both simple single-subpass render passes (most common) and
 * multi-subpass configurations.
 *
 * Example usage:
 *   // Simple depth-only pass (shadow maps)
 *   auto shadowPass = RenderPassBuilder::depthOnly(vk::Format::eD32Sfloat).build(device);
 *
 *   // Standard color + depth
 *   auto mainPass = RenderPassBuilder::colorDepth(
 *       vk::Format::eB8G8R8A8Srgb,
 *       vk::Format::eD32Sfloat
 *   ).build(device);
 *
 *   // Customizing a stereotype
 *   auto customPass = RenderPassBuilder::colorDepth(colorFmt, depthFmt)
 *       .colorFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
 *       .build(device);
 *
 *   // Multiple color attachments (G-buffer)
 *   auto gbufferPass = RenderPassBuilder()
 *       .addColorAttachment(AttachmentBuilder::colorOffscreen(vk::Format::eR8G8B8A8Unorm))
 *       .addColorAttachment(AttachmentBuilder::colorOffscreen(vk::Format::eR16G16B16A16Sfloat))
 *       .setDepthAttachment(AttachmentBuilder::depthTransient())
 *       .build(device);
 */
class RenderPassBuilder {
public:
    RenderPassBuilder() = default;

    // ========================================================================
    // Attachment setters (return new builder - immutable)
    // ========================================================================

    // Add a color attachment at the next index
    [[nodiscard]] RenderPassBuilder addColorAttachment(const AttachmentBuilder& attachment) const {
        RenderPassBuilder copy = *this;
        copy.colorAttachments_.push_back(attachment.build());
        return copy;
    }

    // Set the depth attachment
    [[nodiscard]] RenderPassBuilder setDepthAttachment(const AttachmentBuilder& attachment) const {
        RenderPassBuilder copy = *this;
        copy.depthAttachment_ = attachment.build();
        copy.hasDepth_ = true;
        return copy;
    }

    // Clear the depth attachment
    [[nodiscard]] RenderPassBuilder noDepth() const {
        RenderPassBuilder copy = *this;
        copy.hasDepth_ = false;
        return copy;
    }

    // ========================================================================
    // Quick modification helpers for simple cases
    // ========================================================================

    // Change the final layout of the first (or only) color attachment
    [[nodiscard]] RenderPassBuilder colorFinalLayout(vk::ImageLayout layout) const {
        RenderPassBuilder copy = *this;
        if (!copy.colorAttachments_.empty()) {
            copy.colorAttachments_[0].setFinalLayout(layout);
        }
        return copy;
    }

    // Change the final layout of the depth attachment
    [[nodiscard]] RenderPassBuilder depthFinalLayout(vk::ImageLayout layout) const {
        RenderPassBuilder copy = *this;
        if (copy.hasDepth_) {
            copy.depthAttachment_.setFinalLayout(layout);
        }
        return copy;
    }

    // Store depth (for later sampling)
    [[nodiscard]] RenderPassBuilder storeDepth(bool store = true) const {
        RenderPassBuilder copy = *this;
        if (copy.hasDepth_) {
            copy.depthAttachment_.setStoreOp(store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare);
        }
        return copy;
    }

    // ========================================================================
    // Stereotypes - common render pass configurations
    // ========================================================================

    // Depth-only render pass (shadow maps)
    static RenderPassBuilder depthOnly(vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return RenderPassBuilder()
            .setDepthAttachment(AttachmentBuilder::shadowMap(depthFormat));
    }

    // Standard color + depth for presentation
    static RenderPassBuilder colorDepthPresent(
            vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb,
            vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return RenderPassBuilder()
            .addColorAttachment(AttachmentBuilder::colorPresent(colorFormat))
            .setDepthAttachment(AttachmentBuilder::depthTransient(depthFormat));
    }

    // Color + depth for offscreen rendering
    static RenderPassBuilder colorDepthOffscreen(
            vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm,
            vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return RenderPassBuilder()
            .addColorAttachment(AttachmentBuilder::colorOffscreen(colorFormat))
            .setDepthAttachment(AttachmentBuilder::depthTransient(depthFormat));
    }

    // Color + stored depth (for depth sampling later)
    static RenderPassBuilder colorDepthStored(
            vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm,
            vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return RenderPassBuilder()
            .addColorAttachment(AttachmentBuilder::colorOffscreen(colorFormat))
            .setDepthAttachment(AttachmentBuilder::depthStored(depthFormat));
    }

    // HDR render target
    static RenderPassBuilder hdrColorDepth(
            vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat,
            vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return colorDepthOffscreen(colorFormat, depthFormat);
    }

    // Two color attachments + depth (common for G-buffer first pass)
    static RenderPassBuilder twoColorDepth(
            vk::Format color0Format,
            vk::Format color1Format,
            vk::Format depthFormat = vk::Format::eD32Sfloat) {
        return RenderPassBuilder()
            .addColorAttachment(AttachmentBuilder::colorOffscreen(color0Format))
            .addColorAttachment(AttachmentBuilder::colorOffscreen(color1Format))
            .setDepthAttachment(AttachmentBuilder::depthTransient(depthFormat));
    }

    // Color-only (no depth) - postprocessing
    static RenderPassBuilder colorOnly(vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm) {
        return RenderPassBuilder()
            .addColorAttachment(AttachmentBuilder::colorOffscreen(colorFormat));
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] std::optional<vk::raii::RenderPass> build(const vk::raii::Device& device) const {
        // Combine all attachments
        std::vector<vk::AttachmentDescription> attachments;
        attachments.reserve(colorAttachments_.size() + (hasDepth_ ? 1 : 0));

        for (const auto& color : colorAttachments_) {
            attachments.push_back(color);
        }

        uint32_t depthIndex = static_cast<uint32_t>(attachments.size());
        if (hasDepth_) {
            attachments.push_back(depthAttachment_);
        }

        // Create color attachment references
        std::vector<vk::AttachmentReference> colorRefs;
        colorRefs.reserve(colorAttachments_.size());
        for (uint32_t i = 0; i < colorAttachments_.size(); ++i) {
            colorRefs.push_back(vk::AttachmentReference{}
                .setAttachment(i)
                .setLayout(vk::ImageLayout::eColorAttachmentOptimal));
        }

        // Create depth attachment reference
        vk::AttachmentReference depthRef;
        if (hasDepth_) {
            depthRef = vk::AttachmentReference{}
                .setAttachment(depthIndex)
                .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        }

        // Create subpass
        auto subpass = vk::SubpassDescription{}
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);

        if (!colorRefs.empty()) {
            subpass.setColorAttachments(colorRefs);
        }
        if (hasDepth_) {
            subpass.setPDepthStencilAttachment(&depthRef);
        }

        // Create dependency
        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::AccessFlags srcAccess = {};
        vk::AccessFlags dstAccess = vk::AccessFlagBits::eColorAttachmentWrite;

        if (hasDepth_) {
            srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
            dstAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        }

        // For depth-only passes, adjust stages
        if (colorAttachments_.empty() && hasDepth_) {
            srcStage = vk::PipelineStageFlagBits::eFragmentShader;
            srcAccess = vk::AccessFlagBits::eShaderRead;
            dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
            dstAccess = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        }

        auto dependency = vk::SubpassDependency{}
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(srcStage)
            .setSrcAccessMask(srcAccess)
            .setDstStageMask(dstStage)
            .setDstAccessMask(dstAccess);

        auto renderPassInfo = vk::RenderPassCreateInfo{}
            .setAttachments(attachments)
            .setSubpasses(subpass)
            .setDependencies(dependency);

        try {
            return vk::raii::RenderPass(device, renderPassInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "RenderPassBuilder: Failed to create render pass: %s", e.what());
            return std::nullopt;
        }
    }

    // Build into an optional member
    bool buildInto(const vk::raii::Device& device, std::optional<vk::raii::RenderPass>& outRenderPass) const {
        auto result = build(device);
        if (result) {
            outRenderPass = std::move(result);
            return true;
        }
        return false;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] size_t getColorAttachmentCount() const { return colorAttachments_.size(); }
    [[nodiscard]] bool hasDepthAttachment() const { return hasDepth_; }

private:
    std::vector<vk::AttachmentDescription> colorAttachments_;
    vk::AttachmentDescription depthAttachment_;
    bool hasDepth_ = false;
};
