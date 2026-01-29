#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <vector>
#include <SDL3/SDL_log.h>

/**
 * FramebufferBuilder - Immutable builder for Vulkan framebuffers
 *
 * This builder uses an immutable pattern where each setter returns a new
 * builder instance. This allows for creating "stereotypes" that can be reused.
 *
 * Example usage:
 *   // Basic framebuffer
 *   auto fb = FramebufferBuilder()
 *       .renderPass(myRenderPass)
 *       .extent(1920, 1080)
 *       .addAttachment(colorView)
 *       .addAttachment(depthView)
 *       .build(device);
 *
 *   // Using a stereotype for shadow maps
 *   static const auto shadowFbTemplate = FramebufferBuilder()
 *       .extent(2048, 2048)
 *       .layers(1);
 *
 *   // Create multiple shadow framebuffers from template
 *   auto fb1 = shadowFbTemplate.renderPass(shadowPass).addAttachment(depthView1).build(device);
 *   auto fb2 = shadowFbTemplate.renderPass(shadowPass).addAttachment(depthView2).build(device);
 */
class FramebufferBuilder {
public:
    FramebufferBuilder() = default;

    // ========================================================================
    // Required settings (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] FramebufferBuilder renderPass(vk::RenderPass pass) const {
        FramebufferBuilder copy = *this;
        copy.renderPass_ = pass;
        return copy;
    }

    [[nodiscard]] FramebufferBuilder renderPass(const vk::raii::RenderPass& pass) const {
        return renderPass(*pass);
    }

    // ========================================================================
    // Extent settings
    // ========================================================================

    [[nodiscard]] FramebufferBuilder extent(uint32_t width, uint32_t height) const {
        FramebufferBuilder copy = *this;
        copy.width_ = width;
        copy.height_ = height;
        return copy;
    }

    [[nodiscard]] FramebufferBuilder extent(vk::Extent2D ext) const {
        return extent(ext.width, ext.height);
    }

    [[nodiscard]] FramebufferBuilder width(uint32_t w) const {
        FramebufferBuilder copy = *this;
        copy.width_ = w;
        return copy;
    }

    [[nodiscard]] FramebufferBuilder height(uint32_t h) const {
        FramebufferBuilder copy = *this;
        copy.height_ = h;
        return copy;
    }

    [[nodiscard]] FramebufferBuilder layers(uint32_t l) const {
        FramebufferBuilder copy = *this;
        copy.layers_ = l;
        return copy;
    }

    // ========================================================================
    // Attachment management
    // ========================================================================

    // Add an attachment view
    [[nodiscard]] FramebufferBuilder addAttachment(vk::ImageView view) const {
        FramebufferBuilder copy = *this;
        copy.attachments_.push_back(view);
        return copy;
    }

    [[nodiscard]] FramebufferBuilder addAttachment(const vk::raii::ImageView& view) const {
        return addAttachment(*view);
    }

    // Add attachment from optional (common pattern)
    [[nodiscard]] FramebufferBuilder addAttachment(const std::optional<vk::raii::ImageView>& view) const {
        if (view) {
            return addAttachment(*view);
        }
        return *this;
    }

    // Set all attachments at once (replaces existing)
    [[nodiscard]] FramebufferBuilder attachments(const std::vector<vk::ImageView>& views) const {
        FramebufferBuilder copy = *this;
        copy.attachments_ = views;
        return copy;
    }

    // Clear all attachments
    [[nodiscard]] FramebufferBuilder clearAttachments() const {
        FramebufferBuilder copy = *this;
        copy.attachments_.clear();
        return copy;
    }

    // ========================================================================
    // Stereotypes - common framebuffer configurations
    // ========================================================================

    // Shadow map framebuffer template (depth-only, square)
    static FramebufferBuilder shadowMap(uint32_t size = 2048) {
        return FramebufferBuilder()
            .extent(size, size)
            .layers(1);
    }

    // Offscreen render target template
    static FramebufferBuilder offscreen(uint32_t width, uint32_t height) {
        return FramebufferBuilder()
            .extent(width, height)
            .layers(1);
    }

    // Cube map face template
    static FramebufferBuilder cubeFace(uint32_t size = 512) {
        return FramebufferBuilder()
            .extent(size, size)
            .layers(1);
    }

    // ========================================================================
    // Build methods
    // ========================================================================

    [[nodiscard]] std::optional<vk::raii::Framebuffer> build(const vk::raii::Device& device) const {
        if (renderPass_ == vk::RenderPass{}) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FramebufferBuilder: render pass not set");
            return std::nullopt;
        }

        auto framebufferInfo = vk::FramebufferCreateInfo{}
            .setRenderPass(renderPass_)
            .setAttachments(attachments_)
            .setWidth(width_)
            .setHeight(height_)
            .setLayers(layers_);

        try {
            return vk::raii::Framebuffer(device, framebufferInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "FramebufferBuilder: Failed to create framebuffer: %s", e.what());
            return std::nullopt;
        }
    }

    // Build into an optional member
    bool buildInto(const vk::raii::Device& device, std::optional<vk::raii::Framebuffer>& outFramebuffer) const {
        auto result = build(device);
        if (result) {
            outFramebuffer = std::move(result);
            return true;
        }
        return false;
    }

    // Build multiple framebuffers from a list of attachment sets
    // Useful for swapchain framebuffers
    [[nodiscard]] std::optional<std::vector<vk::raii::Framebuffer>> buildMultiple(
            const vk::raii::Device& device,
            const std::vector<vk::ImageView>& perFrameAttachments) const {

        std::vector<vk::raii::Framebuffer> framebuffers;
        framebuffers.reserve(perFrameAttachments.size());

        for (const auto& view : perFrameAttachments) {
            auto fb = clearAttachments().addAttachment(view);
            // Add shared attachments (like depth) after per-frame attachment
            for (const auto& sharedView : attachments_) {
                fb = fb.addAttachment(sharedView);
            }
            auto result = fb.build(device);
            if (!result) {
                return std::nullopt;
            }
            framebuffers.push_back(std::move(*result));
        }

        return framebuffers;
    }

    // Build multiple with color + depth pattern
    [[nodiscard]] std::optional<std::vector<vk::raii::Framebuffer>> buildSwapchain(
            const vk::raii::Device& device,
            const std::vector<vk::ImageView>& colorViews,
            vk::ImageView depthView) const {

        std::vector<vk::raii::Framebuffer> framebuffers;
        framebuffers.reserve(colorViews.size());

        for (const auto& colorView : colorViews) {
            auto fb = clearAttachments()
                .addAttachment(colorView)
                .addAttachment(depthView)
                .build(device);
            if (!fb) {
                return std::nullopt;
            }
            framebuffers.push_back(std::move(*fb));
        }

        return framebuffers;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] uint32_t getWidth() const { return width_; }
    [[nodiscard]] uint32_t getHeight() const { return height_; }
    [[nodiscard]] uint32_t getLayers() const { return layers_; }
    [[nodiscard]] size_t getAttachmentCount() const { return attachments_.size(); }
    [[nodiscard]] vk::Extent2D getExtent() const { return {width_, height_}; }

private:
    vk::RenderPass renderPass_ = {};
    std::vector<vk::ImageView> attachments_;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint32_t layers_ = 1;
};
