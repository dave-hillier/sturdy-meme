#include "FrameExecutor.h"
#include "VulkanContext.h"
#include "QueueSubmitDiagnostics.h"
#include "Profiler.h"
#include <SDL3/SDL.h>
#include <chrono>

bool FrameExecutor::init(const InitParams& params) {
    if (!params.vulkanContext || !params.frameSync) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FrameExecutor::init: missing required parameters");
        return false;
    }

    vulkanContext_ = params.vulkanContext;
    frameSync_ = params.frameSync;

    SDL_Log("FrameExecutor initialized");
    return true;
}

void FrameExecutor::destroy() {
    vulkanContext_ = nullptr;
    frameSync_ = nullptr;
}

// =============================================================================
// High-level frame execution
// =============================================================================

FrameResult FrameExecutor::execute(const FrameBuilder& builder,
                                   QueueSubmitDiagnostics* diagnostics,
                                   Profiler* profiler) {
    // --- Phase 1: Frame synchronization and swapchain acquire ---

    // Skip if window is suspended
    if (windowSuspended_) {
        return FrameResult::Skipped;
    }

    // Handle pending resize
    if (resizeNeeded_) {
        return FrameResult::SwapchainOutOfDate;
    }

    // Skip if window is minimized
    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        return FrameResult::Skipped;
    }

    // Frame synchronization (wait for this frame slot to be available)
    if (profiler) profiler->beginCpuZone("Wait:FenceSync");
    if (diagnostics) diagnostics->fenceWasAlreadySignaled = frameSync_->isCurrentFenceSignaled();
    auto fenceStart = std::chrono::high_resolution_clock::now();
    frameSync_->waitForCurrentFrameIfNeeded();
    auto fenceEnd = std::chrono::high_resolution_clock::now();
    if (diagnostics) {
        diagnostics->fenceWaitTimeMs =
            std::chrono::duration<float, std::milli>(fenceEnd - fenceStart).count();
    }
    if (profiler) profiler->endCpuZone("Wait:FenceSync");

    // Acquire swapchain image
    if (profiler) profiler->beginCpuZone("Wait:AcquireImage");
    auto acquireStart = std::chrono::high_resolution_clock::now();

    auto acquireResult = acquireSwapchainImage();

    auto acquireEnd = std::chrono::high_resolution_clock::now();
    if (diagnostics) {
        diagnostics->acquireImageTimeMs =
            std::chrono::duration<float, std::milli>(acquireEnd - acquireStart).count();
    }
    if (profiler) profiler->endCpuZone("Wait:AcquireImage");

    if (!acquireResult.success) {
        return acquireResult.error;
    }

    // --- Phase 2: Build frame (caller's responsibility) ---

    FrameBuildContext ctx;
    ctx.imageIndex = acquireResult.imageIndex;
    ctx.frameIndex = frameSync_->currentIndex();

    auto buildResult = builder(ctx);
    if (!buildResult) {
        return FrameResult::Skipped;  // Builder decided to skip this frame
    }

    // --- Phase 3: Submit and present ---

    if (profiler) profiler->beginCpuZone("QueueSubmit");
    FrameResult submitResult = submitCommandBuffer(buildResult->commandBuffer, diagnostics);
    if (submitResult != FrameResult::Success) {
        if (profiler) profiler->endCpuZone("QueueSubmit");
        return submitResult;
    }

    FrameResult presentResult = present(acquireResult.imageIndex, diagnostics);
    if (profiler) profiler->endCpuZone("QueueSubmit");

    return presentResult;
}

// =============================================================================
// Low-level frame phases
// =============================================================================

FrameExecutor::FrameBeginResult FrameExecutor::beginFrame() {
    FrameBeginResult result;

    if (windowSuspended_) {
        result.error = FrameResult::Skipped;
        return result;
    }

    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        result.error = FrameResult::Skipped;
        return result;
    }

    frameSync_->waitForCurrentFrameIfNeeded();
    return acquireSwapchainImage();
}

FrameExecutor::FrameBeginResult FrameExecutor::beginFrame(QueueSubmitDiagnostics& diagnostics, Profiler& profiler) {
    FrameBeginResult result;

    if (windowSuspended_) {
        result.error = FrameResult::Skipped;
        return result;
    }

    if (resizeNeeded_) {
        result.error = FrameResult::SwapchainOutOfDate;
        return result;
    }

    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        result.error = FrameResult::Skipped;
        return result;
    }

    // Frame synchronization with diagnostics
    profiler.beginCpuZone("Wait:FenceSync");
    diagnostics.fenceWasAlreadySignaled = frameSync_->isCurrentFenceSignaled();
    auto fenceStart = std::chrono::high_resolution_clock::now();
    frameSync_->waitForCurrentFrameIfNeeded();
    auto fenceEnd = std::chrono::high_resolution_clock::now();
    diagnostics.fenceWaitTimeMs = std::chrono::duration<float, std::milli>(fenceEnd - fenceStart).count();
    profiler.endCpuZone("Wait:FenceSync");

    // Acquire swapchain image with diagnostics
    profiler.beginCpuZone("Wait:AcquireImage");
    auto acquireStart = std::chrono::high_resolution_clock::now();

    VkDevice device = vulkanContext_->getVkDevice();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    uint32_t imageIndex;
    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms
    VkResult vkResult = vkAcquireNextImageKHR(
        device, swapchain, acquireTimeoutNs,
        frameSync_->currentImageAvailableSemaphore(),
        VK_NULL_HANDLE, &imageIndex);

    auto acquireEnd = std::chrono::high_resolution_clock::now();
    diagnostics.acquireImageTimeMs = std::chrono::duration<float, std::milli>(acquireEnd - acquireStart).count();
    profiler.endCpuZone("Wait:AcquireImage");

    if (vkResult == VK_TIMEOUT || vkResult == VK_NOT_READY) {
        result.error = FrameResult::Skipped;
        return result;
    } else if (vkResult == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeNeeded_ = true;
        result.error = FrameResult::SwapchainOutOfDate;
        return result;
    } else if (vkResult == VK_ERROR_SURFACE_LOST_KHR) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface lost, will recreate on next frame");
        resizeNeeded_ = true;
        result.error = FrameResult::SurfaceLost;
        return result;
    } else if (vkResult == VK_ERROR_DEVICE_LOST) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan device lost - attempting recovery");
        resizeNeeded_ = true;
        result.error = FrameResult::DeviceLost;
        return result;
    } else if (vkResult != VK_SUCCESS && vkResult != VK_SUBOPTIMAL_KHR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain image: %d", vkResult);
        result.error = FrameResult::AcquireFailed;
        return result;
    }

    frameSync_->resetCurrentFence();
    resizeNeeded_ = false;

    currentImageIndex_ = imageIndex;
    result.success = true;
    result.imageIndex = imageIndex;
    return result;
}

FrameExecutor::FrameBeginResult FrameExecutor::acquireSwapchainImage() {
    FrameBeginResult result;

    VkDevice device = vulkanContext_->getVkDevice();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    uint32_t imageIndex;
    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms in nanoseconds
    VkResult vkResult = vkAcquireNextImageKHR(
        device, swapchain, acquireTimeoutNs,
        frameSync_->currentImageAvailableSemaphore(),
        VK_NULL_HANDLE, &imageIndex);

    if (vkResult == VK_TIMEOUT || vkResult == VK_NOT_READY) {
        result.error = FrameResult::Skipped;
        return result;
    } else if (vkResult == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeNeeded_ = true;
        result.error = FrameResult::SwapchainOutOfDate;
        return result;
    } else if (vkResult == VK_ERROR_SURFACE_LOST_KHR) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface lost, will recreate on next frame");
        resizeNeeded_ = true;
        result.error = FrameResult::SurfaceLost;
        return result;
    } else if (vkResult == VK_ERROR_DEVICE_LOST) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan device lost - attempting recovery");
        resizeNeeded_ = true;
        result.error = FrameResult::DeviceLost;
        return result;
    } else if (vkResult != VK_SUCCESS && vkResult != VK_SUBOPTIMAL_KHR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain image: %d", vkResult);
        result.error = FrameResult::AcquireFailed;
        return result;
    }

    frameSync_->resetCurrentFence();

    currentImageIndex_ = imageIndex;
    result.success = true;
    result.imageIndex = imageIndex;
    return result;
}

FrameResult FrameExecutor::submitCommandBuffer(VkCommandBuffer cmd, QueueSubmitDiagnostics* diagnostics) {
    VkQueue graphicsQueue = vulkanContext_->getVkGraphicsQueue();

    vk::CommandBuffer vkCmd(cmd);

    // Binary semaphores for swapchain synchronization
    vk::Semaphore waitSemaphores[] = {frameSync_->currentImageAvailableSemaphore()};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    // Signal both render finished (binary, for present) and timeline (for frame sync)
    vk::Semaphore signalSemaphores[] = {
        frameSync_->currentRenderFinishedSemaphore(),
        frameSync_->frameTimelineSemaphore()
    };

    // Get next timeline value to signal for this frame
    uint64_t timelineSignalValue = frameSync_->nextFrameSignalValue();

    // Timeline semaphore submit info (Vulkan 1.2)
    uint64_t waitValues[] = {0};  // Binary semaphore, value ignored
    uint64_t signalValues[] = {0, timelineSignalValue};  // Binary, then timeline

    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(1)
        .setPWaitSemaphoreValues(waitValues)
        .setSignalSemaphoreValueCount(2)
        .setPSignalSemaphoreValues(signalValues);

    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(vkCmd)
        .setSignalSemaphores(signalSemaphores);

    try {
        auto submitStart = std::chrono::high_resolution_clock::now();
        vk::Queue(graphicsQueue).submit(submitInfo, nullptr);
        auto submitEnd = std::chrono::high_resolution_clock::now();

        if (diagnostics) {
            diagnostics->queueSubmitTimeMs =
                std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();
        }
    } catch (const vk::DeviceLostError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Device lost during queue submit");
        resizeNeeded_ = true;
        return FrameResult::DeviceLost;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit draw command buffer: %s", e.what());
        return FrameResult::SubmitFailed;
    }

    return FrameResult::Success;
}

FrameResult FrameExecutor::present(uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics) {
    VkQueue presentQueue = vulkanContext_->getVkPresentQueue();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    vk::Semaphore waitSemaphores[] = {frameSync_->currentRenderFinishedSemaphore()};
    vk::SwapchainKHR swapChains[] = {swapchain};

    auto presentInfo = vk::PresentInfoKHR{}
        .setWaitSemaphores(waitSemaphores)
        .setSwapchains(swapChains)
        .setImageIndices(imageIndex);

    auto presentStart = std::chrono::high_resolution_clock::now();
    try {
        auto presentResult = vk::Queue(presentQueue).presentKHR(presentInfo);
        if (presentResult == vk::Result::eSuboptimalKHR) {
            resizeNeeded_ = true;
        }
    } catch (const vk::OutOfDateKHRError&) {
        resizeNeeded_ = true;
        return FrameResult::SwapchainOutOfDate;
    } catch (const vk::SurfaceLostKHRError&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface lost during present, will recover");
        resizeNeeded_ = true;
        return FrameResult::SurfaceLost;
    } catch (const vk::DeviceLostError&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Device lost during present, will recover");
        resizeNeeded_ = true;
        return FrameResult::DeviceLost;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to present swapchain image: %s", e.what());
        return FrameResult::SubmitFailed;
    }
    auto presentEnd = std::chrono::high_resolution_clock::now();

    if (diagnostics) {
        diagnostics->presentTimeMs =
            std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();
    }

    return FrameResult::Success;
}

FrameResult FrameExecutor::submitAndPresent(VkCommandBuffer cmd, uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics) {
    FrameResult submitResult = submitCommandBuffer(cmd, diagnostics);
    if (submitResult != FrameResult::Success) {
        return submitResult;
    }
    return present(imageIndex, diagnostics);
}
