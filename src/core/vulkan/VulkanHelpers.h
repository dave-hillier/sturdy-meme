#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <SDL3/SDL_log.h>
#include <utility>
#include <functional>
#include <vector>

// ============================================================================
// VK_CHECK - Error checking macros for Vulkan calls
// ============================================================================

#define VK_CHECK(result) \
    do { \
        VkResult res_ = (result); \
        if (res_ != VK_SUCCESS) { \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, \
                "Vulkan error %d at %s:%d", res_, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

#define VK_CHECK_VOID(result) \
    do { \
        VkResult res_ = (result); \
        if (res_ != VK_SUCCESS) { \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, \
                "Vulkan error %d at %s:%d", res_, __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

// ============================================================================
// ScopeGuard - RAII cleanup helper for exception-safe resource management
// ============================================================================

template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F func) : cleanup_(std::move(func)), active_(true) {}

    ~ScopeGuard() {
        if (active_) {
            cleanup_();
        }
    }

    void dismiss() { active_ = false; }

    ScopeGuard(ScopeGuard&& other) noexcept
        : cleanup_(std::move(other.cleanup_)), active_(other.active_) {
        other.active_ = false;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    F cleanup_;
    bool active_;
};

template<typename F>
ScopeGuard<F> makeScopeGuard(F func) {
    return ScopeGuard<F>(std::move(func));
}

// ============================================================================
// CommandScope - RAII wrapper for one-time command buffer submission
// ============================================================================

class CommandScope {
public:
    CommandScope(vk::Device device, vk::CommandPool commandPool, vk::Queue queue)
        : device_(device), commandPool_(commandPool), queue_(queue) {}

    ~CommandScope() {
        if (commandBuffer_) {
            device_.freeCommandBuffers(commandPool_, commandBuffer_);
        }
    }

    CommandScope(const CommandScope&) = delete;
    CommandScope& operator=(const CommandScope&) = delete;
    CommandScope(CommandScope&&) = delete;
    CommandScope& operator=(CommandScope&&) = delete;

    bool begin() {
        auto allocInfo = vk::CommandBufferAllocateInfo{}
            .setCommandPool(commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);

        try {
            auto buffers = device_.allocateCommandBuffers(allocInfo);
            commandBuffer_ = buffers[0];
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to allocate command buffer: %s", e.what());
            return false;
        }

        auto beginInfo = vk::CommandBufferBeginInfo{}
            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        try {
            commandBuffer_.begin(beginInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to begin command buffer: %s", e.what());
            return false;
        }

        return true;
    }

    bool end() {
        try {
            commandBuffer_.end();
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to end command buffer: %s", e.what());
            return false;
        }

        auto fenceInfo = vk::FenceCreateInfo{};
        vk::Fence fence;

        try {
            fence = device_.createFence(fenceInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to create fence: %s", e.what());
            return false;
        }

        auto submitInfo = vk::SubmitInfo{}
            .setCommandBufferCount(1)
            .setPCommandBuffers(&commandBuffer_);

        try {
            queue_.submit(submitInfo, fence);
            auto result = device_.waitForFences(fence, vk::True, UINT64_MAX);
            device_.destroyFence(fence);
            if (result != vk::Result::eSuccess) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to wait for fence");
                return false;
            }
        } catch (const vk::SystemError& e) {
            device_.destroyFence(fence);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Submit failed: %s", e.what());
            return false;
        }

        return true;
    }

    vk::CommandBuffer get() const { return commandBuffer_; }

    // For compatibility with C API
    VkCommandBuffer getHandle() const { return static_cast<VkCommandBuffer>(commandBuffer_); }

private:
    vk::Device device_;
    vk::CommandPool commandPool_;
    vk::Queue queue_;
    vk::CommandBuffer commandBuffer_;
};

// ============================================================================
// RenderPassScope - RAII wrapper for render pass begin/end
// ============================================================================

class RenderPassScope {
public:
    class Builder {
    public:
        explicit Builder(vk::CommandBuffer cmd) : cmd_(cmd) {}

        Builder& renderPass(vk::RenderPass rp) {
            beginInfo_.setRenderPass(rp);
            return *this;
        }

        Builder& framebuffer(vk::Framebuffer fb) {
            beginInfo_.setFramebuffer(fb);
            return *this;
        }

        Builder& renderArea(int32_t x, int32_t y, uint32_t width, uint32_t height) {
            beginInfo_.setRenderArea(vk::Rect2D{{x, y}, {width, height}});
            return *this;
        }

        Builder& renderArea(vk::Rect2D area) {
            beginInfo_.setRenderArea(area);
            return *this;
        }

        Builder& renderAreaFullExtent(uint32_t width, uint32_t height) {
            beginInfo_.setRenderArea(vk::Rect2D{{0, 0}, {width, height}});
            return *this;
        }

        Builder& clearColor(float r, float g, float b, float a) {
            vk::ClearValue clear;
            clear.color = vk::ClearColorValue{std::array<float, 4>{r, g, b, a}};
            clearValues_.push_back(clear);
            return *this;
        }

        Builder& clearDepth(float depth, uint32_t stencil) {
            vk::ClearValue clear;
            clear.depthStencil = vk::ClearDepthStencilValue{depth, stencil};
            clearValues_.push_back(clear);
            return *this;
        }

        Builder& clearValues(const vk::ClearValue* values, uint32_t count) {
            clearValues_.assign(values, values + count);
            return *this;
        }

        Builder& subpassContents(vk::SubpassContents contents) {
            contents_ = contents;
            return *this;
        }

        operator RenderPassScope() {
            beginInfo_.setClearValueCount(static_cast<uint32_t>(clearValues_.size()));
            beginInfo_.setPClearValues(clearValues_.empty() ? nullptr : clearValues_.data());
            return RenderPassScope(cmd_, beginInfo_, contents_);
        }

    private:
        vk::CommandBuffer cmd_;
        vk::RenderPassBeginInfo beginInfo_;
        std::vector<vk::ClearValue> clearValues_;
        vk::SubpassContents contents_ = vk::SubpassContents::eInline;
    };

    RenderPassScope(vk::CommandBuffer cmd, const vk::RenderPassBeginInfo& beginInfo,
                    vk::SubpassContents contents = vk::SubpassContents::eInline)
        : cmd_(cmd) {
        cmd_.beginRenderPass(beginInfo, contents);
    }

    ~RenderPassScope() {
        if (cmd_) {
            cmd_.endRenderPass();
        }
    }

    RenderPassScope(const RenderPassScope&) = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;

    RenderPassScope(RenderPassScope&& other) noexcept : cmd_(other.cmd_) {
        other.cmd_ = nullptr;
    }

    RenderPassScope& operator=(RenderPassScope&& other) noexcept {
        if (this != &other) {
            if (cmd_) {
                cmd_.endRenderPass();
            }
            cmd_ = other.cmd_;
            other.cmd_ = nullptr;
        }
        return *this;
    }

    static Builder begin(vk::CommandBuffer cmd) {
        return Builder(cmd);
    }

    vk::CommandBuffer cmd() const { return cmd_; }

private:
    vk::CommandBuffer cmd_;
};

// ============================================================================
// Sampler Factory Functions
// ============================================================================

namespace SamplerFactory {

inline vk::raii::Sampler createNearestClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    return vk::raii::Sampler(device, samplerInfo);
}

inline vk::raii::Sampler createLinearClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    return vk::raii::Sampler(device, samplerInfo);
}

inline vk::raii::Sampler createLinearRepeat(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    return vk::raii::Sampler(device, samplerInfo);
}

inline vk::raii::Sampler createLinearRepeatAnisotropic(const vk::raii::Device& device, float maxAnisotropy) {
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
        .setMaxLod(VK_LOD_CLAMP_NONE);

    return vk::raii::Sampler(device, samplerInfo);
}

inline vk::raii::Sampler createShadowComparison(const vk::raii::Device& device) {
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

    return vk::raii::Sampler(device, samplerInfo);
}

// Legacy-compatible versions that use VkDevice and output to ManagedSampler
// These will be removed once all code is migrated to vk::raii::Sampler

} // namespace SamplerFactory
