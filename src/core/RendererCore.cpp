#include "RendererCore.h"
#include "VulkanContext.h"
#include "QueueSubmitDiagnostics.h"
#include "TaskScheduler.h"
#include <SDL3/SDL.h>
#include <chrono>

bool RendererCore::init(const InitParams& params) {
    if (!params.vulkanContext || !params.frameGraph || !params.frameSync) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RendererCore::init: missing required parameters");
        return false;
    }

    vulkanContext_ = params.vulkanContext;
    frameGraph_ = params.frameGraph;
    frameSync_ = params.frameSync;

    SDL_Log("RendererCore initialized");
    return true;
}

void RendererCore::destroy() {
    // Don't destroy frameSync_ - it's owned by Renderer
    vulkanContext_ = nullptr;
    frameGraph_ = nullptr;
    frameSync_ = nullptr;
}

RendererCore::FrameBeginResult RendererCore::beginFrame() {
    FrameBeginResult result;

    // Skip if window is suspended
    if (windowSuspended_) {
        result.error = FrameResult::Skipped;
        return result;
    }

    // Skip if window is minimized
    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        result.error = FrameResult::Skipped;
        return result;
    }

    // Wait for this frame slot to be available
    frameSync_->waitForCurrentFrameIfNeeded();

    // Acquire swapchain image
    return acquireSwapchainImage();
}

RendererCore::FrameBeginResult RendererCore::acquireSwapchainImage() {
    FrameBeginResult result;

    VkDevice device = vulkanContext_->getVkDevice();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    uint32_t imageIndex;
    // Use finite timeout (100ms) to prevent freezing when surface becomes unavailable
    // (e.g., macOS screen lock). This allows the event loop to continue processing.
    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms in nanoseconds
    VkResult vkResult = vkAcquireNextImageKHR(
        device, swapchain, acquireTimeoutNs,
        frameSync_->currentImageAvailableSemaphore(),
        VK_NULL_HANDLE, &imageIndex);

    if (vkResult == VK_TIMEOUT || vkResult == VK_NOT_READY) {
        // Timeout acquiring image - surface may be unavailable (e.g., macOS screen lock)
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

    // Reset fence for this frame (no-op with timeline semaphores, kept for API compatibility)
    frameSync_->resetCurrentFence();

    currentImageIndex_ = imageIndex;
    result.success = true;
    result.imageIndex = imageIndex;
    return result;
}

void RendererCore::executeFrameGraph(const FrameExecutionParams& params) {
    if (!frameGraph_) return;

    frameGraph_->execute(
        const_cast<FrameGraph::RenderContext&>(params.frameGraphContext),
        params.taskScheduler);
}

FrameResult RendererCore::submitCommandBuffer(VkCommandBuffer cmd, QueueSubmitDiagnostics* diagnostics) {
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

FrameResult RendererCore::present(uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics) {
    VkQueue presentQueue = vulkanContext_->getVkPresentQueue();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    // Wait on render finished semaphore before present
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

FrameResult RendererCore::submitAndPresent(const FrameExecutionParams& params) {
    // Submit command buffer
    FrameResult submitResult = submitCommandBuffer(params.commandBuffer, params.diagnostics);
    if (submitResult != FrameResult::Success) {
        return submitResult;
    }

    // Present to screen
    return present(params.swapchainImageIndex, params.diagnostics);
}

void RendererCore::endFrame() {
    frameSync_->advance();
}

FrameResult RendererCore::executeFrame(const FrameExecutionParams& params) {
    // Execute frame graph
    executeFrameGraph(params);

    // Submit and present
    FrameResult result = submitAndPresent(params);
    if (result != FrameResult::Success) {
        return result;
    }

    // Advance frame synchronization
    endFrame();

    return FrameResult::Success;
}
