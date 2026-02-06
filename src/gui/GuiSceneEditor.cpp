#include "GuiSceneEditor.h"
#include "GuiHierarchyPanel.h"
#include "GuiInspectorPanel.h"
#include "core/interfaces/ISceneControl.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

// Persistent dockspace ID
ImGuiID dockspaceId = 0;
bool dockspaceInitialized = false;

void setupDefaultLayout(ImGuiID dockId) {
    // Split the dockspace into left (hierarchy) and right (inspector)
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImVec2(800, 600));

    ImGuiID dockLeft, dockRight;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.4f, &dockLeft, &dockRight);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);

    ImGui::DockBuilderFinish(dockId);
}

} // anonymous namespace

void GuiSceneEditor::render(ISceneControl& sceneControl, SceneEditorState& state, bool* showWindow) {
    if (!*showWindow) return;

    // Set up the main editor window with docking
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    // Make the window dockable itself
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Editor", showWindow, windowFlags)) {
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Layout")) {
                    dockspaceInitialized = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Expand All Hierarchy")) {
                    ecs::World* world = sceneControl.getECSWorld();
                    if (world) {
                        for (auto entity : world->view<ecs::Children>()) {
                            state.setExpanded(entity, true);
                        }
                    }
                }
                if (ImGui::MenuItem("Collapse All Hierarchy")) {
                    state.expandedNodes.clear();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Empty Entity")) {
                    ecs::World* world = sceneControl.getECSWorld();
                    if (world) {
                        ecs::Entity newEntity = world->create();
                        world->add<ecs::Transform>(newEntity);
                        world->add<ecs::LocalTransform>(newEntity);
                        state.select(newEntity);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Point Light")) {
                    ecs::World* world = sceneControl.getECSWorld();
                    if (world) {
                        ecs::Entity newEntity = world->create();
                        world->add<ecs::Transform>(newEntity);
                        world->add<ecs::LocalTransform>(newEntity);
                        world->add<ecs::PointLightComponent>(newEntity, glm::vec3(1.0f), 1.0f, 10.0f);
                        world->add<ecs::LightSourceTag>(newEntity);
                        state.select(newEntity);
                    }
                }
                if (ImGui::MenuItem("Spot Light")) {
                    ecs::World* world = sceneControl.getECSWorld();
                    if (world) {
                        ecs::Entity newEntity = world->create();
                        world->add<ecs::Transform>(newEntity);
                        world->add<ecs::LocalTransform>(newEntity);
                        world->add<ecs::SpotLightComponent>(newEntity, glm::vec3(1.0f), 1.0f);
                        world->add<ecs::LightSourceTag>(newEntity);
                        state.select(newEntity);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                bool hasSelection = state.selectedEntity != ecs::NullEntity;

                if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) {
                    ecs::World* world = sceneControl.getECSWorld();
                    if (world && hasSelection) {
                        ecs::systems::detachFromParent(*world, state.selectedEntity);
                        world->destroy(state.selectedEntity);
                        state.clearSelection();
                    }
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
                    // TODO: Implement entity duplication
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Focus", "F", false, hasSelection)) {
                    // TODO: Implement camera focus on selected entity
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Create dockspace
        dockspaceId = ImGui::GetID("SceneEditorDockspace");

        // Initialize default layout on first run
        if (!dockspaceInitialized) {
            setupDefaultLayout(dockspaceId);
            dockspaceInitialized = true;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    }
    ImGui::End();

    // Render the individual panels (they'll dock themselves)
    ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hierarchy")) {
        GuiHierarchyPanel::render(sceneControl, state);
    }
    ImGui::End();

    ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inspector")) {
        GuiInspectorPanel::render(sceneControl, state);
    }
    ImGui::End();
}

void GuiSceneEditor::renderHierarchy(ISceneControl& sceneControl, SceneEditorState& state) {
    GuiHierarchyPanel::render(sceneControl, state);
}

void GuiSceneEditor::renderInspector(ISceneControl& sceneControl, SceneEditorState& state) {
    GuiInspectorPanel::render(sceneControl, state);
}
