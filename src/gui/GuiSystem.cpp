#include "GuiSystem.h"
#include "GuiInterfaces.h"
#include "Camera.h"

// Interface headers
#include "core/interfaces/ITimeSystem.h"
#include "core/interfaces/ILocationControl.h"
#include "core/interfaces/IWeatherState.h"
#include "core/interfaces/IEnvironmentControl.h"
#include "core/interfaces/IPostProcessState.h"
#include "core/interfaces/ICloudShadowControl.h"
#include "core/interfaces/ITerrainControl.h"
#include "core/interfaces/IWaterControl.h"
#include "core/interfaces/ITreeControl.h"
#include "core/interfaces/IDebugControl.h"
#include "core/interfaces/IProfilerControl.h"
#include "core/interfaces/IPerformanceControl.h"
#include "core/interfaces/ISceneControl.h"
#include "core/interfaces/IPlayerControl.h"
#include "EnvironmentSettings.h"

// GUI module headers
#include "GuiStyle.h"
#include "GuiDashboard.h"
#include "GuiPositionPanel.h"
#include "GuiTileLoaderTab.h"
#include "GuiTimeTab.h"
#include "GuiWeatherTab.h"
#include "GuiEnvironmentTab.h"
#include "GuiPostFXTab.h"
#include "GuiTerrainTab.h"
#include "GuiWaterTab.h"
#include "GuiDebugTab.h"
#include "GuiProfilerTab.h"
#include "GuiPerformanceTab.h"
#include "GuiIKTab.h"
#include "GuiPlayerTab.h"
#include "GuiTreeTab.h"
#include "GuiGrassTab.h"
#include "GuiSceneGraphTab.h"
#include "GuiSceneEditor.h"
#include "GuiHierarchyPanel.h"
#include "GuiInspectorPanel.h"
#include "GuiGizmo.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

static void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        SDL_Log("ImGui Vulkan Error: VkResult = %d", err);
    }
}

// Factory
std::unique_ptr<GuiSystem> GuiSystem::create(SDL_Window* window, VkInstance instance,
                                              VkPhysicalDevice physicalDevice, VkDevice device,
                                              uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                                              VkRenderPass renderPass, uint32_t imageCount) {
    auto gui = std::make_unique<GuiSystem>(ConstructToken{});
    if (!gui->initInternal(window, instance, physicalDevice, device, graphicsQueueFamily,
                           graphicsQueue, renderPass, imageCount)) {
        return nullptr;
    }
    return gui;
}

// Destructor
GuiSystem::~GuiSystem() {
    cleanup();
}

bool GuiSystem::initInternal(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                              VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                              VkRenderPass renderPass, uint32_t imageCount) {
    device_ = device;  // Store for cleanup

    // Create descriptor pool for ImGui
    std::array<vk::DescriptorPoolSize, 11> poolSizes = {{
        {vk::DescriptorType::eSampler, 1000},
        {vk::DescriptorType::eCombinedImageSampler, 1000},
        {vk::DescriptorType::eSampledImage, 1000},
        {vk::DescriptorType::eStorageImage, 1000},
        {vk::DescriptorType::eUniformTexelBuffer, 1000},
        {vk::DescriptorType::eStorageTexelBuffer, 1000},
        {vk::DescriptorType::eUniformBuffer, 1000},
        {vk::DescriptorType::eStorageBuffer, 1000},
        {vk::DescriptorType::eUniformBufferDynamic, 1000},
        {vk::DescriptorType::eStorageBufferDynamic, 1000},
        {vk::DescriptorType::eInputAttachment, 1000}
    }};

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setMaxSets(1000)
        .setPoolSizes(poolSizes);

    vk::Device vkDevice(device);
    try {
        imguiPool = vkDevice.createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create ImGui descriptor pool: %s", e.what());
        return false;
    }

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.RenderPass = renderPass;
    initInfo.CheckVkResultFn = checkVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        SDL_Log("Failed to initialize ImGui Vulkan backend");
        return false;
    }

    // Setup custom style
    GuiStyle::apply();

    SDL_Log("ImGui initialized successfully");
    return true;
}

void GuiSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;  // Not initialized or already cleaned up

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, imguiPool, nullptr);
        imguiPool = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

void GuiSystem::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void GuiSystem::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void GuiSystem::render(GuiInterfaces& ui, const Camera& camera, float deltaTime, float fps) {
    if (!visible) return;

    // Create main viewport dockspace - allows all windows to be freely dockable
    ImGuiID mainDockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);

    // Set up default dock layout on first use when editor panels are shown
    if (!dockLayoutInitialized_ && (windowStates.showHierarchy || windowStates.showInspector)) {
        ImGui::DockBuilderRemoveNode(mainDockspaceId);
        ImGui::DockBuilderAddNode(mainDockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(mainDockspaceId, ImGui::GetMainViewport()->Size);

        // Split: right 20% for inspector, then left side split for hierarchy
        ImGuiID dockMain = mainDockspaceId;
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.22f, nullptr, &dockMain);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);

        ImGui::DockBuilderFinish(mainDockspaceId);
        dockLayoutInitialized_ = true;
    }

    // Main menu bar
    renderMainMenuBar();

    // Render individual windows based on visibility state

    // View windows
    if (windowStates.showDashboard) {
        renderDashboard(ui, camera, deltaTime, fps);
    }
    if (windowStates.showPosition) {
        renderPositionPanel(camera);
    }

    // Environment windows
    if (windowStates.showTime) {
        renderTimeWindow(ui);
    }
    if (windowStates.showWeather) {
        renderWeatherWindow(ui);
    }
    if (windowStates.showEnvironment) {
        ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Environment", &windowStates.showEnvironment)) {
            ImGui::SeparatorText("Froxel Volumetric Fog");
            GuiEnvironmentTab::renderFroxelFog(ui.environment);
            ImGui::SeparatorText("Height Fog Layer");
            GuiEnvironmentTab::renderHeightFog(ui.environment, environmentTabState);
            ImGui::SeparatorText("Atmospheric Scattering");
            GuiEnvironmentTab::renderAtmosphere(ui.environment, environmentTabState);
            ImGui::SeparatorText("Clouds");
            GuiEnvironmentTab::renderClouds(ui.environment);
            ImGui::SeparatorText("Falling Leaves");
            GuiEnvironmentTab::renderLeaves(ui.environment);
        }
        ImGui::End();
    }

    // Post Processing window
    if (windowStates.showPostFX) {
        ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Post Processing", &windowStates.showPostFX)) {
            ImGui::SeparatorText("HDR Pipeline");
            GuiPostFXTab::renderHDRPipeline(ui.postProcess);
            ImGui::SeparatorText("Cloud Shadows");
            GuiPostFXTab::renderCloudShadows(ui.cloudShadow);
            ImGui::SeparatorText("Bloom");
            GuiPostFXTab::renderBloom(ui.postProcess);
            ImGui::SeparatorText("God Rays");
            GuiPostFXTab::renderGodRays(ui.postProcess);
            ImGui::SeparatorText("Volumetric Fog");
            GuiPostFXTab::renderVolumetricFogSettings(ui.postProcess);
            ImGui::SeparatorText("Local Tone Mapping");
            GuiPostFXTab::renderLocalToneMapping(ui.postProcess);
            ImGui::SeparatorText("Exposure");
            GuiPostFXTab::renderExposure(ui.postProcess);
        }
        ImGui::End();
    }

    // Rendering - Other
    if (windowStates.showTerrain) {
        renderTerrainWindow(ui);
    }
    if (windowStates.showWater) {
        renderWaterWindow(ui);
    }
    if (windowStates.showTrees) {
        renderTreesWindow(ui);
    }
    if (windowStates.showGrass) {
        renderGrassWindow(ui);
    }

    // Character window
    if (windowStates.showCharacter) {
        ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Character", &windowStates.showCharacter)) {
            ImGui::SeparatorText("Cape");
            GuiPlayerTab::renderCape(playerSettings);
            ImGui::SeparatorText("Weapons");
            GuiPlayerTab::renderWeapons(playerSettings);
            ImGui::SeparatorText("Character LOD");
            GuiPlayerTab::renderCharacterLOD(ui.player, playerSettings);
            ImGui::SeparatorText("Cape Info");
            GuiPlayerTab::renderCapeInfo();
            ImGui::SeparatorText("NPC LOD");
            GuiPlayerTab::renderNPCLOD(ui.player);
            ImGui::SeparatorText("Motion Matching");
            GuiPlayerTab::renderMotionMatching(ui.player, playerSettings);
        }
        ImGui::End();
    }
    if (windowStates.showIK) {
        renderIKWindow(ui, camera);
    }

    // Debug window
    if (windowStates.showDebug) {
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &windowStates.showDebug)) {
            ImGui::SeparatorText("Visualizations");
            GuiDebugTab::renderVisualizations(ui.debug);
            ImGui::SeparatorText("Occlusion Culling");
            GuiDebugTab::renderOcclusionCulling(ui.debug);
            ImGui::SeparatorText("System Info");
            GuiDebugTab::renderSystemInfo();
            ImGui::SeparatorText("Keyboard Shortcuts");
            GuiDebugTab::renderKeyboardShortcuts();
        }
        ImGui::End();
    }
    // Physics Debug: window open = feature enabled
    if (windowStates.showPhysicsDebug) {
        ui.debug.setPhysicsDebugEnabled(true);
        ImGui::SetNextWindowSize(ImVec2(280, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Physics Debug", &windowStates.showPhysicsDebug)) {
            GuiDebugTab::renderPhysicsDebugOptions(ui.debug);
        }
        ImGui::End();
    } else {
        ui.debug.setPhysicsDebugEnabled(false);
    }
    if (windowStates.showPerformance) {
        renderPerformanceWindow(ui);
    }
    if (windowStates.showProfiler) {
        renderProfilerWindow(ui);
    }
    if (windowStates.showTileLoader) {
        renderTileLoaderWindow(ui, camera);
    }
    if (windowStates.showSceneGraph) {
        renderSceneGraphWindow(ui);
    }

    // Scene Editor: render Hierarchy and Inspector as independent dockable windows
    if (windowStates.showSceneEditor) {
        // Legacy nested dockspace mode (keep for backwards compat if both flags are off)
        if (!windowStates.showHierarchy && !windowStates.showInspector) {
            renderSceneEditorWindow(ui);
        }
        GuiGizmo::render(camera, ui.scene, sceneEditorState);
    }

    // Independent dockable Hierarchy and Inspector panels
    if (windowStates.showHierarchy) {
        if (ImGui::Begin("Hierarchy", &windowStates.showHierarchy, ImGuiWindowFlags_MenuBar)) {
            GuiHierarchyPanel::renderCreateMenuBar(ui.scene, sceneEditorState);
            GuiHierarchyPanel::render(ui.scene, sceneEditorState);
        }
        ImGui::End();
        // Render gizmo when hierarchy is showing
        GuiGizmo::render(camera, ui.scene, sceneEditorState);
    }
    if (windowStates.showInspector) {
        if (ImGui::Begin("Inspector", &windowStates.showInspector)) {
            GuiInspectorPanel::render(ui.scene, sceneEditorState);
        }
        ImGui::End();
    }

    // Skeleton/IK debug overlay
    if (ikDebugSettings.showSkeleton || ikDebugSettings.showIKTargets) {
        GuiIKTab::renderSkeletonOverlay(ui.scene, camera, ikDebugSettings, playerSettings.showCapeColliders);
    }

    // Motion matching debug overlay
    if (playerSettings.motionMatchingEnabled &&
        (playerSettings.showMotionMatchingTrajectory ||
         playerSettings.showMotionMatchingFeatures ||
         playerSettings.showMotionMatchingStats)) {
        GuiPlayerTab::renderMotionMatchingOverlay(ui.player, camera, playerSettings);
    }
}

void GuiSystem::endFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void GuiSystem::cancelFrame() {
    // End the ImGui frame without rendering to GPU
    // This must be called if beginFrame() was called but render won't happen
    ImGui::EndFrame();
}

bool GuiSystem::wantsInput() const {
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void GuiSystem::renderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Dashboard", nullptr, &windowStates.showDashboard);
            ImGui::MenuItem("Position", nullptr, &windowStates.showPosition);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Environment")) {
            ImGui::MenuItem("Time", nullptr, &windowStates.showTime);
            ImGui::MenuItem("Weather", nullptr, &windowStates.showWeather);
            ImGui::Separator();
            ImGui::MenuItem("Fog / Atmosphere / Clouds", nullptr, &windowStates.showEnvironment);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Rendering")) {
            ImGui::MenuItem("Post Processing", nullptr, &windowStates.showPostFX);
            ImGui::Separator();
            ImGui::MenuItem("Terrain", nullptr, &windowStates.showTerrain);
            ImGui::MenuItem("Water", nullptr, &windowStates.showWater);
            ImGui::MenuItem("Trees", nullptr, &windowStates.showTrees);
            ImGui::MenuItem("Grass", nullptr, &windowStates.showGrass);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Character")) {
            ImGui::MenuItem("Character", nullptr, &windowStates.showCharacter);
            ImGui::Separator();
            ImGui::MenuItem("IK / Animation", nullptr, &windowStates.showIK);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            ImGui::MenuItem("Hierarchy", nullptr, &windowStates.showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &windowStates.showInspector);
            ImGui::Separator();
            ImGui::MenuItem("Scene Editor (Legacy)", nullptr, &windowStates.showSceneEditor);
            ImGui::MenuItem("Scene Graph", nullptr, &windowStates.showSceneGraph);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Debug", nullptr, &windowStates.showDebug);
            ImGui::MenuItem("Physics Debug", nullptr, &windowStates.showPhysicsDebug);
            ImGui::Separator();
            ImGui::MenuItem("Performance Toggles", nullptr, &windowStates.showPerformance);
            ImGui::MenuItem("Profiler", nullptr, &windowStates.showProfiler);
            ImGui::MenuItem("Tile Loader", nullptr, &windowStates.showTileLoader);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void GuiSystem::renderDashboard(GuiInterfaces& ui, const Camera& camera, float deltaTime, float fps) {
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Dashboard", &windowStates.showDashboard)) {
        GuiDashboard::render(ui.terrain, ui.time, camera, deltaTime, fps, dashboardState);
    }
    ImGui::End();
}

void GuiSystem::renderPositionPanel(const Camera& camera) {
    // Position in top-right corner
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 200, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(180, 280), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Position", &windowStates.showPosition)) {
        GuiPositionPanel::render(camera);
    }
    ImGui::End();
}

void GuiSystem::renderTimeWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(20, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Time", &windowStates.showTime)) {
        GuiTimeTab::render(ui.time, ui.location);
    }
    ImGui::End();
}

void GuiSystem::renderWeatherWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(20, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Weather", &windowStates.showWeather)) {
        GuiWeatherTab::render(ui.weather, ui.environmentSettings);
    }
    ImGui::End();
}

void GuiSystem::renderTerrainWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(320, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Terrain", &windowStates.showTerrain)) {
        GuiTerrainTab::render(ui.terrain);
    }
    ImGui::End();
}

void GuiSystem::renderWaterWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(320, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Water", &windowStates.showWater)) {
        GuiWaterTab::render(ui.water);
    }
    ImGui::End();
}

void GuiSystem::renderTreesWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(320, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Trees", &windowStates.showTrees)) {
        GuiTreeTab::render(ui.tree);
    }
    ImGui::End();
}

void GuiSystem::renderGrassWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(320, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 450), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Grass", &windowStates.showGrass)) {
        GuiGrassTab::render(ui.grass, ui.environment);
    }
    ImGui::End();
}

void GuiSystem::renderIKWindow(GuiInterfaces& ui, const Camera& camera) {
    ImGui::SetNextWindowPos(ImVec2(620, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 350), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("IK / Animation", &windowStates.showIK)) {
        GuiIKTab::render(ui.scene, camera, ikDebugSettings);
    }
    ImGui::End();
}

void GuiSystem::renderPerformanceWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(920, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Performance Toggles", &windowStates.showPerformance)) {
        GuiPerformanceTab::render(ui.performance);
    }
    ImGui::End();
}

void GuiSystem::renderProfilerWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(920, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Profiler", &windowStates.showProfiler)) {
        GuiProfilerTab::render(ui.profiler);
    }
    ImGui::End();
}

void GuiSystem::renderTileLoaderWindow(GuiInterfaces& ui, const Camera& camera) {
    ImGui::SetNextWindowPos(ImVec2(320, 300), ImGuiCond_FirstUseEver);
    // Window size: 32x32 grid * 16px cells = 512x512, plus padding and title
    ImGui::SetNextWindowSize(ImVec2(560, 650), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Tile Loader", &windowStates.showTileLoader)) {
        GuiTileLoaderTab::render(ui.terrain, ui.physicsTerrainTiles, camera, tileLoaderState);
    }
    ImGui::End();
}

void GuiSystem::renderSceneGraphWindow(GuiInterfaces& ui) {
    ImGui::SetNextWindowPos(ImVec2(620, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Graph", &windowStates.showSceneGraph)) {
        GuiSceneGraphTab::render(ui.scene, sceneGraphTabState);
    }
    ImGui::End();
}

void GuiSystem::renderSceneEditorWindow(GuiInterfaces& ui) {
    GuiSceneEditor::render(ui.scene, sceneEditorState, &windowStates.showSceneEditor);
}

bool GuiSystem::isGizmoActive() const {
    if (!windowStates.showSceneEditor && !windowStates.showHierarchy) return false;
    return GuiGizmo::isUsing() || GuiGizmo::isOver();
}
