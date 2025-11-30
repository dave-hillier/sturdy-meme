#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <string>

class Renderer;

class GuiSystem {
public:
    GuiSystem() = default;
    ~GuiSystem() = default;

    bool init(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
              VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
              VkRenderPass renderPass, uint32_t imageCount);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void render(Renderer& renderer, float deltaTime, float fps);
    void endFrame(VkCommandBuffer cmd);

    bool wantsInput() const;
    bool isVisible() const { return visible; }
    void toggleVisibility() { visible = !visible; }
    void setVisible(bool v) { visible = v; }

private:
    void setupStyle();
    void renderDashboard(Renderer& renderer, float fps);
    void renderTimeSection(Renderer& renderer);
    void renderWeatherSection(Renderer& renderer);
    void renderEnvironmentSection(Renderer& renderer);
    void renderPostProcessSection(Renderer& renderer);
    void renderTerrainSection(Renderer& renderer);
    void renderDebugSection(Renderer& renderer);
    void renderHelpOverlay();

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;
    bool showHelp = false;

    // Cached performance metrics
    float frameTimeHistory[120] = {0};
    int frameTimeIndex = 0;
    float avgFrameTime = 0.0f;
};
