#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

#include "GuiIKTab.h"
#include "GuiPlayerTab.h"
#include "GuiEnvironmentTab.h"
#include "GuiInterfaces.h"

class Camera;

class GuiSystem {
public:
    /**
     * Factory: Create and initialize GUI system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GuiSystem> create(SDL_Window* window, VkInstance instance,
                                              VkPhysicalDevice physicalDevice, VkDevice device,
                                              uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                                              VkRenderPass renderPass, uint32_t imageCount);

    ~GuiSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    GuiSystem(GuiSystem&&) = delete;
    GuiSystem& operator=(GuiSystem&&) = delete;
    GuiSystem(const GuiSystem&) = delete;
    GuiSystem& operator=(const GuiSystem&) = delete;

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void render(GuiInterfaces& interfaces, const Camera& camera, float deltaTime, float fps);
    void endFrame(VkCommandBuffer cmd);
    void cancelFrame();  // End frame without rendering (for early returns)

    bool wantsInput() const;
    bool isVisible() const { return visible; }
    void toggleVisibility() { visible = !visible; }
    void setVisible(bool v) { visible = v; }

    // Get IK debug settings for external systems
    IKDebugSettings& getIKDebugSettings() { return ikDebugSettings; }
    const IKDebugSettings& getIKDebugSettings() const { return ikDebugSettings; }

    // Get player settings for external systems
    PlayerSettings& getPlayerSettings() { return playerSettings; }
    const PlayerSettings& getPlayerSettings() const { return playerSettings; }

private:
    GuiSystem();  // Private: use factory
    bool initInternal(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                      VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                      VkRenderPass renderPass, uint32_t imageCount);
    void cleanup();

    void setupStyle();
    void renderDashboard(GuiInterfaces& interfaces, const Camera& camera, float fps);
    void renderPositionPanel(const Camera& camera);

    VkDevice device_ = VK_NULL_HANDLE;  // Stored for cleanup
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;

    // IK debug settings
    IKDebugSettings ikDebugSettings;

    // Player settings
    PlayerSettings playerSettings;

    // Environment tab state
    EnvironmentTabState environmentTabState;

    // Cached performance metrics
    float frameTimeHistory[120] = {0};
    int frameTimeIndex = 0;
    float avgFrameTime = 0.0f;
};
