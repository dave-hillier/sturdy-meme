#include "GuiSystem.h"
#include "Renderer.h"
#include "Camera.h"
#include "AtmosphereLUTSystem.h"
#include "AnimatedCharacter.h"

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

bool GuiSystem::init(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                     VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue,
                     VkRenderPass renderPass, uint32_t imageCount) {

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

void GuiSystem::shutdown() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (imguiPool != VK_NULL_HANDLE) {
        // Note: Pool is destroyed with device
        imguiPool = VK_NULL_HANDLE;
    }
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
                renderTimeSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Weather")) {
                renderWeatherSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Environment")) {
                renderEnvironmentSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Post FX")) {
                renderPostProcessSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Terrain")) {
                renderTerrainSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Water")) {
                renderWaterSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("IK")) {
                renderIKSection(renderer, camera);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                renderDebugSection(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Profiler")) {
                renderProfilerSection(renderer);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // Help overlay
    if (showHelp) {
        renderHelpOverlay();
    }

    // Position panel (separate window)
    renderPositionPanel(camera);

    // Skeleton/IK debug overlay
    if (ikDebugSettings.showSkeleton || ikDebugSettings.showIKTargets) {
        renderSkeletonOverlay(renderer, camera);
    }

    // Tree editor as separate window
    treeEditorGui.render(renderer, camera);
}

void GuiSystem::endFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
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

    // Help toggle
    ImGui::Spacing();
    if (ImGui::Button(showHelp ? "Hide Help (H)" : "Show Help (H)", ImVec2(-1, 0))) {
        showHelp = !showHelp;
    }

    // Tree Editor toggle
    if (ImGui::Button(treeEditorGui.isVisible() ? "Hide Tree Editor (F2)" : "Show Tree Editor (F2)", ImVec2(-1, 0))) {
        treeEditorGui.toggleVisibility();
    }
}

void GuiSystem::renderTimeSection(Renderer& renderer) {
    ImGui::Spacing();

    // Time of day slider
    float timeOfDay = renderer.getTimeOfDay();
    if (ImGui::SliderFloat("Time of Day", &timeOfDay, 0.0f, 1.0f, "%.3f")) {
        renderer.setTimeOfDay(timeOfDay);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0.0 = Midnight, 0.25 = Sunrise, 0.5 = Noon, 0.75 = Sunset");
    }

    // Quick time buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Dawn")) renderer.setTimeOfDay(0.25f);
    ImGui::SameLine();
    if (ImGui::Button("Noon")) renderer.setTimeOfDay(0.5f);
    ImGui::SameLine();
    if (ImGui::Button("Dusk")) renderer.setTimeOfDay(0.75f);
    ImGui::SameLine();
    if (ImGui::Button("Night")) renderer.setTimeOfDay(0.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Time scale
    float timeScale = renderer.getTimeScale();
    if (ImGui::SliderFloat("Time Scale", &timeScale, 0.0f, 100.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
        renderer.setTimeScale(timeScale);
    }

    if (ImGui::Button("Resume Real-Time")) {
        renderer.resumeAutoTime();
        renderer.setTimeScale(1.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Date controls
    ImGui::Text("Date (affects sun position):");
    int year = renderer.getCurrentYear();
    int month = renderer.getCurrentMonth();
    int day = renderer.getCurrentDay();

    bool dateChanged = false;
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Year", &year, 1, 10)) dateChanged = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Month", &month, 1, 1)) dateChanged = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Day", &day, 1, 1)) dateChanged = true;

    if (dateChanged) {
        month = std::clamp(month, 1, 12);
        day = std::clamp(day, 1, 31);
        renderer.setDate(year, month, day);
    }

    // Season presets
    ImGui::Text("Season:");
    ImGui::SameLine();
    if (ImGui::Button("Spring")) renderer.setDate(renderer.getCurrentYear(), 3, 20);
    ImGui::SameLine();
    if (ImGui::Button("Summer")) renderer.setDate(renderer.getCurrentYear(), 6, 21);
    ImGui::SameLine();
    if (ImGui::Button("Autumn")) renderer.setDate(renderer.getCurrentYear(), 9, 22);
    ImGui::SameLine();
    if (ImGui::Button("Winter")) renderer.setDate(renderer.getCurrentYear(), 12, 21);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Location
    GeographicLocation loc = renderer.getLocation();
    float lat = static_cast<float>(loc.latitude);
    float lon = static_cast<float>(loc.longitude);
    bool locChanged = false;

    if (ImGui::SliderFloat("Latitude", &lat, -90.0f, 90.0f, "%.1f")) locChanged = true;
    if (ImGui::SliderFloat("Longitude", &lon, -180.0f, 180.0f, "%.1f")) locChanged = true;

    if (locChanged) {
        renderer.setLocation({static_cast<double>(lat), static_cast<double>(lon)});
    }

    // Location presets
    ImGui::Text("Location:");
    if (ImGui::Button("London")) {
        renderer.setLocation({51.5f, -0.1f});
    }
    ImGui::SameLine();
    if (ImGui::Button("New York")) {
        renderer.setLocation({40.7f, -74.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Tokyo")) {
        renderer.setLocation({35.7f, 139.7f});
    }
    if (ImGui::Button("Sydney")) {
        renderer.setLocation({-33.9f, 151.2f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Arctic")) {
        renderer.setLocation({71.0f, 25.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Equator")) {
        renderer.setLocation({0.0f, 0.0f});
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Moon Phase Controls
    ImGui::Text("Moon Phase:");

    // Display current moon phase
    float currentPhase = renderer.getCurrentMoonPhase();
    const char* phaseNames[] = { "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                 "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent" };
    int phaseIndex = static_cast<int>(currentPhase * 8.0f) % 8;
    ImGui::Text("Current: %s (%.2f)", phaseNames[phaseIndex], currentPhase);

    // Override checkbox
    bool overrideEnabled = renderer.isMoonPhaseOverrideEnabled();
    if (ImGui::Checkbox("Override Moon Phase", &overrideEnabled)) {
        renderer.setMoonPhaseOverride(overrideEnabled);
    }

    // Manual phase slider (only active when override is enabled)
    if (overrideEnabled) {
        float manualPhase = renderer.getMoonPhase();
        if (ImGui::SliderFloat("Moon Phase", &manualPhase, 0.0f, 1.0f, "%.3f")) {
            renderer.setMoonPhase(manualPhase);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = New Moon, 0.25 = First Quarter, 0.5 = Full Moon, 0.75 = Last Quarter");
        }

        // Quick phase buttons
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("New")) renderer.setMoonPhase(0.0f);
        ImGui::SameLine();
        if (ImGui::Button("1st Q")) renderer.setMoonPhase(0.25f);
        ImGui::SameLine();
        if (ImGui::Button("Full")) renderer.setMoonPhase(0.5f);
        ImGui::SameLine();
        if (ImGui::Button("3rd Q")) renderer.setMoonPhase(0.75f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Eclipse Controls
    ImGui::Text("Solar Eclipse:");

    bool eclipseEnabled = renderer.isEclipseEnabled();
    if (ImGui::Checkbox("Enable Eclipse", &eclipseEnabled)) {
        renderer.setEclipseEnabled(eclipseEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Simulates a solar eclipse with the moon passing in front of the sun");
    }

    if (eclipseEnabled) {
        float eclipseAmount = renderer.getEclipseAmount();
        if (ImGui::SliderFloat("Eclipse Amount", &eclipseAmount, 0.0f, 1.0f, "%.3f")) {
            renderer.setEclipseAmount(eclipseAmount);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = No eclipse, 1.0 = Total eclipse");
        }

        // Eclipse presets
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("Partial")) renderer.setEclipseAmount(0.5f);
        ImGui::SameLine();
        if (ImGui::Button("Annular")) renderer.setEclipseAmount(0.85f);
        ImGui::SameLine();
        if (ImGui::Button("Total")) renderer.setEclipseAmount(1.0f);
    }
}

void GuiSystem::renderWeatherSection(Renderer& renderer) {
    ImGui::Spacing();

    // Weather type
    const char* weatherTypes[] = { "Rain", "Snow" };
    int weatherType = static_cast<int>(renderer.getWeatherType());
    if (ImGui::Combo("Weather Type", &weatherType, weatherTypes, 2)) {
        renderer.setWeatherType(static_cast<uint32_t>(weatherType));
    }

    // Intensity
    float intensity = renderer.getIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
        renderer.setWeatherIntensity(intensity);
    }

    // Quick intensity buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) renderer.setWeatherIntensity(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Light")) renderer.setWeatherIntensity(0.3f);
    ImGui::SameLine();
    if (ImGui::Button("Medium")) renderer.setWeatherIntensity(0.6f);
    ImGui::SameLine();
    if (ImGui::Button("Heavy")) renderer.setWeatherIntensity(1.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Snow coverage
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("SNOW COVERAGE");
    ImGui::PopStyleColor();

    float snowAmount = renderer.getSnowAmount();
    if (ImGui::SliderFloat("Snow Amount", &snowAmount, 0.0f, 1.0f)) {
        renderer.setSnowAmount(snowAmount);
    }

    glm::vec3 snowColor = renderer.getSnowColor();
    float sc[3] = {snowColor.r, snowColor.g, snowColor.b};
    if (ImGui::ColorEdit3("Snow Color", sc)) {
        renderer.setSnowColor(glm::vec3(sc[0], sc[1], sc[2]));
    }

    // Environment settings for snow
    auto& env = renderer.getEnvironmentSettings();

    if (ImGui::SliderFloat("Snow Roughness", &env.snowRoughness, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat("Accumulation Rate", &env.snowAccumulationRate, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat("Melt Rate", &env.snowMeltRate, 0.0f, 1.0f)) {}

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Wind settings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.7f, 1.0f));
    ImGui::Text("WIND");
    ImGui::PopStyleColor();

    float windDir[2] = {env.windDirection.x, env.windDirection.y};
    if (ImGui::SliderFloat2("Direction", windDir, -1.0f, 1.0f)) {
        env.windDirection = glm::vec2(windDir[0], windDir[1]);
    }

    if (ImGui::SliderFloat("Strength", &env.windStrength, 0.0f, 3.0f)) {}
    if (ImGui::SliderFloat("Speed", &env.windSpeed, 0.0f, 5.0f)) {}
    if (ImGui::SliderFloat("Gust Frequency", &env.gustFrequency, 0.0f, 2.0f)) {}
    if (ImGui::SliderFloat("Gust Amplitude", &env.gustAmplitude, 0.0f, 2.0f)) {}
}

void GuiSystem::renderEnvironmentSection(Renderer& renderer) {
    ImGui::Spacing();

    // ========== FROXEL VOLUMETRIC FOG ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("FROXEL VOLUMETRIC FOG");
    ImGui::PopStyleColor();

    bool fogEnabled = renderer.isFogEnabled();
    if (ImGui::Checkbox("Enable Froxel Fog", &fogEnabled)) {
        renderer.setFogEnabled(fogEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Frustum-aligned voxel grid volumetric fog with temporal reprojection");
    }

    if (fogEnabled) {
        // Main fog parameters - wide ranges for extreme testing
        float fogDensity = renderer.getFogDensity();
        if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogDensity(fogDensity);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no fog, 1 = extremely dense (logarithmic scale)");
        }

        float fogAbsorption = renderer.getFogAbsorption();
        if (ImGui::SliderFloat("Absorption", &fogAbsorption, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogAbsorption(fogAbsorption);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Light absorption coefficient (0 = transparent, 1 = opaque fog)");
        }

        float fogBaseHeight = renderer.getFogBaseHeight();
        if (ImGui::SliderFloat("Base Height", &fogBaseHeight, -500.0f, 500.0f, "%.1f")) {
            renderer.setFogBaseHeight(fogBaseHeight);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Height where fog density is maximum");
        }

        float fogScaleHeight = renderer.getFogScaleHeight();
        if (ImGui::SliderFloat("Scale Height", &fogScaleHeight, 0.1f, 2000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogScaleHeight(fogScaleHeight);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Exponential falloff (0.1 = thin layer, 2000 = fog everywhere)");
        }

        float volumetricFar = renderer.getVolumetricFarPlane();
        if (ImGui::SliderFloat("Far Plane", &volumetricFar, 10.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setVolumetricFarPlane(volumetricFar);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volumetric range (10 = close only, 5000 = entire scene)");
        }

        float temporalBlend = renderer.getTemporalBlend();
        if (ImGui::SliderFloat("Temporal Blend", &temporalBlend, 0.0f, 0.999f, "%.3f")) {
            renderer.setTemporalBlend(temporalBlend);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no temporal filtering (noisy), 0.999 = extreme smoothing (ghosting)");
        }

        // Quick presets for common scenarios
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("Clear##froxel")) {
            renderer.setFogDensity(0.0f);
            renderer.setLayerDensity(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Light##froxel")) {
            renderer.setFogDensity(0.005f);
            renderer.setFogAbsorption(0.005f);
            renderer.setFogScaleHeight(100.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dense##froxel")) {
            renderer.setFogDensity(0.03f);
            renderer.setFogAbsorption(0.02f);
            renderer.setFogScaleHeight(50.0f);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== HEIGHT FOG LAYER ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 0.9f, 1.0f));
    ImGui::Text("HEIGHT FOG LAYER");
    ImGui::PopStyleColor();

    if (fogEnabled) {
        // Enable toggle for height fog layer
        if (ImGui::Checkbox("Enable Height Fog", &heightFogEnabled)) {
            if (heightFogEnabled) {
                // Restore cached density
                renderer.setLayerDensity(cachedLayerDensity);
            } else {
                // Cache current density and zero it out
                cachedLayerDensity = renderer.getLayerDensity();
                if (cachedLayerDensity < 0.001f) cachedLayerDensity = 0.02f;  // Ensure valid restore value
                renderer.setLayerDensity(0.0f);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle ground-hugging fog layer");
        }

        if (heightFogEnabled) {
            float layerHeight = renderer.getLayerHeight();
            if (ImGui::SliderFloat("Layer Height", &layerHeight, -200.0f, 500.0f, "%.1f")) {
                renderer.setLayerHeight(layerHeight);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Top of ground fog layer (-200 = below ground, 500 = high altitude cloud)");
            }

            float layerThickness = renderer.getLayerThickness();
            if (ImGui::SliderFloat("Layer Thickness", &layerThickness, 0.1f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                renderer.setLayerThickness(layerThickness);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Vertical extent (0.1 = paper thin, 500 = massive fog bank)");
            }

            float layerDensity = renderer.getLayerDensity();
            if (ImGui::SliderFloat("Layer Density", &layerDensity, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
                renderer.setLayerDensity(layerDensity);
                cachedLayerDensity = layerDensity;  // Update cache when manually changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 = invisible, 1 = completely opaque (logarithmic)");
            }

            // Quick presets
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::Button("Valley##layer")) {
                renderer.setLayerHeight(20.0f);
                renderer.setLayerThickness(30.0f);
                renderer.setLayerDensity(0.03f);
                cachedLayerDensity = 0.03f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Thick Mist##layer")) {
                renderer.setLayerHeight(10.0f);
                renderer.setLayerThickness(15.0f);
                renderer.setLayerDensity(0.1f);
                cachedLayerDensity = 0.1f;
            }
        }
    } else {
        ImGui::TextDisabled("Enable Froxel Fog to access height fog settings");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== ATMOSPHERIC SCATTERING ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
    ImGui::Text("ATMOSPHERIC SCATTERING");
    ImGui::PopStyleColor();

    AtmosphereParams atmosParams = renderer.getAtmosphereParams();
    bool atmosChanged = false;

    // Enable toggle for atmospheric scattering
    if (ImGui::Checkbox("Enable Atmosphere", &atmosphereEnabled)) {
        if (atmosphereEnabled) {
            // Restore cached values
            atmosParams.rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f) * (cachedRayleighScale / 13.558f);
            atmosParams.mieScatteringBase = cachedMieScale / 1000.0f;
            atmosChanged = true;
        } else {
            // Cache current values and zero out scattering
            cachedRayleighScale = atmosParams.rayleighScatteringBase.y * 1000.0f;
            cachedMieScale = atmosParams.mieScatteringBase * 1000.0f;
            if (cachedRayleighScale < 0.001f) cachedRayleighScale = 13.558f;
            if (cachedMieScale < 0.001f) cachedMieScale = 3.996f;
            atmosParams.rayleighScatteringBase = glm::vec3(0.0f);
            atmosParams.mieScatteringBase = 0.0f;
            atmosParams.mieAbsorptionBase = 0.0f;
            atmosParams.ozoneAbsorption = glm::vec3(0.0f);
            atmosChanged = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle sky scattering (Rayleigh blue sky, Mie haze)");
    }

    if (atmosphereEnabled) {
        // Rayleigh scattering (blue sky) - wide ranges for extreme testing
        ImGui::Text("Rayleigh Scattering (Air):");
        float rayleighScale = atmosParams.rayleighScatteringBase.y * 1000.0f;  // Scale for UI
        if (ImGui::SliderFloat("Rayleigh Strength", &rayleighScale, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            float oldVal = atmosParams.rayleighScatteringBase.y * 1000.0f;
            if (oldVal > 0.0001f) {
                float ratio = rayleighScale / oldVal;
                atmosParams.rayleighScatteringBase *= ratio;
            } else {
                // If starting from near zero, set to Earth-like ratio
                atmosParams.rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f) * (rayleighScale / 13.558f);
            }
            cachedRayleighScale = rayleighScale;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no blue sky, 13.5 = Earth, 200 = extremely blue (logarithmic)");
        }

        if (ImGui::SliderFloat("Rayleigh Scale Height", &atmosParams.rayleighScaleHeight, 0.1f, 100.0f, "%.1f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.1 = thin atmosphere, 8 = Earth, 100 = very thick");
        }

        // Mie scattering (haze/sun halo) - wide ranges
        ImGui::Spacing();
        ImGui::Text("Mie Scattering (Haze):");
        float mieScale = atmosParams.mieScatteringBase * 1000.0f;
        if (ImGui::SliderFloat("Mie Strength", &mieScale, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            atmosParams.mieScatteringBase = mieScale / 1000.0f;
            cachedMieScale = mieScale;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no haze, 4 = Earth, 200 = dense smog (logarithmic)");
        }

        if (ImGui::SliderFloat("Mie Scale Height", &atmosParams.mieScaleHeight, 0.01f, 50.0f, "%.2f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.01 = ground-level only, 1.2 = Earth, 50 = everywhere");
        }

        if (ImGui::SliderFloat("Mie Anisotropy", &atmosParams.mieAnisotropy, -0.99f, 0.99f, "%.2f")) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("-1 = backward scatter, 0 = uniform, 0.8 = Earth (forward), 0.99 = laser-like sun");
        }

        float mieAbsScale = atmosParams.mieAbsorptionBase * 1000.0f;
        if (ImGui::SliderFloat("Mie Absorption", &mieAbsScale, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            atmosParams.mieAbsorptionBase = mieAbsScale / 1000.0f;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no absorption, 4.4 = Earth, 100 = heavy smog");
        }

        // Ozone (affects horizon color) - wide ranges
        ImGui::Spacing();
        ImGui::Text("Ozone Layer:");
        float ozoneScale = atmosParams.ozoneAbsorption.y * 1000.0f;
        if (ImGui::SliderFloat("Ozone Strength", &ozoneScale, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            float oldVal = atmosParams.ozoneAbsorption.y * 1000.0f;
            if (oldVal > 0.0001f) {
                float ratio = ozoneScale / oldVal;
                atmosParams.ozoneAbsorption *= ratio;
            } else {
                // If starting from near zero, set to Earth-like ratio
                atmosParams.ozoneAbsorption = glm::vec3(0.65e-3f, 1.881e-3f, 0.085e-3f) * (ozoneScale / 1.881f);
            }
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no ozone, 1.9 = Earth, 50 = extreme orange sunsets");
        }

        if (ImGui::SliderFloat("Ozone Center", &atmosParams.ozoneLayerCenter, 0.0f, 100.0f, "%.0f km")) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = at surface, 25 = Earth, 100 = very high");
        }

        if (ImGui::SliderFloat("Ozone Width", &atmosParams.ozoneLayerWidth, 0.1f, 100.0f, "%.1f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.1 = thin band, 15 = Earth, 100 = everywhere");
        }

        // Quick presets
        ImGui::Spacing();
        ImGui::Text("Presets:");
        if (ImGui::Button("Earth##atmos")) {
            AtmosphereParams earth;
            renderer.setAtmosphereParams(earth);
            cachedRayleighScale = 13.558f;
            cachedMieScale = 3.996f;
            atmosChanged = false;  // Already set
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##atmos")) {
            AtmosphereParams clear;
            clear.mieScatteringBase = 1.0e-3f;
            clear.mieAbsorptionBase = 1.0e-3f;
            renderer.setAtmosphereParams(clear);
            cachedMieScale = 1.0f;
            atmosChanged = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hazy##atmos")) {
            AtmosphereParams hazy;
            hazy.mieScatteringBase = 15.0e-3f;
            hazy.mieAbsorptionBase = 10.0e-3f;
            hazy.mieAnisotropy = 0.7f;
            renderer.setAtmosphereParams(hazy);
            cachedMieScale = 15.0f;
            atmosChanged = false;
        }
    }

    if (atmosChanged) {
        renderer.setAtmosphereParams(atmosParams);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Leaf system
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("FALLING LEAVES");
    ImGui::PopStyleColor();

    float leafIntensity = renderer.getLeafIntensity();
    if (ImGui::SliderFloat("Leaf Intensity", &leafIntensity, 0.0f, 1.0f)) {
        renderer.setLeafIntensity(leafIntensity);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cloud style
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.7f, 1.0f));
    ImGui::Text("CLOUDS");
    ImGui::PopStyleColor();

    bool paraboloid = renderer.isUsingParaboloidClouds();
    if (ImGui::Checkbox("Paraboloid LUT Clouds", &paraboloid)) {
        renderer.toggleCloudStyle();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle between procedural and paraboloid LUT hybrid cloud rendering");
    }

    // Cloud coverage and density controls
    float cloudCoverage = renderer.getCloudCoverage();
    if (ImGui::SliderFloat("Cloud Coverage", &cloudCoverage, 0.0f, 1.0f, "%.2f")) {
        renderer.setCloudCoverage(cloudCoverage);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = clear sky, 0.5 = partly cloudy, 1 = overcast");
    }

    float cloudDensity = renderer.getCloudDensity();
    if (ImGui::SliderFloat("Cloud Density", &cloudDensity, 0.0f, 1.0f, "%.2f")) {
        renderer.setCloudDensity(cloudDensity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = thin/wispy, 0.3 = normal, 1 = thick/opaque");
    }

    // Cloud presets
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Clear##clouds")) {
        renderer.setCloudCoverage(0.0f);
        renderer.setCloudDensity(0.3f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Partly##clouds")) {
        renderer.setCloudCoverage(0.4f);
        renderer.setCloudDensity(0.3f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cloudy##clouds")) {
        renderer.setCloudCoverage(0.7f);
        renderer.setCloudDensity(0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Overcast##clouds")) {
        renderer.setCloudCoverage(0.95f);
        renderer.setCloudDensity(0.7f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Grass interaction
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("GRASS INTERACTION");
    ImGui::PopStyleColor();

    auto& env = renderer.getEnvironmentSettings();
    if (ImGui::SliderFloat("Displacement Decay", &env.grassDisplacementDecay, 0.1f, 5.0f)) {}
    if (ImGui::SliderFloat("Max Displacement", &env.grassMaxDisplacement, 0.0f, 2.0f)) {}
}

void GuiSystem::renderPostProcessSection(Renderer& renderer) {
    ImGui::Spacing();

    // HDR Tonemapping toggle
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
    ImGui::Text("HDR PIPELINE");
    ImGui::PopStyleColor();

    bool hdrEnabled = renderer.isHDREnabled();
    if (ImGui::Checkbox("HDR Tonemapping", &hdrEnabled)) {
        renderer.setHDREnabled(hdrEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable ACES tonemapping and exposure control");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cloud shadows toggle
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("CLOUD SHADOWS");
    ImGui::PopStyleColor();

    bool cloudShadowEnabled = renderer.isCloudShadowEnabled();
    if (ImGui::Checkbox("Cloud Shadows", &cloudShadowEnabled)) {
        renderer.setCloudShadowEnabled(cloudShadowEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable cloud shadow projection on terrain");
    }

    if (cloudShadowEnabled) {
        float cloudShadowIntensity = renderer.getCloudShadowIntensity();
        if (ImGui::SliderFloat("Shadow Intensity", &cloudShadowIntensity, 0.0f, 1.0f)) {
            renderer.setCloudShadowIntensity(cloudShadowIntensity);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("BLOOM");
    ImGui::PopStyleColor();

    ImGui::TextDisabled("Bloom is enabled by default");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.6f, 1.0f));
    ImGui::Text("GOD RAYS");
    ImGui::PopStyleColor();

    ImGui::TextDisabled("God rays follow sun position");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("EXPOSURE");
    ImGui::PopStyleColor();

    ImGui::TextDisabled("Auto-exposure is active");
    ImGui::TextDisabled("Histogram-based adaptation");
}

void GuiSystem::renderTerrainSection(Renderer& renderer) {
    ImGui::Spacing();

    // Terrain info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 0.6f, 1.0f));
    ImGui::Text("TERRAIN SYSTEM");
    ImGui::PopStyleColor();

    const auto& terrain = renderer.getTerrainSystem();
    const auto& config = terrain.getConfig();

    ImGui::Text("Size: %.0f x %.0f meters", config.size, config.size);
    ImGui::Text("Height Scale: %.1f", config.heightScale);

    // Triangle count with color coding
    uint32_t triangleCount = renderer.getTerrainNodeCount();
    ImVec4 triColor = triangleCount < 100000 ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                      triangleCount < 500000 ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                               ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
    ImGui::Text("Triangles:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, triColor);
    if (triangleCount >= 1000000) {
        ImGui::Text("%.2fM", triangleCount / 1000000.0f);
    } else if (triangleCount >= 1000) {
        ImGui::Text("%.1fK", triangleCount / 1000.0f);
    } else {
        ImGui::Text("%u", triangleCount);
    }
    ImGui::PopStyleColor();

    // CBT depth info
    ImGui::Text("Max Depth: %d (min edge: %.1fm)", config.maxDepth,
                config.size / (1 << (config.maxDepth / 2)));
    ImGui::Text("Min Depth: %d", config.minDepth);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // LOD parameters (modifiable at runtime)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("LOD PARAMETERS");
    ImGui::PopStyleColor();

    auto& terrainMut = renderer.getTerrainSystem();
    TerrainConfig cfg = terrainMut.getConfig();
    bool configChanged = false;

    if (ImGui::SliderFloat("Split Threshold", &cfg.splitThreshold, 1.0f, 256.0f, "%.0f px")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Screen-space edge length (pixels) to trigger subdivision");
    }

    if (ImGui::SliderFloat("Merge Threshold", &cfg.mergeThreshold, 1.0f, 256.0f, "%.0f px")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Screen-space edge length (pixels) to trigger merge");
    }

    if (ImGui::SliderFloat("Flatness Scale", &cfg.flatnessScale, 0.0f, 5.0f, "%.1f")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Curvature LOD: 0=disabled, 2=flat areas use 3x threshold");
    }

    int maxDepth = cfg.maxDepth;
    if (ImGui::SliderInt("Max Depth", &maxDepth, 16, 32)) {
        cfg.maxDepth = maxDepth;
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum subdivision depth (higher = finer detail, more triangles)");
    }

    int minDepth = cfg.minDepth;
    if (ImGui::SliderInt("Min Depth", &minDepth, 1, 10)) {
        cfg.minDepth = minDepth;
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimum subdivision depth (base tessellation level)");
    }

    int spreadFactor = static_cast<int>(cfg.spreadFactor);
    if (ImGui::SliderInt("Spread Factor", &spreadFactor, 1, 32)) {
        cfg.spreadFactor = static_cast<uint32_t>(spreadFactor);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Temporal spreading: process 1/N triangles per frame (1 = all, higher = less GPU work per frame)");
    }

    if (configChanged) {
        terrainMut.setConfig(cfg);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Wireframe toggle
    bool wireframe = renderer.isTerrainWireframeMode();
    if (ImGui::Checkbox("Wireframe Mode", &wireframe)) {
        renderer.toggleTerrainWireframe();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Meshlet rendering
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.9f, 1.0f));
    ImGui::Text("MESHLET RENDERING");
    ImGui::PopStyleColor();

    bool meshletsEnabled = terrainMut.isMeshletsEnabled();
    if (ImGui::Checkbox("Enable Meshlets", &meshletsEnabled)) {
        terrainMut.setMeshletsEnabled(meshletsEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use pre-tessellated meshlets per CBT leaf for higher resolution");
    }

    if (meshletsEnabled) {
        int meshletLevel = terrainMut.getMeshletSubdivisionLevel();
        if (ImGui::SliderInt("Meshlet Level", &meshletLevel, 0, 6)) {
            terrainMut.setMeshletSubdivisionLevel(meshletLevel);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Subdivision level per meshlet (0=1, 1=4, 2=16, 3=64, 4=256 triangles)");
        }

        uint32_t meshletTris = terrainMut.getMeshletTriangleCount();
        ImGui::Text("Triangles per leaf: %u", meshletTris);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Optimization toggles
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.8f, 1.0f));
    ImGui::Text("OPTIMIZATIONS");
    ImGui::PopStyleColor();

    bool skipFrameOpt = terrainMut.isSkipFrameOptimizationEnabled();
    if (ImGui::Checkbox("Skip-Frame (Camera Still)", &skipFrameOpt)) {
        terrainMut.setSkipFrameOptimization(skipFrameOpt);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Skip subdivision compute when camera is stationary");
    }

    bool gpuCulling = terrainMut.isGpuCullingEnabled();
    if (ImGui::Checkbox("GPU Frustum Culling", &gpuCulling)) {
        terrainMut.setGpuCulling(gpuCulling);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use GPU frustum culling with stream compaction for split phase");
    }

    // Debug status (phase alternates every frame so not useful to display)
    ImGui::Text("Status: %s", terrainMut.isCurrentlySkipping() ? "SKIPPING" : "ACTIVE");

    ImGui::Spacing();

    // Height query demo
    ImGui::Text("Height at origin: %.2f", renderer.getTerrainHeightAt(0.0f, 0.0f));
}

void GuiSystem::renderWaterSection(Renderer& renderer) {
    ImGui::Spacing();

    auto& water = renderer.getWaterSystem();

    // Water info header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("WATER SYSTEM");
    ImGui::PopStyleColor();

    ImGui::Text("Current Level: %.2f m", water.getWaterLevel());
    ImGui::Text("Base Level: %.2f m", water.getBaseWaterLevel());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Water level controls
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("LEVEL & TIDES");
    ImGui::PopStyleColor();

    float baseLevel = water.getBaseWaterLevel();
    if (ImGui::SliderFloat("Base Water Level", &baseLevel, -50.0f, 50.0f, "%.1f m")) {
        water.setWaterLevel(baseLevel);
    }

    float tidalRange = water.getTidalRange();
    if (ImGui::SliderFloat("Tidal Range", &tidalRange, 0.0f, 10.0f, "%.1f m")) {
        water.setTidalRange(tidalRange);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum tide variation from base level");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Wave parameters
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("WAVES");
    ImGui::PopStyleColor();

    float amplitude = water.getWaveAmplitude();
    if (ImGui::SliderFloat("Amplitude", &amplitude, 0.0f, 5.0f, "%.2f m")) {
        water.setWaveAmplitude(amplitude);
    }

    float wavelength = water.getWaveLength();
    if (ImGui::SliderFloat("Wavelength", &wavelength, 1.0f, 100.0f, "%.1f m")) {
        water.setWaveLength(wavelength);
    }

    float steepness = water.getWaveSteepness();
    if (ImGui::SliderFloat("Steepness", &steepness, 0.0f, 1.0f, "%.2f")) {
        water.setWaveSteepness(steepness);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Wave sharpness (0=sine, 1=peaked)");
    }

    float speed = water.getWaveSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.0f, 3.0f, "%.2f")) {
        water.setWaveSpeed(speed);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Appearance
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("APPEARANCE");
    ImGui::PopStyleColor();

    glm::vec4 waterColor = water.getWaterColor();
    float col[4] = {waterColor.r, waterColor.g, waterColor.b, waterColor.a};
    if (ImGui::ColorEdit4("Water Color", col)) {
        water.setWaterColor(glm::vec4(col[0], col[1], col[2], col[3]));
    }

    float foam = water.getFoamThreshold();
    if (ImGui::SliderFloat("Foam Threshold", &foam, 0.0f, 2.0f, "%.2f")) {
        water.setFoamThreshold(foam);
    }

    float fresnel = water.getFresnelPower();
    if (ImGui::SliderFloat("Fresnel Power", &fresnel, 1.0f, 10.0f, "%.1f")) {
        water.setFresnelPower(fresnel);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Controls reflection intensity at grazing angles");
    }

    // Shore effects
    ImGui::Spacing();
    ImGui::Text("Shore Effects:");

    float shoreBlend = water.getShoreBlendDistance();
    if (ImGui::SliderFloat("Shore Blend", &shoreBlend, 0.5f, 10.0f, "%.1f m")) {
        water.setShoreBlendDistance(shoreBlend);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance over which water fades near shore");
    }

    float shoreFoam = water.getShoreFoamWidth();
    if (ImGui::SliderFloat("Shore Foam Width", &shoreFoam, 1.0f, 20.0f, "%.1f m")) {
        water.setShoreFoamWidth(shoreFoam);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Width of foam bands along the shoreline");
    }

    // Presets
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Presets:");
    if (ImGui::Button("Ocean")) {
        water.setWaterColor(glm::vec4(0.02f, 0.08f, 0.15f, 0.95f));
        water.setWaveAmplitude(1.5f);
        water.setWaveLength(30.0f);
        water.setWaveSteepness(0.4f);
        water.setWaveSpeed(0.8f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Lake")) {
        water.setWaterColor(glm::vec4(0.05f, 0.12f, 0.18f, 0.9f));
        water.setWaveAmplitude(0.3f);
        water.setWaveLength(8.0f);
        water.setWaveSteepness(0.2f);
        water.setWaveSpeed(0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Calm")) {
        water.setWaterColor(glm::vec4(0.03f, 0.1f, 0.2f, 0.85f));
        water.setWaveAmplitude(0.1f);
        water.setWaveLength(5.0f);
        water.setWaveSteepness(0.1f);
        water.setWaveSpeed(0.3f);
    }
    if (ImGui::Button("Storm")) {
        water.setWaterColor(glm::vec4(0.04f, 0.06f, 0.1f, 0.98f));
        water.setWaveAmplitude(3.0f);
        water.setWaveLength(20.0f);
        water.setWaveSteepness(0.6f);
        water.setWaveSpeed(1.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Tropical")) {
        water.setWaterColor(glm::vec4(0.0f, 0.15f, 0.2f, 0.8f));
        water.setWaveAmplitude(0.5f);
        water.setWaveLength(12.0f);
        water.setWaveSteepness(0.3f);
        water.setWaveSpeed(0.6f);
    }
}

void GuiSystem::renderDebugSection(Renderer& renderer) {
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("DEBUG VISUALIZATIONS");
    ImGui::PopStyleColor();

    bool cascadeDebug = renderer.isShowingCascadeDebug();
    if (ImGui::Checkbox("Shadow Cascade Debug", &cascadeDebug)) {
        renderer.toggleCascadeDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows colored overlay for each shadow cascade");
    }

    bool snowDepthDebug = renderer.isShowingSnowDepthDebug();
    if (ImGui::Checkbox("Snow Depth Debug", &snowDepthDebug)) {
        renderer.toggleSnowDepthDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows snow accumulation depth as heat map");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("OCCLUSION CULLING");
    ImGui::PopStyleColor();

    bool hiZEnabled = renderer.isHiZCullingEnabled();
    if (ImGui::Checkbox("Hi-Z Occlusion Culling", &hiZEnabled)) {
        renderer.setHiZCullingEnabled(hiZEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable hierarchical Z-buffer occlusion culling (8 key)");
    }

    // Display culling statistics
    auto stats = renderer.getHiZCullingStats();
    ImGui::Text("Total Objects: %u", stats.totalObjects);
    ImGui::Text("Visible: %u", stats.visibleObjects);
    ImGui::Text("Frustum Culled: %u", stats.frustumCulled);
    ImGui::Text("Occlusion Culled: %u", stats.occlusionCulled);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("SYSTEM INFO");
    ImGui::PopStyleColor();

    ImGui::Text("Renderer: Vulkan");
    ImGui::Text("Shadow Cascades: 4");
    ImGui::Text("Shadow Map Size: 2048");
    ImGui::Text("Max Frames in Flight: 2");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Keyboard shortcuts reference
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("KEYBOARD SHORTCUTS");
    ImGui::PopStyleColor();

    ImGui::BulletText("F1 - Toggle GUI");
    ImGui::BulletText("F2 - Tree Editor");
    ImGui::BulletText("P - Place tree at camera");
    ImGui::BulletText("Tab - Toggle camera mode");
    ImGui::BulletText("1-4 - Time presets");
    ImGui::BulletText("+/- - Time scale");
    ImGui::BulletText("C - Cycle weather");
    ImGui::BulletText("Z/X - Weather intensity");
    ImGui::BulletText(",/. - Snow amount");
    ImGui::BulletText("T - Terrain wireframe");
    ImGui::BulletText("6 - Cascade debug");
    ImGui::BulletText("7 - Snow depth debug");
    ImGui::BulletText("8 - Hi-Z culling toggle");
    ImGui::BulletText("[ ] - Fog density");
    ImGui::BulletText("\\ - Toggle fog");
    ImGui::BulletText("F - Spawn confetti");
}

void GuiSystem::renderProfilerSection(Renderer& renderer) {
    ImGui::Spacing();

    auto& profiler = renderer.getProfiler();

    // Enable/disable toggle
    bool enabled = profiler.isEnabled();
    if (ImGui::Checkbox("Enable Profiling", &enabled)) {
        profiler.setEnabled(enabled);
    }

    if (!enabled) {
        ImGui::TextDisabled("Profiling disabled");
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // GPU Profiling Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("GPU TIMING");
    ImGui::PopStyleColor();

    const auto& gpuStats = profiler.getSmoothedGpuResults();

    if (gpuStats.zones.empty()) {
        ImGui::TextDisabled("No GPU data yet (waiting for frames)");
    } else {
        // Total GPU time
        ImGui::Text("Total GPU: %.2f ms", gpuStats.totalGpuTimeMs);

        ImGui::Spacing();

        // GPU timing breakdown table
        if (ImGui::BeginTable("GPUTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto& zone : gpuStats.zones) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", zone.name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%.2f", zone.gpuTimeMs);

                ImGui::TableNextColumn();
                // Color code by percentage
                if (zone.percentOfFrame > 30.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                } else if (zone.percentOfFrame > 15.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                }
                ImGui::Text("%.1f%%", zone.percentOfFrame);
                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }

        // Visual bar chart of GPU zones
        ImGui::Spacing();
        float maxTime = gpuStats.totalGpuTimeMs;
        for (const auto& zone : gpuStats.zones) {
            float fraction = (maxTime > 0.0f) ? (zone.gpuTimeMs / maxTime) : 0.0f;
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), zone.name.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // CPU Profiling Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("CPU TIMING");
    ImGui::PopStyleColor();

    const auto& cpuStats = profiler.getSmoothedCpuResults();

    if (cpuStats.zones.empty()) {
        ImGui::TextDisabled("No CPU data yet");
    } else {
        // Total CPU time
        ImGui::Text("Total CPU: %.2f ms", cpuStats.totalCpuTimeMs);

        ImGui::Spacing();

        // CPU timing breakdown table
        if (ImGui::BeginTable("CPUTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto& zone : cpuStats.zones) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", zone.name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", zone.cpuTimeMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", zone.percentOfFrame);
            }

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Frame budget indicator
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("FRAME BUDGET");
    ImGui::PopStyleColor();

    float targetMs = 16.67f;  // 60 FPS target
    float gpuTime = gpuStats.totalGpuTimeMs;
    float cpuTime = cpuStats.totalCpuTimeMs;
    float maxTime = std::max(gpuTime, cpuTime);

    // Budget bar
    float budgetUsed = maxTime / targetMs;
    ImVec4 budgetColor;
    if (budgetUsed < 0.8f) {
        budgetColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  // Green
    } else if (budgetUsed < 1.0f) {
        budgetColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);  // Yellow
    } else {
        budgetColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
    }

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, budgetColor);
    char budgetText[64];
    snprintf(budgetText, sizeof(budgetText), "%.1f / %.1f ms (%.0f%%)",
             maxTime, targetMs, budgetUsed * 100.0f);
    ImGui::ProgressBar(std::min(budgetUsed, 1.5f) / 1.5f, ImVec2(-1, 20), budgetText);
    ImGui::PopStyleColor();

    ImGui::Text("GPU Bound: %s", (gpuTime > cpuTime) ? "Yes" : "No");
    ImGui::Text("CPU Bound: %s", (cpuTime > gpuTime) ? "Yes" : "No");
}

void GuiSystem::renderHelpOverlay() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                   ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);

    if (ImGui::Begin("Help", &showHelp, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("VULKAN GAME ENGINE");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("CAMERA CONTROLS");
        ImGui::BulletText("Free Camera: WASD + Arrow keys");
        ImGui::BulletText("Third Person: WASD moves player");
        ImGui::BulletText("Space: Jump (3rd person) / Up (free cam)");
        ImGui::BulletText("Tab: Switch camera mode");

        ImGui::Spacing();
        ImGui::Text("GAMEPAD CONTROLS");
        ImGui::BulletText("Left Stick: Move");
        ImGui::BulletText("Right Stick: Look / Orbit");
        ImGui::BulletText("A/B/X/Y: Time presets");
        ImGui::BulletText("Bumpers: Up/Down or Zoom");

        ImGui::Spacing();
        ImGui::Text("GUI");
        ImGui::BulletText("F1: Toggle this panel");
        ImGui::BulletText("Click and drag sliders");
        ImGui::BulletText("Ctrl+Click for precise input");

        ImGui::Spacing();
        if (ImGui::Button("Close (H)", ImVec2(-1, 0))) {
            showHelp = false;
        }
    }
    ImGui::End();
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

void GuiSystem::renderIKSection(Renderer& renderer, const Camera& camera) {
    ImGui::Spacing();

    // Check if character is loaded
    auto& sceneBuilder = renderer.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        ImGui::TextDisabled("No animated character loaded");
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();
    auto& ikSystem = character.getIKSystem();

    // Debug Visualization section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("DEBUG VISUALIZATION");
    ImGui::PopStyleColor();

    ImGui::Checkbox("Show Skeleton", &ikDebugSettings.showSkeleton);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw wireframe skeleton bones");
    }

    ImGui::Checkbox("Show IK Targets", &ikDebugSettings.showIKTargets);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw IK target positions and pole vectors");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Look-At IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("LOOK-AT IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Look-At", &ikDebugSettings.lookAtEnabled)) {
        ikSystem.setLookAtEnabled(ikDebugSettings.lookAtEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Character head/neck tracks a target");
    }

    if (ikDebugSettings.lookAtEnabled) {
        ImGui::Indent();

        const char* modeNames[] = { "Fixed Point", "Camera Position", "Mouse (Screen)" };
        int currentMode = static_cast<int>(ikDebugSettings.lookAtMode);
        if (ImGui::Combo("Target Mode", &currentMode, modeNames, 3)) {
            ikDebugSettings.lookAtMode = static_cast<IKDebugSettings::LookAtMode>(currentMode);
        }

        if (ikDebugSettings.lookAtMode == IKDebugSettings::LookAtMode::Fixed) {
            ImGui::DragFloat3("Target Position", &ikDebugSettings.fixedLookAtTarget.x, 0.1f);
        }

        // Update look-at target based on mode
        glm::vec3 lookTarget;
        switch (ikDebugSettings.lookAtMode) {
            case IKDebugSettings::LookAtMode::Fixed:
                lookTarget = ikDebugSettings.fixedLookAtTarget;
                break;
            case IKDebugSettings::LookAtMode::Camera:
                lookTarget = camera.getPosition();
                break;
            case IKDebugSettings::LookAtMode::Mouse:
                // Project mouse into world (simplified - uses camera front direction)
                lookTarget = camera.getPosition() + camera.getFront() * 5.0f;
                break;
        }
        ikSystem.setLookAtTarget(lookTarget);

        float lookAtWeight = 1.0f;
        if (ImGui::SliderFloat("Weight", &lookAtWeight, 0.0f, 1.0f)) {
            ikSystem.setLookAtWeight(lookAtWeight);
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Foot Placement IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("FOOT PLACEMENT IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Foot Placement", &ikDebugSettings.footPlacementEnabled)) {
        // Enable/disable both feet
        ikSystem.setFootPlacementEnabled("LeftFoot", ikDebugSettings.footPlacementEnabled);
        ikSystem.setFootPlacementEnabled("RightFoot", ikDebugSettings.footPlacementEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Feet adapt to terrain height");
    }

    if (ikDebugSettings.footPlacementEnabled) {
        ImGui::Indent();

        if (ImGui::SliderFloat("Ground Offset", &ikDebugSettings.groundOffset, -0.2f, 0.2f)) {
            // Ground offset adjustment
        }

        // Show foot placement debug info
        ImGui::TextDisabled("Left Foot: Active");
        ImGui::TextDisabled("Right Foot: Active");

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Straddle IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.8f, 1.0f));
    ImGui::Text("STRADDLE IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Straddling", &ikDebugSettings.straddleEnabled)) {
        ikSystem.setStraddleEnabled(ikDebugSettings.straddleEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hip tilt when feet at different heights");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // IK Chain Info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("IK CHAINS");
    ImGui::PopStyleColor();

    // Show configured chains
    const auto& skeleton = character.getSkeleton();
    ImGui::Text("Skeleton Bones: %zu", skeleton.joints.size());

    // Two-bone chains
    if (auto* chain = ikSystem.getChain("LeftArm")) {
        ImGui::BulletText("Left Arm: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("RightArm")) {
        ImGui::BulletText("Right Arm: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("LeftLeg")) {
        ImGui::BulletText("Left Leg: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("RightLeg")) {
        ImGui::BulletText("Right Leg: %s", chain->enabled ? "Enabled" : "Disabled");
    }
}

void GuiSystem::renderSkeletonOverlay(Renderer& renderer, const Camera& camera) {
    auto& sceneBuilder = renderer.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();

    // Get the character's world transform from the scene object
    const auto& sceneObjects = sceneBuilder.getRenderables();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    if (playerIndex >= sceneObjects.size()) {
        return;
    }

    glm::mat4 worldTransform = sceneObjects[playerIndex].transform;

    // Get viewport size
    float width = static_cast<float>(renderer.getWidth());
    float height = static_cast<float>(renderer.getHeight());

    // Get view-projection matrix
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();

    // Helper to project world position to screen
    auto worldToScreen = [&](const glm::vec3& worldPos) -> ImVec2 {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w <= 0.0f) {
            return ImVec2(-1000, -1000);  // Behind camera
        }
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        float screenX = (ndc.x * 0.5f + 0.5f) * width;
        // Vulkan projection already flips Y (proj[1][1] *= -1), so NDC Y is already
        // in screen orientation (negative = up, positive = down). Just map to pixels.
        float screenY = (ndc.y * 0.5f + 0.5f) * height;
        return ImVec2(screenX, screenY);
    };

    // Get the background draw list (renders behind everything)
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Draw skeleton if enabled
    if (ikDebugSettings.showSkeleton) {
        SkeletonDebugData skelData = character.getSkeletonDebugData(worldTransform);

        // Draw bones as lines
        for (const auto& bone : skelData.bones) {
            if (bone.parentIndex < 0) continue;  // Skip root

            ImVec2 startScreen = worldToScreen(bone.startPos);
            ImVec2 endScreen = worldToScreen(bone.endPos);

            // Skip if behind camera
            if (startScreen.x < -500 || endScreen.x < -500) continue;

            // Bone color - cyan for normal, yellow for end effectors
            ImU32 boneColor = bone.isEndEffector ?
                IM_COL32(255, 255, 100, 200) :
                IM_COL32(100, 255, 255, 200);

            drawList->AddLine(startScreen, endScreen, boneColor, 2.0f);
        }

        // Draw joint positions as circles
        for (const auto& pos : skelData.jointPositions) {
            ImVec2 screenPos = worldToScreen(pos);
            if (screenPos.x < -500) continue;

            drawList->AddCircleFilled(screenPos, 4.0f, IM_COL32(255, 100, 100, 255));
        }
    }

    // Draw IK targets if enabled
    if (ikDebugSettings.showIKTargets) {
        IKDebugData ikData = character.getIKDebugData();

        // Transform IK positions to world space and draw
        for (const auto& chain : ikData.chains) {
            if (!chain.active) continue;

            // Chain positions are already in skeleton local space
            glm::vec4 targetWorld = worldTransform * glm::vec4(chain.targetPos, 1.0f);
            glm::vec4 poleWorld = worldTransform * glm::vec4(chain.polePos, 1.0f);
            glm::vec4 endWorld = worldTransform * glm::vec4(chain.endPos, 1.0f);

            ImVec2 targetScreen = worldToScreen(glm::vec3(targetWorld));
            ImVec2 poleScreen = worldToScreen(glm::vec3(poleWorld));
            ImVec2 endScreen = worldToScreen(glm::vec3(endWorld));

            // Draw target as green cross
            if (targetScreen.x > -500) {
                drawList->AddLine(
                    ImVec2(targetScreen.x - 8, targetScreen.y),
                    ImVec2(targetScreen.x + 8, targetScreen.y),
                    IM_COL32(100, 255, 100, 255), 2.0f);
                drawList->AddLine(
                    ImVec2(targetScreen.x, targetScreen.y - 8),
                    ImVec2(targetScreen.x, targetScreen.y + 8),
                    IM_COL32(100, 255, 100, 255), 2.0f);
            }

            // Draw pole vector as blue diamond
            if (poleScreen.x > -500) {
                drawList->AddQuadFilled(
                    ImVec2(poleScreen.x, poleScreen.y - 6),
                    ImVec2(poleScreen.x + 6, poleScreen.y),
                    ImVec2(poleScreen.x, poleScreen.y + 6),
                    ImVec2(poleScreen.x - 6, poleScreen.y),
                    IM_COL32(100, 100, 255, 200));
            }

            // Draw line from end effector to target
            if (targetScreen.x > -500 && endScreen.x > -500) {
                drawList->AddLine(endScreen, targetScreen, IM_COL32(255, 255, 100, 150), 1.0f);
            }
        }

        // Draw look-at targets
        for (const auto& lookAt : ikData.lookAtTargets) {
            if (!lookAt.active) continue;

            glm::vec4 targetWorld = worldTransform * glm::vec4(lookAt.targetPos, 1.0f);
            glm::vec4 headWorld = worldTransform * glm::vec4(lookAt.headPos, 1.0f);

            ImVec2 targetScreen = worldToScreen(glm::vec3(targetWorld));
            ImVec2 headScreen = worldToScreen(glm::vec3(headWorld));

            // Draw target as magenta circle
            if (targetScreen.x > -500) {
                drawList->AddCircle(targetScreen, 10.0f, IM_COL32(255, 100, 255, 255), 12, 2.0f);
            }

            // Draw line from head to target
            if (targetScreen.x > -500 && headScreen.x > -500) {
                drawList->AddLine(headScreen, targetScreen, IM_COL32(255, 100, 255, 150), 1.0f);
            }
        }

        // Draw foot placement targets
        for (const auto& foot : ikData.footPlacements) {
            if (!foot.active) continue;

            glm::vec4 groundWorld = worldTransform * glm::vec4(foot.groundPos, 1.0f);
            glm::vec4 footWorld = worldTransform * glm::vec4(foot.footPos, 1.0f);

            ImVec2 groundScreen = worldToScreen(glm::vec3(groundWorld));
            ImVec2 footScreen = worldToScreen(glm::vec3(footWorld));

            // Draw ground target as orange square
            if (groundScreen.x > -500) {
                drawList->AddRectFilled(
                    ImVec2(groundScreen.x - 5, groundScreen.y - 5),
                    ImVec2(groundScreen.x + 5, groundScreen.y + 5),
                    IM_COL32(255, 150, 50, 200));
            }

            // Draw line from foot to ground target
            if (groundScreen.x > -500 && footScreen.x > -500) {
                drawList->AddLine(footScreen, groundScreen, IM_COL32(255, 150, 50, 150), 1.0f);
            }
        }
    }
}
