#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <string>

#include "TreeEditorGui.h"

class Renderer;
class Camera;

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
    void render(Renderer& renderer, const Camera& camera, float deltaTime, float fps);
    void endFrame(VkCommandBuffer cmd);

    bool wantsInput() const;
    bool isVisible() const { return visible; }
    void toggleVisibility() { visible = !visible; }
    void setVisible(bool v) { visible = v; }

private:
    void setupStyle();
    void renderDashboard(Renderer& renderer, const Camera& camera, float fps);
    void renderTimeSection(Renderer& renderer);
    void renderWeatherSection(Renderer& renderer);
    void renderEnvironmentSection(Renderer& renderer);
    void renderPostProcessSection(Renderer& renderer);
    void renderTerrainSection(Renderer& renderer);
    void renderWaterSection(Renderer& renderer);
    void renderDebugSection(Renderer& renderer);
    void renderProfilerSection(Renderer& renderer);
    void renderHelpOverlay();
    void renderPositionPanel(const Camera& camera);

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;
    bool showHelp = false;

    // Tree editor as separate window
    TreeEditorGui treeEditorGui;

    // Cached performance metrics
    float frameTimeHistory[120] = {0};
    int frameTimeIndex = 0;
    float avgFrameTime = 0.0f;

    // Height fog layer enable state and cached values
    bool heightFogEnabled = true;
    float cachedLayerDensity = 0.02f;

    // Atmospheric scattering enable state and cached values
    bool atmosphereEnabled = true;
    float cachedRayleighScale = 13.558f;
    float cachedMieScale = 3.996f;

public:
    // Access to tree editor GUI
    TreeEditorGui& getTreeEditorGui() { return treeEditorGui; }
    const TreeEditorGui& getTreeEditorGui() const { return treeEditorGui; }
};
