#include "GuiPositionPanel.h"
#include "Camera.h"

#include <imgui.h>
#include <cmath>

void GuiPositionPanel::render(const Camera& camera) {
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
