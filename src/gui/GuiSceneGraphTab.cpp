#include "GuiSceneGraphTab.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "core/RenderableBuilder.h"
#include "Mesh.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <cmath>

namespace {
    // Extract position from transform matrix
    glm::vec3 extractPosition(const glm::mat4& transform) {
        return glm::vec3(transform[3]);
    }

    // Extract scale (approximate - assumes uniform or near-uniform scale)
    glm::vec3 extractScale(const glm::mat4& transform) {
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(transform[0]));
        scale.y = glm::length(glm::vec3(transform[1]));
        scale.z = glm::length(glm::vec3(transform[2]));
        return scale;
    }

    // Extract Euler angles (approximate - assumes no gimbal lock)
    glm::vec3 extractEulerAngles(const glm::mat4& transform) {
        glm::vec3 scale = extractScale(transform);
        glm::mat3 rotMat(
            glm::vec3(transform[0]) / scale.x,
            glm::vec3(transform[1]) / scale.y,
            glm::vec3(transform[2]) / scale.z
        );

        glm::vec3 euler;
        euler.y = std::asin(-rotMat[2][0]);  // Yaw

        if (std::abs(std::cos(euler.y)) > 0.001f) {
            euler.x = std::atan2(rotMat[2][1], rotMat[2][2]);  // Pitch
            euler.z = std::atan2(rotMat[1][0], rotMat[0][0]);  // Roll
        } else {
            euler.x = std::atan2(-rotMat[1][2], rotMat[1][1]);
            euler.z = 0.0f;
        }

        // Convert to degrees
        return glm::degrees(euler);
    }

    // Get a display name for a renderable based on its properties
    const char* getObjectTypeName(const Renderable& obj, size_t index, size_t playerIndex) {
        if (index == playerIndex) {
            return "Player";
        }
        if (obj.emissiveIntensity > 0.0f) {
            return "Emissive";
        }
        if (obj.treeInstanceIndex >= 0) {
            return "Tree";
        }
        if (obj.leafInstanceIndex >= 0) {
            return "Leaves";
        }
        if (obj.alphaTestThreshold > 0.0f) {
            return "Alpha-Test";
        }
        return "Object";
    }

    // Draw a color preview square
    void drawColorPreview(const glm::vec3& color, float size = 16.0f) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 col = IM_COL32(
            static_cast<int>(color.r * 255),
            static_cast<int>(color.g * 255),
            static_cast<int>(color.b * 255),
            255
        );
        drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col);
        drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(100, 100, 100, 255));
        ImGui::Dummy(ImVec2(size, size));
    }
}

void GuiSceneGraphTab::render(ISceneControl& sceneControl, SceneGraphTabState& state) {
    SceneBuilder& sceneBuilder = sceneControl.getSceneBuilder();
    const auto& renderables = sceneBuilder.getRenderables();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();

    ImGui::Spacing();

    // Header with object count
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("SCENE GRAPH");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu objects)", renderables.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Filter input
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter objects...", state.filterText, sizeof(state.filterText));

    ImGui::Spacing();

    // Object list (left side) - using child window for scroll
    float listHeight = ImGui::GetContentRegionAvail().y * 0.45f;
    if (ImGui::BeginChild("ObjectList", ImVec2(-1, listHeight), true)) {
        for (size_t i = 0; i < renderables.size(); ++i) {
            const Renderable& obj = renderables[i];

            // Build display name
            char displayName[128];
            const char* typeName = getObjectTypeName(obj, i, playerIndex);
            snprintf(displayName, sizeof(displayName), "[%zu] %s", i, typeName);

            // Apply filter
            if (state.filterText[0] != '\0') {
                bool matches = false;
                // Case-insensitive search
                char lowerFilter[256];
                char lowerName[128];
                strncpy(lowerFilter, state.filterText, sizeof(lowerFilter) - 1);
                lowerFilter[sizeof(lowerFilter) - 1] = '\0';
                strncpy(lowerName, displayName, sizeof(lowerName) - 1);
                lowerName[sizeof(lowerName) - 1] = '\0';

                for (char* p = lowerFilter; *p; ++p) *p = static_cast<char>(tolower(*p));
                for (char* p = lowerName; *p; ++p) *p = static_cast<char>(tolower(*p));

                if (strstr(lowerName, lowerFilter) != nullptr) {
                    matches = true;
                }
                // Also match by type name
                char lowerType[64];
                strncpy(lowerType, typeName, sizeof(lowerType) - 1);
                lowerType[sizeof(lowerType) - 1] = '\0';
                for (char* p = lowerType; *p; ++p) *p = static_cast<char>(tolower(*p));
                if (strstr(lowerType, lowerFilter) != nullptr) {
                    matches = true;
                }

                if (!matches) continue;
            }

            bool isSelected = (state.selectedObjectIndex == static_cast<int>(i));

            // Color code by type
            ImVec4 itemColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);  // Default white
            if (i == playerIndex) {
                itemColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);  // Green for player
            } else if (obj.emissiveIntensity > 0.0f) {
                itemColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Yellow for emissive
            } else if (obj.treeInstanceIndex >= 0 || obj.leafInstanceIndex >= 0) {
                itemColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);  // Light green for trees
            } else if (!obj.castsShadow) {
                itemColor = ImVec4(0.6f, 0.6f, 0.8f, 1.0f);  // Blue-ish for non-shadow casters
            }

            ImGui::PushStyleColor(ImGuiCol_Text, itemColor);

            if (ImGui::Selectable(displayName, isSelected)) {
                state.selectedObjectIndex = static_cast<int>(i);
            }

            ImGui::PopStyleColor();

            // Tooltip with quick info
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                glm::vec3 pos = extractPosition(obj.transform);
                ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
                ImGui::Text("Material ID: %u", obj.materialId);
                if (obj.emissiveIntensity > 0.0f) {
                    ImGui::Text("Emissive: %.2f", obj.emissiveIntensity);
                }
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Properties panel for selected object
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("PROPERTIES");
    ImGui::PopStyleColor();

    ImGui::Spacing();

    if (state.selectedObjectIndex >= 0 && state.selectedObjectIndex < static_cast<int>(renderables.size())) {
        const Renderable& selected = renderables[static_cast<size_t>(state.selectedObjectIndex)];

        // Highlight indicator for selected object
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();

        // Draw selection indicator bar at the side
        float barWidth = 4.0f;
        ImVec2 barStart(windowPos.x, ImGui::GetCursorScreenPos().y);
        ImVec2 barEnd(windowPos.x + barWidth, barStart.y + 200.0f);
        drawList->AddRectFilled(barStart, barEnd, IM_COL32(100, 200, 100, 255));

        if (ImGui::BeginChild("Properties", ImVec2(-1, -1), false)) {
            // Transform section
            if (ImGui::CollapsingHeader("Transform", state.showTransformSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                state.showTransformSection = true;

                glm::vec3 position = extractPosition(selected.transform);
                glm::vec3 scale = extractScale(selected.transform);
                glm::vec3 rotation = extractEulerAngles(selected.transform);

                ImGui::Text("Position");
                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "X: %.3f", position.x);
                ImGui::SameLine(100);
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.3f", position.y);
                ImGui::SameLine(200);
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Z: %.3f", position.z);
                ImGui::Unindent();

                ImGui::Text("Rotation (deg)");
                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "P: %.1f", rotation.x);
                ImGui::SameLine(100);
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.1f", rotation.y);
                ImGui::SameLine(200);
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "R: %.1f", rotation.z);
                ImGui::Unindent();

                ImGui::Text("Scale");
                ImGui::Indent();
                if (std::abs(scale.x - scale.y) < 0.001f && std::abs(scale.y - scale.z) < 0.001f) {
                    ImGui::Text("Uniform: %.3f", scale.x);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "X: %.3f", scale.x);
                    ImGui::SameLine(100);
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.3f", scale.y);
                    ImGui::SameLine(200);
                    ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Z: %.3f", scale.z);
                }
                ImGui::Unindent();

                ImGui::Spacing();
            }

            // Material section
            if (ImGui::CollapsingHeader("Material", state.showMaterialSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                state.showMaterialSection = true;

                ImGui::Text("Material ID: %u", selected.materialId);
                ImGui::Text("Roughness: %.2f", selected.roughness);
                ImGui::Text("Metallic: %.2f", selected.metallic);
                ImGui::Text("Opacity: %.2f", selected.opacity);

                if (selected.alphaTestThreshold > 0.0f) {
                    ImGui::Text("Alpha Test: %.2f", selected.alphaTestThreshold);
                }

                ImGui::Spacing();

                // Emissive info
                if (selected.emissiveIntensity > 0.0f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                    ImGui::Text("Emissive");
                    ImGui::PopStyleColor();
                    ImGui::Indent();
                    ImGui::Text("Intensity: %.2f", selected.emissiveIntensity);
                    ImGui::Text("Color:");
                    ImGui::SameLine();
                    drawColorPreview(selected.emissiveColor);
                    ImGui::SameLine();
                    ImGui::Text("(%.2f, %.2f, %.2f)",
                        selected.emissiveColor.r,
                        selected.emissiveColor.g,
                        selected.emissiveColor.b);
                    ImGui::Unindent();
                }

                // Tree-specific info
                if (selected.treeInstanceIndex >= 0) {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
                    ImGui::Text("Tree Properties");
                    ImGui::PopStyleColor();
                    ImGui::Indent();
                    ImGui::Text("Tree Index: %d", selected.treeInstanceIndex);
                    ImGui::Text("Bark Type: %s", selected.barkType.c_str());
                    ImGui::Text("Leaf Type: %s", selected.leafType.c_str());
                    ImGui::Text("Autumn Shift: %.2f", selected.autumnHueShift);
                    ImGui::Text("Leaf Tint:");
                    ImGui::SameLine();
                    drawColorPreview(selected.leafTint);
                    ImGui::Unindent();
                }

                if (selected.leafInstanceIndex >= 0) {
                    ImGui::Text("Leaf Instance: %d", selected.leafInstanceIndex);
                }

                ImGui::Spacing();
            }

            // Info section
            if (ImGui::CollapsingHeader("Info", state.showInfoSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                state.showInfoSection = true;

                ImGui::Text("Casts Shadow: %s", selected.castsShadow ? "Yes" : "No");
                ImGui::Text("PBR Flags: 0x%X", selected.pbrFlags);

                if (selected.mesh) {
                    ImGui::Spacing();
                    ImGui::Text("Mesh Info");
                    ImGui::Indent();
                    ImGui::Text("Index Count: %u", selected.mesh->getIndexCount());
                    ImGui::Text("Vertex Count: %zu", selected.mesh->getVertices().size());
                    ImGui::Unindent();
                }

                // Index info
                ImGui::Spacing();
                ImGui::Text("Object Index: %d", state.selectedObjectIndex);
                if (static_cast<size_t>(state.selectedObjectIndex) == playerIndex) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "(Player Object)");
                }
            }
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Select an object to view properties");

        ImGui::Spacing();
        ImGui::TextDisabled("Tips:");
        ImGui::BulletText("Click on an object in the list");
        ImGui::BulletText("Use filter to search by type");
        ImGui::BulletText("Types: Player, Tree, Emissive, etc.");
    }
}
