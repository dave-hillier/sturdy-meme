#include "GuiPerformanceTab.h"
#include "core/interfaces/IPerformanceControl.h"
#include "PerformanceToggles.h"

#include <imgui.h>
#include <string>

void GuiPerformanceTab::render(IPerformanceControl& perfControl) {
    ImGui::Spacing();

    PerformanceToggles& toggles = perfControl.getPerformanceToggles();

    // Quick actions
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("QUICK ACTIONS");
    ImGui::PopStyleColor();

    if (ImGui::Button("Enable All")) {
        toggles.enableAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) {
        toggles.disableAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Minimal")) {
        toggles.disableAll();
        toggles.skyDraw = true;
        toggles.terrainDraw = true;
        toggles.sceneObjectsDraw = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Render toggles by category
    auto allToggles = toggles.getAllToggles();
    std::string currentCategory;

    for (auto& t : allToggles) {
        // New category header
        if (t.category != currentCategory) {
            if (!currentCategory.empty()) {
                ImGui::Spacing();
            }

            currentCategory = t.category;

            // Category color based on type
            ImVec4 categoryColor(0.6f, 0.8f, 1.0f, 1.0f);
            if (t.category == "Compute") {
                categoryColor = ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
            } else if (t.category == "HDR Draw") {
                categoryColor = ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
            } else if (t.category == "Shadows") {
                categoryColor = ImVec4(0.6f, 0.6f, 0.8f, 1.0f);
            } else if (t.category == "Post") {
                categoryColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
            } else if (t.category == "Other") {
                categoryColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            } else if (t.category == "Sync") {
                categoryColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, categoryColor);
            ImGui::Text("%s", t.category.c_str());
            ImGui::PopStyleColor();

            // Category enable/disable buttons
            ImGui::SameLine(ImGui::GetWindowWidth() - 120);
            ImGui::PushID(t.category.c_str());
            if (ImGui::SmallButton("All")) {
                toggles.enableCategory(t.category);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("None")) {
                toggles.disableCategory(t.category);
            }
            ImGui::PopID();
        }

        // Individual toggle checkbox
        ImGui::Checkbox(t.name.c_str(), t.value);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Usage hints
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextWrapped("Toggle individual subsystems to isolate performance bottlenecks. "
                       "Start with 'Minimal' and enable systems one by one to find the culprit.");
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Show sync debugging hint
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("SYNC DEBUGGING");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("If disabling a compute pass fixes stuttering, "
                       "check for missing barriers between that pass and dependent draws.");
}
