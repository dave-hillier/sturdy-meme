#include "ResizeCoordinator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <memory>

void ResizeCoordinator::registerResizable(IResizable* resizable, ResizePriority priority) {
    registrations_.push_back({resizable, priority});
    sorted_ = false;
}

void ResizeCoordinator::registerCallback(const char* name, ResizeCallback resizeCb, ExtentCallback extentCb, ResizePriority priority) {
    auto callback = std::make_unique<CallbackResizable>(name, std::move(resizeCb), std::move(extentCb));
    registerResizable(callback.get(), priority);
    adapters_.push_back(std::move(callback));
}

void ResizeCoordinator::ensureSorted() {
    if (sorted_) return;

    std::stable_sort(registrations_.begin(), registrations_.end(),
        [](const Registration& a, const Registration& b) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        });
    sorted_ = true;
}

bool ResizeCoordinator::performResize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    // If we have a core resize handler and no explicit extent, let it determine the extent
    if (coreResizeHandler_ && newExtent.width == 0 && newExtent.height == 0) {
        // Wait for GPU to finish all work before resizing
        vkDeviceWaitIdle(device);

        newExtent = coreResizeHandler_(device, allocator);

        // Handle minimized window or failure
        if (newExtent.width == 0 || newExtent.height == 0) {
            return true;  // Not an error, just minimized
        }
    }

    ensureSorted();

    for (const auto& reg : registrations_) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "  Resizing: %s", reg.resizable->getResizableName());
        reg.resizable->onResize(device, allocator, newExtent);
    }

    // Also update extents
    updateExtent(newExtent);

    return true;
}

void ResizeCoordinator::updateExtent(VkExtent2D newExtent) {
    ensureSorted();

    for (const auto& reg : registrations_) {
        reg.resizable->onExtentChanged(newExtent);
    }
}

void ResizeCoordinator::clear() {
    registrations_.clear();
    adapters_.clear();
    sorted_ = false;
}
