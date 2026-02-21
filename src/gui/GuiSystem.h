#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

#include "GuiIKTab.h"
#include "GuiPlayerTab.h"
#include "GuiEnvironmentTab.h"
#include "GuiSceneGraphTab.h"
#include "GuiTileLoaderTab.h"
#include "GuiDashboard.h"
#include "GuiInterfaces.h"
#include "SceneEditorState.h"

class Camera;

class GuiSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GuiSystem(ConstructToken) {}

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

    // Get scene editor state for external systems
    SceneEditorState& getSceneEditorState() { return sceneEditorState; }
    const SceneEditorState& getSceneEditorState() const { return sceneEditorState; }

    // Check if gizmo is being used (for input blocking)
    bool isGizmoActive() const;

private:
    bool initInternal(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                      VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                      VkRenderPass renderPass, uint32_t imageCount);
    void cleanup();

    void renderMainMenuBar();
    void renderDashboard(GuiInterfaces& interfaces, const Camera& camera, float deltaTime, float fps);
    void renderPositionPanel(const Camera& camera);
    void renderTimeWindow(GuiInterfaces& ui);
    void renderWeatherWindow(GuiInterfaces& ui);
    void renderEnvironmentWindow(GuiInterfaces& ui);
    void renderPostFXWindow(GuiInterfaces& ui);
    void renderTerrainWindow(GuiInterfaces& ui);
    void renderWaterWindow(GuiInterfaces& ui);
    void renderTreesWindow(GuiInterfaces& ui);
    void renderGrassWindow(GuiInterfaces& ui);
    void renderPlayerWindow(GuiInterfaces& ui);
    void renderIKWindow(GuiInterfaces& ui, const Camera& camera);
    void renderDebugWindow(GuiInterfaces& ui);
    void renderPerformanceWindow(GuiInterfaces& ui);
    void renderProfilerWindow(GuiInterfaces& ui);
    void renderTileLoaderWindow(GuiInterfaces& ui, const Camera& camera);
    void renderSceneGraphWindow(GuiInterfaces& ui);
    void renderSceneEditorWindow(GuiInterfaces& ui);

    VkDevice device_ = VK_NULL_HANDLE;  // Stored for cleanup
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;

    // IK debug settings
    IKDebugSettings ikDebugSettings;

    // Player settings
    PlayerSettings playerSettings;

    // Environment tab state
    EnvironmentTabState environmentTabState;

    // Scene graph tab state
    SceneGraphTabState sceneGraphTabState;

    // Scene editor state (Unity-like hierarchy + inspector)
    SceneEditorState sceneEditorState;

    // Dashboard state (frame time tracking)
    GuiDashboard::State dashboardState;

    // Tile loader state
    GuiTileLoaderTab::State tileLoaderState;

    // Window visibility states for menu-based UI
    struct WindowStates {
        bool showDashboard = true;
        bool showPosition = true;
        bool showTime = false;
        bool showWeather = false;
        bool showEnvironment = false;
        bool showPostFX = false;
        bool showTerrain = false;
        bool showWater = false;
        bool showTrees = false;
        bool showGrass = false;
        bool showPlayer = false;
        bool showIK = false;
        bool showDebug = false;
        bool showPerformance = false;
        bool showProfiler = false;
        bool showTileLoader = false;
        bool showSceneGraph = false;
        bool showSceneEditor = false;  // Unity-like scene editor
        bool showHierarchy = false;     // ECS hierarchy panel (independent dockable window)
        bool showInspector = false;     // Entity inspector panel (independent dockable window)
    } windowStates;

    // Track whether the default dock layout has been applied
    bool dockLayoutInitialized_ = false;
};
