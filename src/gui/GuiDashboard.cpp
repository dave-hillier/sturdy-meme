#include "GuiDashboard.h"
#include "Camera.h"
#include "core/interfaces/ITerrainControl.h"
#include "core/interfaces/ITimeSystem.h"

#include <imgui.h>

void GuiDashboard::render(ITerrainControl& terrain, ITimeSystem& time, const Camera& camera,
                           float deltaTime, float fps, State& state) {
    // Update frame time history
    state.frameTimeHistory[state.frameTimeIndex] = deltaTime * 1000.0f;
    state.frameTimeIndex = (state.frameTimeIndex + 1) % 120;

    // Calculate average
    float sum = 0.0f;
    for (int i = 0; i < 120; i++) {
        sum += state.frameTimeHistory[i];
    }
    state.avgFrameTime = sum / 120.0f;

    // FPS and frame time in columns
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 140);

    ImGui::Text("FPS");
    ImGui::PushStyleColor(ImGuiCol_Text, fps > 55.0f ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                                          fps > 30.0f ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                                        ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
    ImGui::SameLine(60);
    ImGui::Text("%.0f", fps);
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    ImGui::Text("Frame");
    ImGui::SameLine(50);
    ImGui::Text("%.2f ms", state.avgFrameTime);

    ImGui::Columns(1);

    // Frame time graph
    ImGui::PlotLines("##frametime", state.frameTimeHistory, 120, state.frameTimeIndex,
                     nullptr, 0.0f, 33.3f, ImVec2(-1, 35));

    // Quick stats
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 140);

    uint32_t triCount = terrain.getTerrainNodeCount();
    ImGui::Text("Terrain");
    ImGui::SameLine(60);
    if (triCount >= 1000000) {
        ImGui::Text("%.2fM", triCount / 1000000.0f);
    } else if (triCount >= 1000) {
        ImGui::Text("%.0fK", triCount / 1000.0f);
    } else {
        ImGui::Text("%u", triCount);
    }

    ImGui::NextColumn();

    float tod = time.getTimeOfDay();
    int h = static_cast<int>(tod * 24.0f);
    int m = static_cast<int>((tod * 24.0f - h) * 60.0f);
    ImGui::Text("Time");
    ImGui::SameLine(40);
    ImGui::Text("%02d:%02d", h, m);

    ImGui::Columns(1);

    // Camera position
    glm::vec3 pos = camera.getPosition();
    ImGui::Text("Pos: %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
}
