#include "GuiInspectorPanel.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"

#include <imgui.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace {

// Helper to draw a color preview square
void drawColorPreview(const glm::vec3& color, float size = 16.0f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 col = IM_COL32(
        static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255),
        static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255),
        static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255),
        255
    );
    drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col);
    drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(100, 100, 100, 255));
    ImGui::Dummy(ImVec2(size, size));
}

// Helper to edit vec3 with colored labels
bool editVec3(const char* label, glm::vec3& value, float speed = 0.1f, float min = -10000.0f, float max = 10000.0f) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::Text("%s", label);
    ImGui::SameLine(100);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##X", &value.x, speed, min, max, "X:%.2f");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##Y", &value.y, speed, min, max, "Y:%.2f");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##Z", &value.z, speed, min, max, "Z:%.2f");
    ImGui::PopStyleColor();

    ImGui::PopID();
    return changed;
}

// Helper to edit a color with preview
bool editColor(const char* label, glm::vec3& color) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine(100);

    float col[3] = { color.r, color.g, color.b };
    bool changed = ImGui::ColorEdit3("##color", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    if (changed) {
        color = glm::vec3(col[0], col[1], col[2]);
    }
    ImGui::SameLine();
    drawColorPreview(color);

    ImGui::PopID();
    return changed;
}

// Render transform section
void renderTransformSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Transform", state.showTransformSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // Transform mode selector
    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Translate", state.transformMode == SceneEditorState::TransformMode::Translate)) {
        state.transformMode = SceneEditorState::TransformMode::Translate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", state.transformMode == SceneEditorState::TransformMode::Rotate)) {
        state.transformMode = SceneEditorState::TransformMode::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", state.transformMode == SceneEditorState::TransformMode::Scale)) {
        state.transformMode = SceneEditorState::TransformMode::Scale;
    }

    // Space selector
    ImGui::Text("Space:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", state.transformSpace == SceneEditorState::TransformSpace::Local)) {
        state.transformSpace = SceneEditorState::TransformSpace::Local;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", state.transformSpace == SceneEditorState::TransformSpace::World)) {
        state.transformSpace = SceneEditorState::TransformSpace::World;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Edit LocalTransform if available
    if (world.has<ecs::LocalTransform>(entity)) {
        auto& local = world.get<ecs::LocalTransform>(entity);

        if (editVec3("Position", local.position, 0.1f)) {
            // Mark for world transform update
            ecs::systems::updateWorldTransforms(world);
        }

        // Convert quaternion to euler for editing
        glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(local.rotation));
        if (editVec3("Rotation", eulerDegrees, 1.0f, -360.0f, 360.0f)) {
            local.rotation = glm::quat(glm::radians(eulerDegrees));
            ecs::systems::updateWorldTransforms(world);
        }

        if (editVec3("Scale", local.scale, 0.01f, 0.01f, 100.0f)) {
            ecs::systems::updateWorldTransforms(world);
        }

        // Uniform scale checkbox
        static bool uniformScale = true;
        ImGui::Checkbox("Uniform Scale", &uniformScale);
        if (uniformScale) {
            ImGui::SameLine();
            float avgScale = (local.scale.x + local.scale.y + local.scale.z) / 3.0f;
            ImGui::SetNextItemWidth(100);
            if (ImGui::DragFloat("##uniformScale", &avgScale, 0.01f, 0.01f, 100.0f)) {
                local.scale = glm::vec3(avgScale);
                ecs::systems::updateWorldTransforms(world);
            }
        }
    } else if (world.has<ecs::Transform>(entity)) {
        // Read-only world transform display
        const auto& transform = world.get<ecs::Transform>(entity);
        glm::vec3 pos = transform.position();

        // Decompose matrix for display
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform.matrix, scale, rotation, translation, skew, perspective);

        glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(rotation));

        ImGui::TextDisabled("(World Transform - Read Only)");
        ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Rotation: %.1f, %.1f, %.1f", eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
        ImGui::Text("Scale: %.2f, %.2f, %.2f", scale.x, scale.y, scale.z);

        // Offer to add LocalTransform
        if (ImGui::Button("Add LocalTransform")) {
            world.add<ecs::LocalTransform>(entity, translation, rotation, scale);
        }
    } else {
        ImGui::TextDisabled("No transform component");
        if (ImGui::Button("Add Transform")) {
            world.add<ecs::Transform>(entity);
            world.add<ecs::LocalTransform>(entity);
        }
    }

    ImGui::Spacing();
}

// Render material/PBR section
void renderMaterialSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Material", state.showMaterialSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    if (world.has<ecs::MaterialRef>(entity)) {
        auto& matRef = world.get<ecs::MaterialRef>(entity);
        ImGui::Text("Material ID: %u", matRef.id);
    }

    if (world.has<ecs::PBRProperties>(entity)) {
        auto& pbr = world.get<ecs::PBRProperties>(entity);

        ImGui::SliderFloat("Roughness", &pbr.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Metallic", &pbr.metallic, 0.0f, 1.0f);

        ImGui::Spacing();
        ImGui::Text("Emissive");
        ImGui::Indent();
        ImGui::SliderFloat("Intensity##emissive", &pbr.emissiveIntensity, 0.0f, 10.0f);
        editColor("Color##emissive", pbr.emissiveColor);
        ImGui::Unindent();

        if (pbr.alphaTestThreshold > 0.0f) {
            ImGui::SliderFloat("Alpha Test", &pbr.alphaTestThreshold, 0.0f, 1.0f);
        }
    } else {
        ImGui::TextDisabled("No PBR properties");
        if (ImGui::Button("Add PBR Properties")) {
            world.add<ecs::PBRProperties>(entity);
        }
    }

    // Opacity
    if (world.has<ecs::Opacity>(entity)) {
        auto& opacity = world.get<ecs::Opacity>(entity);
        ImGui::SliderFloat("Opacity", &opacity.value, 0.0f, 1.0f);
    }

    // Hue Shift
    if (world.has<ecs::HueShift>(entity)) {
        auto& hue = world.get<ecs::HueShift>(entity);
        ImGui::SliderFloat("Hue Shift", &hue.value, -1.0f, 1.0f);
    }

    ImGui::Spacing();
}

// Render components section
void renderComponentsSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Components", state.showComponentsSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // List existing components with remove buttons
    ImGui::BeginChild("ComponentList", ImVec2(0, 200), true);

    // Light components
    if (world.has<ecs::PointLightComponent>(entity)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.1f, 0.5f));
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = world.get<ecs::PointLightComponent>(entity);
            editColor("Color##pl", light.properties.color);
            ImGui::SliderFloat("Intensity##pl", &light.properties.intensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Radius##pl", &light.radius, 0.1f, 100.0f);
            ImGui::Checkbox("Enabled##pl", &light.properties.enabled);
            ImGui::Checkbox("Cast Shadows##pl", &light.properties.castsShadows);

            if (ImGui::Button("Remove##pl")) {
                world.remove<ecs::PointLightComponent>(entity);
            }
        }
        ImGui::PopStyleColor();
    }

    if (world.has<ecs::SpotLightComponent>(entity)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.1f, 0.5f));
        if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = world.get<ecs::SpotLightComponent>(entity);
            editColor("Color##sl", light.properties.color);
            ImGui::SliderFloat("Intensity##sl", &light.properties.intensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Radius##sl", &light.radius, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Angle##sl", &light.innerConeAngle, 1.0f, 89.0f);
            ImGui::SliderFloat("Outer Angle##sl", &light.outerConeAngle, 1.0f, 90.0f);
            ImGui::Checkbox("Enabled##sl", &light.properties.enabled);

            if (ImGui::Button("Remove##sl")) {
                world.remove<ecs::SpotLightComponent>(entity);
            }
        }
        ImGui::PopStyleColor();
    }

    if (world.has<ecs::LightFlickerComponent>(entity)) {
        if (ImGui::CollapsingHeader("Light Flicker")) {
            auto& flicker = world.get<ecs::LightFlickerComponent>(entity);
            ImGui::SliderFloat("Amount", &flicker.flickerAmount, 0.0f, 1.0f);
            ImGui::SliderFloat("Speed", &flicker.flickerSpeed, 0.0f, 20.0f);
            ImGui::SliderFloat("Noise Scale", &flicker.noiseScale, 0.1f, 10.0f);

            if (ImGui::Button("Remove##flicker")) {
                world.remove<ecs::LightFlickerComponent>(entity);
            }
        }
    }

    // Selection outline
    if (world.has<ecs::SelectionOutline>(entity)) {
        if (ImGui::CollapsingHeader("Selection Outline")) {
            auto& outline = world.get<ecs::SelectionOutline>(entity);
            editColor("Color##outline", outline.color);
            ImGui::SliderFloat("Thickness", &outline.thickness, 0.5f, 10.0f);
            ImGui::SliderFloat("Pulse Speed", &outline.pulseSpeed, 0.0f, 5.0f);

            if (ImGui::Button("Remove##outline")) {
                world.remove<ecs::SelectionOutline>(entity);
            }
        }
    }

    // LOD Controller
    if (world.has<ecs::LODController>(entity)) {
        if (ImGui::CollapsingHeader("LOD Controller")) {
            auto& lod = world.get<ecs::LODController>(entity);
            ImGui::Text("Current Level: %u", lod.currentLevel);
            ImGui::DragFloat("Near", &lod.thresholds[0], 1.0f, 1.0f, 1000.0f);
            ImGui::DragFloat("Mid", &lod.thresholds[1], 1.0f, 1.0f, 1000.0f);
            ImGui::DragFloat("Far", &lod.thresholds[2], 1.0f, 1.0f, 1000.0f);

            if (ImGui::Button("Remove##lod")) {
                world.remove<ecs::LODController>(entity);
            }
        }
    }

    // Bounding sphere
    if (world.has<ecs::BoundingSphere>(entity)) {
        if (ImGui::CollapsingHeader("Bounding Sphere")) {
            auto& bounds = world.get<ecs::BoundingSphere>(entity);
            ImGui::DragFloat3("Center", &bounds.center.x, 0.1f);
            ImGui::DragFloat("Radius", &bounds.radius, 0.1f, 0.01f, 1000.0f);

            if (ImGui::Button("Remove##bounds")) {
                world.remove<ecs::BoundingSphere>(entity);
            }
        }
    }

    ImGui::EndChild();

    // Add component button
    if (ImGui::Button("Add Component...")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!world.has<ecs::PointLightComponent>(entity) && ImGui::MenuItem("Point Light")) {
            world.add<ecs::PointLightComponent>(entity, glm::vec3(1.0f), 1.0f, 10.0f);
            world.add<ecs::LightSourceTag>(entity);
        }
        if (!world.has<ecs::SpotLightComponent>(entity) && ImGui::MenuItem("Spot Light")) {
            world.add<ecs::SpotLightComponent>(entity, glm::vec3(1.0f), 1.0f);
            world.add<ecs::LightSourceTag>(entity);
        }
        if (!world.has<ecs::LightFlickerComponent>(entity) && ImGui::MenuItem("Light Flicker")) {
            world.add<ecs::LightFlickerComponent>(entity);
        }
        ImGui::Separator();
        if (!world.has<ecs::SelectionOutline>(entity) && ImGui::MenuItem("Selection Outline")) {
            world.add<ecs::SelectionOutline>(entity);
        }
        if (!world.has<ecs::LODController>(entity) && ImGui::MenuItem("LOD Controller")) {
            world.add<ecs::LODController>(entity);
        }
        if (!world.has<ecs::BoundingSphere>(entity) && ImGui::MenuItem("Bounding Sphere")) {
            world.add<ecs::BoundingSphere>(entity, glm::vec3(0.0f), 1.0f);
        }
        ImGui::Separator();
        if (!world.has<ecs::Opacity>(entity) && ImGui::MenuItem("Opacity")) {
            world.add<ecs::Opacity>(entity, 1.0f);
        }
        if (!world.has<ecs::HueShift>(entity) && ImGui::MenuItem("Hue Shift")) {
            world.add<ecs::HueShift>(entity, 0.0f);
        }
        if (!world.has<ecs::PBRProperties>(entity) && ImGui::MenuItem("PBR Properties")) {
            world.add<ecs::PBRProperties>(entity);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

// Render tags section
void renderTagsSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Tags", state.showTagsSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // Display current tags
    ImGui::BeginChild("TagList", ImVec2(0, 100), true);

    // Check and display each tag type
    if (world.has<ecs::CastsShadow>(entity)) {
        ImGui::BulletText("Casts Shadow");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##shadow")) {
            world.remove<ecs::CastsShadow>(entity);
        }
    }
    if (world.has<ecs::Visible>(entity)) {
        ImGui::BulletText("Visible");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##visible")) {
            world.remove<ecs::Visible>(entity);
        }
    }
    if (world.has<ecs::Transparent>(entity)) {
        ImGui::BulletText("Transparent");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##transparent")) {
            world.remove<ecs::Transparent>(entity);
        }
    }
    if (world.has<ecs::Reflective>(entity)) {
        ImGui::BulletText("Reflective");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##reflective")) {
            world.remove<ecs::Reflective>(entity);
        }
    }
    if (world.has<ecs::LightSourceTag>(entity)) {
        ImGui::BulletText("Light Source");
    }
    if (world.has<ecs::PlayerTag>(entity)) {
        ImGui::BulletText("Player");
    }
    if (world.has<ecs::NPCTag>(entity)) {
        ImGui::BulletText("NPC");
    }

    ImGui::EndChild();

    // Add tag button
    if (ImGui::Button("Add Tag...")) {
        ImGui::OpenPopup("AddTagPopup");
    }

    if (ImGui::BeginPopup("AddTagPopup")) {
        if (!world.has<ecs::CastsShadow>(entity) && ImGui::MenuItem("Casts Shadow")) {
            world.add<ecs::CastsShadow>(entity);
        }
        if (!world.has<ecs::Visible>(entity) && ImGui::MenuItem("Visible")) {
            world.add<ecs::Visible>(entity);
        }
        if (!world.has<ecs::Transparent>(entity) && ImGui::MenuItem("Transparent")) {
            world.add<ecs::Transparent>(entity);
        }
        if (!world.has<ecs::Reflective>(entity) && ImGui::MenuItem("Reflective")) {
            world.add<ecs::Reflective>(entity);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

} // anonymous namespace

void GuiInspectorPanel::render(ISceneControl& sceneControl, SceneEditorState& state) {
    ecs::World* world = sceneControl.getECSWorld();

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("INSPECTOR");
    ImGui::PopStyleColor();

    if (!world) {
        ImGui::TextDisabled("ECS World not available");
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Check if an entity is selected
    if (state.selectedEntity == ecs::NullEntity) {
        ImGui::TextDisabled("No entity selected");
        ImGui::Spacing();
        ImGui::TextDisabled("Select an entity in the Hierarchy panel");
        ImGui::TextDisabled("to view and edit its properties.");
        return;
    }

    // Validate entity
    if (!world->valid(state.selectedEntity)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid entity selected");
        if (ImGui::Button("Clear Selection")) {
            state.clearSelection();
        }
        return;
    }

    // Entity header
    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(state.selectedEntity));

    // Name editing (if DebugName component exists)
    if (world->has<ecs::DebugName>(state.selectedEntity)) {
        auto& debugName = world->get<ecs::DebugName>(state.selectedEntity);
        ImGui::Text("Name: %s", debugName.name ? debugName.name : "(unnamed)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Scrollable content area
    if (ImGui::BeginChild("InspectorContent", ImVec2(0, 0), false)) {
        renderTransformSection(*world, state.selectedEntity, state);
        renderMaterialSection(*world, state.selectedEntity, state);
        renderComponentsSection(*world, state.selectedEntity, state);
        renderTagsSection(*world, state.selectedEntity, state);
    }
    ImGui::EndChild();
}
