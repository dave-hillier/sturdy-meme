#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>

#include "TreeEditorGui.h"

class Renderer;
class Camera;

// IK debug settings for GUI control
struct IKDebugSettings {
    bool showSkeleton = false;
    bool showIKTargets = false;
    bool showFootPlacement = false;

    // IK feature enables
    bool lookAtEnabled = false;
    bool footPlacementEnabled = false;
    bool straddleEnabled = false;

    // Look-at target mode
    enum class LookAtMode { Fixed, Camera, Mouse };
    LookAtMode lookAtMode = LookAtMode::Camera;
    glm::vec3 fixedLookAtTarget = glm::vec3(0, 1.5f, 5.0f);

    // Foot placement
    float groundOffset = 0.0f;
};

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

    // Get IK debug settings for external systems
    IKDebugSettings& getIKDebugSettings() { return ikDebugSettings; }
    const IKDebugSettings& getIKDebugSettings() const { return ikDebugSettings; }

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
    void renderIKSection(Renderer& renderer, const Camera& camera);
    void renderProfilerSection(Renderer& renderer);
    void renderHelpOverlay();
    void renderPositionPanel(const Camera& camera);
    void renderSkeletonOverlay(Renderer& renderer, const Camera& camera);

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;
    bool showHelp = false;

    // IK debug settings
    IKDebugSettings ikDebugSettings;

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
