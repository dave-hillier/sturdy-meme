#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <functional>
#include <string>
#include <memory>

// Interface for components that need to respond to window resize
// There are two levels of resize handling:
// 1. Full resize - reallocates GPU resources (render targets, buffers)
// 2. Extent update - just updates viewport/scissor dimensions
class IResizable {
public:
    virtual ~IResizable() = default;

    // Called when window is resized - reallocate resources if needed
    // Default implementation does nothing (for extent-only systems)
    virtual void onResize(VkExtent2D newExtent) {
        (void)newExtent;
    }

    // Called after resize to update viewport/scissor dimensions
    virtual void onExtentChanged(VkExtent2D newExtent) = 0;

    // Name for debugging/logging purposes
    virtual const char* getResizableName() const = 0;
};

// Adapter for systems with resize(extent) method
template<typename T>
class ResizeAdapter : public IResizable {
public:
    ResizeAdapter(T& system, const char* name) : system_(system), name_(name) {}

    void onResize(VkExtent2D newExtent) override {
        system_.resize(newExtent);
    }

    void onExtentChanged(VkExtent2D newExtent) override {
        (void)newExtent;  // resize() already handles everything
    }

    const char* getResizableName() const override { return name_; }

private:
    T& system_;
    const char* name_;
};

// Adapter for systems that only need extent updates via setExtent()
template<typename T>
class ExtentAdapter : public IResizable {
public:
    ExtentAdapter(T& system, const char* name) : system_(system), name_(name) {}

    void onExtentChanged(VkExtent2D newExtent) override {
        system_.setExtent(newExtent);
    }

    const char* getResizableName() const override { return name_; }

private:
    T& system_;
    const char* name_;
};

// Resize priority levels - determines order of resize operations
enum class ResizePriority {
    Core = 0,       // Swapchain, depth buffer, framebuffers
    RenderTarget,   // Post-process, bloom, HDR targets
    Culling,        // Hi-Z, water tile cull
    GBuffer,        // G-buffer systems
    Viewport        // Systems that just need extent updates
};

// Coordinates window resize across all rendering subsystems
// Ensures proper ordering and simplifies Renderer code
class ResizeCoordinator {
public:
    ResizeCoordinator() = default;
    ~ResizeCoordinator() = default;

    // Non-copyable, non-movable (owns adapters)
    ResizeCoordinator(const ResizeCoordinator&) = delete;
    ResizeCoordinator& operator=(const ResizeCoordinator&) = delete;

    // Core resize handler type - called before performResize to handle swapchain/depth/framebuffers
    // Returns the new extent (or {0,0} if minimized/failed)
    using CoreResizeHandler = std::function<VkExtent2D(VkDevice, VmaAllocator)>;

    // Set the core resize handler (swapchain, depth buffer, framebuffers)
    void setCoreResizeHandler(CoreResizeHandler handler) { coreResizeHandler_ = std::move(handler); }

    // Register a resizable component with given priority
    void registerResizable(IResizable* resizable, ResizePriority priority);

    // Register systems with resize(extent) method
    template<typename T>
    void registerWithSimpleResize(T& system, const char* name, ResizePriority priority) {
        auto adapter = std::make_unique<ResizeAdapter<T>>(system, name);
        registerResizable(adapter.get(), priority);
        adapters_.push_back(std::move(adapter));
    }

    // Register systems that only need setExtent(extent)
    template<typename T>
    void registerWithExtent(T& system, const char* name, ResizePriority priority = ResizePriority::Viewport) {
        auto adapter = std::make_unique<ExtentAdapter<T>>(system, name);
        registerResizable(adapter.get(), priority);
        adapters_.push_back(std::move(adapter));
    }

    // Register a custom resize callback for systems with non-standard interfaces
    using ResizeCallback = std::function<void(VkExtent2D)>;
    using ExtentCallback = std::function<void(VkExtent2D)>;
    void registerCallback(const char* name, ResizeCallback resizeCb, ExtentCallback extentCb, ResizePriority priority);

    // Perform resize on all registered components
    // Returns false if any component fails to resize
    bool performResize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    // Update extent only (no resource reallocation)
    void updateExtent(VkExtent2D newExtent);

    // Clear all registrations
    void clear();

private:
    struct Registration {
        IResizable* resizable;
        ResizePriority priority;
    };

    // Callback-based resizable for custom handlers
    class CallbackResizable : public IResizable {
    public:
        CallbackResizable(const char* name, ResizeCallback resizeCb, ExtentCallback extentCb)
            : name_(name), resizeCb_(std::move(resizeCb)), extentCb_(std::move(extentCb)) {}

        void onResize(VkExtent2D newExtent) override {
            if (resizeCb_) resizeCb_(newExtent);
        }

        void onExtentChanged(VkExtent2D newExtent) override {
            if (extentCb_) extentCb_(newExtent);
        }

        const char* getResizableName() const override { return name_; }

    private:
        const char* name_;
        ResizeCallback resizeCb_;
        ExtentCallback extentCb_;
    };

    std::vector<Registration> registrations_;
    std::vector<std::unique_ptr<IResizable>> adapters_;  // Owns adapter instances
    CoreResizeHandler coreResizeHandler_;
    bool sorted_ = false;

    void ensureSorted();
};
