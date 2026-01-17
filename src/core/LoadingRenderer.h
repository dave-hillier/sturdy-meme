#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <functional>

class VulkanContext;

/**
 * LoadingRenderer - Minimal renderer for early loading screen
 *
 * This component displays a simple animated loading screen while the
 * heavy game systems (terrain, physics, etc.) are being initialized.
 *
 * Design:
 * - Borrows VulkanContext (does not take ownership)
 * - Creates its own render pass, framebuffers, pipeline, command buffers
 * - Renders a rotating quad with animated colors
 * - Resources are cleaned up before full Renderer takes over
 *
 * Usage:
 *   auto loading = LoadingRenderer::create(vulkanContext, shaderPath);
 *   while (!loadingComplete) {
 *       loading->render();
 *       SDL_PumpEvents();
 *   }
 *   loading->cleanup();  // Must call before creating full Renderer
 */
class LoadingRenderer {
public:
    struct InitInfo {
        VulkanContext* vulkanContext;  // Borrowed, not owned
        std::string shaderPath;
    };

    /**
     * Factory: Create and initialize LoadingRenderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<LoadingRenderer> create(const InitInfo& info);

    ~LoadingRenderer();

    // Non-copyable, non-movable
    LoadingRenderer(const LoadingRenderer&) = delete;
    LoadingRenderer& operator=(const LoadingRenderer&) = delete;
    LoadingRenderer(LoadingRenderer&&) = delete;
    LoadingRenderer& operator=(LoadingRenderer&&) = delete;

    /**
     * Render one frame of the loading screen.
     * Returns true if frame was rendered, false if skipped (minimized window).
     */
    bool render();

    /**
     * Cleanup all resources. MUST be called before VulkanContext is used
     * by the full Renderer to avoid resource conflicts.
     */
    void cleanup();

    /**
     * Set loading progress (0.0 to 1.0) for optional progress display.
     */
    void setProgress(float progress) { progress_ = progress; }

private:
    LoadingRenderer() = default;

    bool init(const InitInfo& info);
    bool createRenderPass();
    bool createFramebuffers();
    bool createPipeline();
    bool createCommandPool();
    bool createSyncObjects();

    // Push constants for the loading shader
    struct LoadingPushConstants {
        float time;
        float aspect;
        float progress;
        float _pad;
    };

    std::optional<std::reference_wrapper<VulkanContext>> ctx_;  // Borrowed, not owned
    std::string shaderPath_;

    // Render resources (all managed with RAII via vulkan-hpp)
    std::optional<vk::raii::RenderPass> renderPass_;
    std::vector<vk::raii::Framebuffer> framebuffers_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;
    std::optional<vk::raii::CommandPool> commandPool_;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Sync objects
    std::optional<vk::raii::Semaphore> imageAvailableSemaphore_;
    std::optional<vk::raii::Semaphore> renderFinishedSemaphore_;
    std::optional<vk::raii::Fence> inFlightFence_;

    // State
    float startTime_ = 0.0f;
    float progress_ = 0.0f;
    bool initialized_ = false;
};
