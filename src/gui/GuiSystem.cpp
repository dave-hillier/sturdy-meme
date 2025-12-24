#include "GuiSystem.h"
#include "Renderer.h"
#include "Camera.h"
#include "TimeSystem.h"

// Interface headers for casting
#include "core/interfaces/ITimeSystem.h"
#include "core/interfaces/ILocationControl.h"
#include "core/interfaces/IWeatherControl.h"
#include "core/interfaces/IEnvironmentControl.h"
#include "core/interfaces/IPostProcessControl.h"
#include "core/interfaces/ITerrainControl.h"
#include "core/interfaces/IWaterControl.h"
#include "core/interfaces/ITreeControl.h"
#include "core/interfaces/IDebugControl.h"
#include "core/interfaces/IProfilerControl.h"
#include "core/interfaces/IPerformanceControl.h"
#include "core/interfaces/ISceneControl.h"
#include "core/interfaces/IPlayerControl.h"

// GUI tab headers
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

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <cmath>
#include <algorithm>

static void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        SDL_Log("ImGui Vulkan Error: VkResult = %d", err);
    }
}

// Private constructor
GuiSystem::GuiSystem() = default;

// Factory
std::unique_ptr<GuiSystem> GuiSystem::create(SDL_Window* window, VkInstance instance,
                                              VkPhysicalDevice physicalDevice, VkDevice device,
                                              uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                                              VkRenderPass renderPass, uint32_t imageCount) {
    auto gui = std::unique_ptr<GuiSystem>(new GuiSystem());
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

// Note: Move operations are deleted - GuiSystem is stored via unique_ptr only

bool GuiSystem::initInternal(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                              VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                              VkRenderPass renderPass, uint32_t imageCount) {
    device_ = device;  // Store for cleanup

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool) != VK_SUCCESS) {
        SDL_Log("Failed to create ImGui descriptor pool");
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
    setupStyle();

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

void GuiSystem::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Modern dark theme with blue accent
    ImVec4 bgDark = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    ImVec4 bgMid = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    ImVec4 bgLight = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
    ImVec4 accentHover = ImVec4(0.36f, 0.69f, 1.0f, 1.0f);
    ImVec4 accentActive = ImVec4(0.16f, 0.49f, 0.88f, 1.0f);
    ImVec4 textBright = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    ImVec4 textDim = ImVec4(0.60f, 0.60f, 0.65f, 1.0f);

    colors[ImGuiCol_WindowBg] = bgDark;
    colors[ImGuiCol_PopupBg] = bgMid;
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = bgMid;
    colors[ImGuiCol_FrameBgHovered] = bgLight;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);

    colors[ImGuiCol_TitleBg] = bgDark;
    colors[ImGuiCol_TitleBgActive] = bgMid;
    colors[ImGuiCol_TitleBgCollapsed] = bgDark;

    colors[ImGuiCol_MenuBarBg] = bgMid;
    colors[ImGuiCol_ScrollbarBg] = bgDark;
    colors[ImGuiCol_ScrollbarGrab] = bgLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentActive;

    colors[ImGuiCol_Button] = bgLight;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
    colors[ImGuiCol_ButtonActive] = accent;

    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.26f, 0.32f, 1.0f);
    colors[ImGuiCol_HeaderActive] = accent;

    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = accentHover;
    colors[ImGuiCol_ResizeGripActive] = accentActive;

    colors[ImGuiCol_Tab] = bgLight;
    colors[ImGuiCol_TabHovered] = accentHover;
    colors[ImGuiCol_TabSelected] = accent;
    colors[ImGuiCol_TabDimmed] = bgMid;
    colors[ImGuiCol_TabDimmedSelected] = bgLight;

    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;

    colors[ImGuiCol_TableHeaderBg] = bgMid;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_Text] = textBright;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

    colors[ImGuiCol_DragDropTarget] = accentHover;
    colors[ImGuiCol_NavHighlight] = accent;

    // Rounding and spacing for modern look
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;

    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Scale for high DPI
    style.ScaleAllSizes(1.0f);
}

void GuiSystem::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void GuiSystem::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void GuiSystem::render(Renderer& renderer, const Camera& camera, float deltaTime, float fps) {
    if (!visible) return;

    // Update frame time history
    frameTimeHistory[frameTimeIndex] = deltaTime * 1000.0f;
    frameTimeIndex = (frameTimeIndex + 1) % 120;

    // Calculate average
    float sum = 0.0f;
    for (int i = 0; i < 120; i++) {
        sum += frameTimeHistory[i];
    }
    avgFrameTime = sum / 120.0f;

    // Main control panel
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 680), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Engine Controls", &visible, windowFlags)) {
        renderDashboard(renderer, camera, fps);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("ControlTabs")) {
            if (ImGui::BeginTabItem("Time")) {
                // TimeSystem implements ITimeSystem directly, Renderer provides ILocationControl
                GuiTimeTab::render(renderer.getTimeSystem(), renderer.getLocationControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Weather")) {
                GuiWeatherTab::render(renderer.getWeatherControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Environment")) {
                GuiEnvironmentTab::render(renderer.getEnvironmentControl(), environmentTabState);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Post FX")) {
                GuiPostFXTab::render(renderer.getPostProcessControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Terrain")) {
                GuiTerrainTab::render(renderer.getTerrainControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Water")) {
                GuiWaterTab::render(renderer.getWaterControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Trees")) {
                GuiTreeTab::render(renderer.getTreeControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Player")) {
                GuiPlayerTab::render(renderer.getPlayerControl(), playerSettings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("IK")) {
                GuiIKTab::render(renderer.getSceneControl(), camera, ikDebugSettings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                GuiDebugTab::render(renderer.getDebugControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Perf")) {
                GuiPerformanceTab::render(renderer.getPerformanceControl());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Profiler")) {
                GuiProfilerTab::render(renderer.getProfilerControl());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // Position panel (separate window)
    renderPositionPanel(camera);

    // Skeleton/IK debug overlay
    if (ikDebugSettings.showSkeleton || ikDebugSettings.showIKTargets) {
        GuiIKTab::renderSkeletonOverlay(renderer.getSceneControl(), camera, ikDebugSettings, playerSettings.showCapeColliders);
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

void GuiSystem::renderDashboard(Renderer& renderer, const Camera& camera, float fps) {
    // Performance metrics header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("PERFORMANCE");
    ImGui::PopStyleColor();

    ImGui::Separator();

    // FPS and frame time in columns
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 160);

    ImGui::Text("FPS");
    ImGui::PushStyleColor(ImGuiCol_Text, fps > 55.0f ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                                          fps > 30.0f ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                                        ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
    ImGui::SameLine(80);
    ImGui::Text("%.0f", fps);
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    ImGui::Text("Frame Time");
    ImGui::SameLine(80);
    ImGui::Text("%.2f ms", avgFrameTime);

    ImGui::Columns(1);

    // Frame time graph
    ImGui::PlotLines("##frametime", frameTimeHistory, 120, frameTimeIndex,
                     nullptr, 0.0f, 33.3f, ImVec2(-1, 40));

    // Quick stats
    ImGui::Spacing();
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 160);

    uint32_t triCount = renderer.getTerrainNodeCount();
    ImGui::Text("Terrain Tris");
    ImGui::SameLine(100);
    if (triCount >= 1000000) {
        ImGui::Text("%.2fM", triCount / 1000000.0f);
    } else if (triCount >= 1000) {
        ImGui::Text("%.0fK", triCount / 1000.0f);
    } else {
        ImGui::Text("%u", triCount);
    }

    ImGui::NextColumn();

    float tod = renderer.getTimeOfDay();
    int h = static_cast<int>(tod * 24.0f);
    int m = static_cast<int>((tod * 24.0f - h) * 60.0f);
    ImGui::Text("Time");
    ImGui::SameLine(60);
    ImGui::Text("%02d:%02d", h, m);

    ImGui::Columns(1);

    // Camera position
    ImGui::Spacing();
    glm::vec3 pos = camera.getPosition();
    ImGui::Text("Camera: X %.1f  Y %.1f  Z %.1f", pos.x, pos.y, pos.z);
}

void GuiSystem::renderPositionPanel(const Camera& camera) {
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    // Position in top-right corner
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 200, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(180, 280), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Position", nullptr, windowFlags)) {
        // Position section
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("POSITION");
        ImGui::PopStyleColor();

        glm::vec3 pos = camera.getPosition();
        ImGui::Text("X: %.1f", pos.x);
        ImGui::Text("Y: %.1f", pos.y);
        ImGui::Text("Z: %.1f", pos.z);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Orientation section
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.5f, 1.0f));
        ImGui::Text("ORIENTATION");
        ImGui::PopStyleColor();

        float yaw = camera.getYaw();
        float pitch = camera.getPitch();

        ImGui::Text("Yaw:   %.1f", yaw);
        ImGui::Text("Pitch: %.1f", pitch);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Compass section
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
        ImGui::Text("COMPASS");
        ImGui::PopStyleColor();

        // Draw compass
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 compassCenter = ImGui::GetCursorScreenPos();
        float compassRadius = 50.0f;
        compassCenter.x += compassRadius + 10;
        compassCenter.y += compassRadius + 5;

        // Background circle
        drawList->AddCircleFilled(compassCenter, compassRadius, IM_COL32(40, 40, 50, 200));
        drawList->AddCircle(compassCenter, compassRadius, IM_COL32(100, 100, 120, 255), 32, 2.0f);

        // Cardinal direction markers
        const float PI = 3.14159265358979323846f;
        // North is at yaw = -90 in this coordinate system (negative Z direction)
        // Adjust so compass shows correct heading
        float northAngle = (-90.0f - yaw) * PI / 180.0f;

        // Draw cardinal points (N, E, S, W)
        const char* cardinals[] = {"N", "E", "S", "W"};
        ImU32 cardinalColors[] = {
            IM_COL32(255, 80, 80, 255),   // N - Red
            IM_COL32(200, 200, 200, 255), // E - White
            IM_COL32(200, 200, 200, 255), // S - White
            IM_COL32(200, 200, 200, 255)  // W - White
        };

        for (int i = 0; i < 4; i++) {
            float angle = northAngle + i * PI / 2.0f;
            float textRadius = compassRadius - 12.0f;
            ImVec2 textPos(
                compassCenter.x + std::sin(angle) * textRadius - 4,
                compassCenter.y - std::cos(angle) * textRadius - 6
            );
            drawList->AddText(textPos, cardinalColors[i], cardinals[i]);
        }

        // Draw tick marks for 8 directions
        for (int i = 0; i < 8; i++) {
            float angle = northAngle + i * PI / 4.0f;
            float innerRadius = (i % 2 == 0) ? compassRadius - 20.0f : compassRadius - 14.0f;
            float outerRadius = compassRadius - 4.0f;
            ImVec2 inner(
                compassCenter.x + std::sin(angle) * innerRadius,
                compassCenter.y - std::cos(angle) * innerRadius
            );
            ImVec2 outer(
                compassCenter.x + std::sin(angle) * outerRadius,
                compassCenter.y - std::cos(angle) * outerRadius
            );
            ImU32 tickColor = (i % 2 == 0) ? IM_COL32(150, 150, 160, 255) : IM_COL32(80, 80, 90, 255);
            drawList->AddLine(inner, outer, tickColor, 1.5f);
        }

        // Draw direction indicator (points where camera is looking)
        float indicatorLength = compassRadius - 8.0f;
        ImVec2 indicatorTip(
            compassCenter.x,
            compassCenter.y - indicatorLength
        );

        // Triangle indicator pointing up (camera forward direction)
        ImVec2 tri1(compassCenter.x, compassCenter.y - indicatorLength);
        ImVec2 tri2(compassCenter.x - 6, compassCenter.y - indicatorLength + 18);
        ImVec2 tri3(compassCenter.x + 6, compassCenter.y - indicatorLength + 18);
        drawList->AddTriangleFilled(tri1, tri2, tri3, IM_COL32(255, 200, 100, 255));

        // Center dot
        drawList->AddCircleFilled(compassCenter, 4.0f, IM_COL32(200, 200, 220, 255));

        // Reserve space for compass
        ImGui::Dummy(ImVec2(compassRadius * 2 + 20, compassRadius * 2 + 15));

        // Heading display
        // Normalize yaw to 0-360 range for bearing display
        float bearing = std::fmod(-yaw + 90.0f, 360.0f);
        if (bearing < 0) bearing += 360.0f;
        ImGui::Text("Bearing: %.0f", bearing);
    }
    ImGui::End();
}
