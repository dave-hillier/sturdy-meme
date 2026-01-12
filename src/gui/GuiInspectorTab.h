#pragma once

#include <entt/entt.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <string>
#include "../ecs/Components.h"
#include "../ecs/SceneGraphSystem.h"

// Inspector Tab - Unity-like component property editor
// Displays and edits components of the selected entity
class GuiInspectorTab {
public:
    // Render the inspector for the selected entity
    void render(entt::registry& registry, entt::entity selectedEntity) {
        if (selectedEntity == entt::null || !registry.valid(selectedEntity)) {
            ImGui::TextDisabled("No entity selected");
            ImGui::TextDisabled("Select an entity in the Scene Graph");
            return;
        }

        // Entity header
        renderEntityHeader(registry, selectedEntity);

        ImGui::Separator();

        // Scrollable component list
        ImGui::BeginChild("ComponentList", ImVec2(0, 0), false);

        // Render each component type
        renderEntityInfoComponent(registry, selectedEntity);
        renderTransformComponent(registry, selectedEntity);
        renderHierarchyComponent(registry, selectedEntity);
        renderWorldTransformComponent(registry, selectedEntity);
        renderVelocityComponent(registry, selectedEntity);
        renderPointLightComponent(registry, selectedEntity);
        renderSpotLightComponent(registry, selectedEntity);
        renderHealthComponent(registry, selectedEntity);
        renderAIStateComponent(registry, selectedEntity);
        renderPatrolPathComponent(registry, selectedEntity);
        renderMovementSettingsComponent(registry, selectedEntity);
        renderPhysicsBodyComponent(registry, selectedEntity);
        renderRenderableRefComponent(registry, selectedEntity);
        renderMeshRendererComponent(registry, selectedEntity);
        renderCameraComponent(registry, selectedEntity);
        renderAABBBoundsComponent(registry, selectedEntity);
        renderLODGroupComponent(registry, selectedEntity);
        renderTagComponents(registry, selectedEntity);

        ImGui::Separator();

        // Add component button
        renderAddComponentMenu(registry, selectedEntity);

        ImGui::EndChild();
    }

private:
    char renameBuffer_[128] = "";
    bool renaming_ = false;

    // Render entity header with name and basic info
    void renderEntityHeader(entt::registry& registry, entt::entity entity) {
        std::string name = SceneGraph::getEntityName(registry, entity);
        std::string icon = SceneGraph::getEntityIcon(registry, entity);

        // Icon and name
        ImGui::Text("[%s]", icon.c_str());
        ImGui::SameLine();

        if (renaming_) {
            if (ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (registry.all_of<EntityInfo>(entity)) {
                    registry.get<EntityInfo>(entity).name = renameBuffer_;
                } else if (registry.all_of<NameTag>(entity)) {
                    registry.get<NameTag>(entity).name = renameBuffer_;
                }
                renaming_ = false;
            }
            if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                renaming_ = false;
            }
        } else {
            ImGui::Text("%s", name.c_str());
            if (ImGui::IsItemClicked()) {
                strcpy(renameBuffer_, name.c_str());
                renaming_ = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to rename");
            }
        }

        // Entity ID
        ImGui::SameLine();
        ImGui::TextDisabled("(ID: %u)", static_cast<uint32_t>(entity));
    }

    // Helper to render collapsible component header
    bool renderComponentHeader(const char* name, bool canRemove = true) {
        bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
        return open;
    }

    // Helper for vec3 editor
    bool editVec3(const char* label, glm::vec3& v, float speed = 0.1f) {
        float arr[3] = {v.x, v.y, v.z};
        if (ImGui::DragFloat3(label, arr, speed)) {
            v = glm::vec3(arr[0], arr[1], arr[2]);
            return true;
        }
        return false;
    }

    // Helper for color editor
    bool editColor3(const char* label, glm::vec3& color) {
        float arr[3] = {color.r, color.g, color.b};
        if (ImGui::ColorEdit3(label, arr)) {
            color = glm::vec3(arr[0], arr[1], arr[2]);
            return true;
        }
        return false;
    }

    // ========================================================================
    // Component Renderers
    // ========================================================================

    void renderEntityInfoComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<EntityInfo>(entity)) return;

        if (renderComponentHeader("Entity Info")) {
            auto& info = registry.get<EntityInfo>(entity);

            char nameBuffer[128];
            strcpy(nameBuffer, info.name.c_str());
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                info.name = nameBuffer;
            }

            char iconBuffer[8];
            strcpy(iconBuffer, info.icon.c_str());
            if (ImGui::InputText("Icon", iconBuffer, sizeof(iconBuffer))) {
                info.icon = iconBuffer;
            }

            ImGui::Checkbox("Visible", &info.visible);
            ImGui::Checkbox("Locked", &info.locked);

            int layer = static_cast<int>(info.layer);
            if (ImGui::InputInt("Layer", &layer)) {
                info.layer = static_cast<uint32_t>(std::max(0, layer));
            }
        }
    }

    void renderTransformComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Transform>(entity)) return;

        if (renderComponentHeader("Transform")) {
            auto& transform = registry.get<Transform>(entity);

            editVec3("Position", transform.position);

            float yaw = transform.yaw;
            if (ImGui::DragFloat("Yaw", &yaw, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                transform.yaw = yaw;
                transform.normalizeYaw();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Forward: (%.2f, %.2f, %.2f)",
                               transform.getForward().x, transform.getForward().y, transform.getForward().z);
            ImGui::TextDisabled("Right: (%.2f, %.2f, %.2f)",
                               transform.getRight().x, transform.getRight().y, transform.getRight().z);
        }
    }

    void renderHierarchyComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Hierarchy>(entity)) return;

        if (renderComponentHeader("Hierarchy")) {
            auto& hierarchy = registry.get<Hierarchy>(entity);
            bool dirty = false;

            // Parent info
            if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent)) {
                std::string parentName = SceneGraph::getEntityName(registry, hierarchy.parent);
                ImGui::Text("Parent: %s", parentName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##parent")) {
                    SceneGraph::removeParent(registry, entity);
                }
            } else {
                ImGui::TextDisabled("Parent: None (Root)");
            }

            // Children count
            ImGui::Text("Children: %zu", hierarchy.children.size());

            ImGui::Separator();

            // Local transform
            if (editVec3("Local Position", hierarchy.localPosition)) dirty = true;
            if (editVec3("Local Scale", hierarchy.localScale, 0.01f)) dirty = true;

            float localYaw = hierarchy.localYaw;
            if (ImGui::DragFloat("Local Yaw", &localYaw, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                hierarchy.localYaw = localYaw;
                dirty = true;
            }

            if (dirty) {
                SceneGraph::markTransformDirty(registry, entity);
            }
        }
    }

    void renderWorldTransformComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<WorldTransform>(entity)) return;

        if (renderComponentHeader("World Transform (Read-Only)")) {
            const auto& world = registry.get<WorldTransform>(entity);

            ImGui::BeginDisabled();
            glm::vec3 pos = world.position;
            glm::vec3 scale = world.scale;
            float yaw = world.yaw;
            editVec3("World Position", pos);
            editVec3("World Scale", scale, 0.01f);
            ImGui::DragFloat("World Yaw", &yaw);
            ImGui::EndDisabled();

            ImGui::TextDisabled("Dirty: %s", world.dirty ? "Yes" : "No");
        }
    }

    void renderVelocityComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Velocity>(entity)) return;

        if (renderComponentHeader("Velocity")) {
            auto& velocity = registry.get<Velocity>(entity);
            editVec3("Linear", velocity.linear, 0.01f);

            float speed = glm::length(velocity.linear);
            ImGui::TextDisabled("Speed: %.2f", speed);
        }
    }

    void renderPointLightComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<PointLight>(entity)) return;

        if (renderComponentHeader("Point Light")) {
            auto& light = registry.get<PointLight>(entity);

            editColor3("Color", light.color);
            ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.1f, 100.0f);
            ImGui::DragFloat("Priority", &light.priority, 0.1f, 0.0f, 10.0f);
            ImGui::Checkbox("Casts Shadows", &light.castsShadows);

            // Enabled toggle
            bool enabled = registry.all_of<LightEnabled>(entity);
            if (ImGui::Checkbox("Enabled", &enabled)) {
                if (enabled) {
                    registry.emplace_or_replace<LightEnabled>(entity);
                } else {
                    registry.remove<LightEnabled>(entity);
                }
            }
        }
    }

    void renderSpotLightComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<SpotLight>(entity)) return;

        if (renderComponentHeader("Spot Light")) {
            auto& light = registry.get<SpotLight>(entity);

            editColor3("Color", light.color);
            ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.1f, 100.0f);
            editVec3("Direction", light.direction, 0.01f);

            // Normalize direction after editing
            if (glm::length(light.direction) > 0.001f) {
                light.direction = glm::normalize(light.direction);
            }

            ImGui::DragFloat("Inner Cone", &light.innerConeAngle, 0.5f, 1.0f, 89.0f, "%.1f deg");
            ImGui::DragFloat("Outer Cone", &light.outerConeAngle, 0.5f, 1.0f, 90.0f, "%.1f deg");

            // Ensure inner <= outer
            if (light.innerConeAngle > light.outerConeAngle) {
                light.innerConeAngle = light.outerConeAngle;
            }

            ImGui::DragFloat("Priority", &light.priority, 0.1f, 0.0f, 10.0f);
            ImGui::Checkbox("Casts Shadows", &light.castsShadows);

            // Enabled toggle
            bool enabled = registry.all_of<LightEnabled>(entity);
            if (ImGui::Checkbox("Enabled", &enabled)) {
                if (enabled) {
                    registry.emplace_or_replace<LightEnabled>(entity);
                } else {
                    registry.remove<LightEnabled>(entity);
                }
            }
        }
    }

    void renderHealthComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Health>(entity)) return;

        if (renderComponentHeader("Health")) {
            auto& health = registry.get<Health>(entity);

            // Health bar
            float ratio = health.maximum > 0 ? health.current / health.maximum : 0.0f;
            ImGui::ProgressBar(ratio, ImVec2(-1, 0),
                              (std::to_string(static_cast<int>(health.current)) + "/" +
                               std::to_string(static_cast<int>(health.maximum))).c_str());

            ImGui::DragFloat("Current", &health.current, 1.0f, 0.0f, health.maximum);
            ImGui::DragFloat("Maximum", &health.maximum, 1.0f, 1.0f, 10000.0f);
            ImGui::Checkbox("Invulnerable", &health.invulnerable);
        }
    }

    void renderAIStateComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AIState>(entity)) return;

        if (renderComponentHeader("AI State")) {
            auto& ai = registry.get<AIState>(entity);

            const char* stateNames[] = {"Idle", "Patrol", "Chase", "Flee"};
            int currentState = static_cast<int>(ai.current);
            if (ImGui::Combo("State", &currentState, stateNames, IM_ARRAYSIZE(stateNames))) {
                ai.current = static_cast<AIState::State>(currentState);
            }

            ImGui::DragFloat("State Timer", &ai.stateTimer, 0.1f, 0.0f, 100.0f, "%.1f s");
        }
    }

    void renderPatrolPathComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<PatrolPath>(entity)) return;

        if (renderComponentHeader("Patrol Path")) {
            auto& patrol = registry.get<PatrolPath>(entity);

            ImGui::Text("Waypoints: %zu", patrol.waypoints.size());
            ImGui::Text("Current: %zu", patrol.currentWaypoint);
            ImGui::Checkbox("Loop", &patrol.loop);
            ImGui::DragFloat("Waypoint Radius", &patrol.waypointRadius, 0.1f, 0.1f, 10.0f);

            // Show waypoints in a list
            if (ImGui::TreeNode("Waypoints")) {
                for (size_t i = 0; i < patrol.waypoints.size(); i++) {
                    ImGui::PushID(static_cast<int>(i));
                    std::string label = "##wp" + std::to_string(i);
                    float wp[3] = {patrol.waypoints[i].x, patrol.waypoints[i].y, patrol.waypoints[i].z};
                    if (ImGui::DragFloat3(label.c_str(), wp, 0.1f)) {
                        patrol.waypoints[i] = glm::vec3(wp[0], wp[1], wp[2]);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        patrol.waypoints.erase(patrol.waypoints.begin() + i);
                        if (patrol.currentWaypoint >= patrol.waypoints.size() && !patrol.waypoints.empty()) {
                            patrol.currentWaypoint = patrol.waypoints.size() - 1;
                        }
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("+ Add Waypoint")) {
                    glm::vec3 pos{0.0f};
                    if (registry.all_of<Transform>(entity)) {
                        pos = registry.get<Transform>(entity).position;
                    }
                    patrol.waypoints.push_back(pos);
                }

                ImGui::TreePop();
            }
        }
    }

    void renderMovementSettingsComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<MovementSettings>(entity)) return;

        if (renderComponentHeader("Movement Settings")) {
            auto& movement = registry.get<MovementSettings>(entity);

            ImGui::DragFloat("Walk Speed", &movement.walkSpeed, 0.1f, 0.0f, 50.0f, "%.1f m/s");
            ImGui::DragFloat("Run Speed", &movement.runSpeed, 0.1f, 0.0f, 100.0f, "%.1f m/s");
            ImGui::DragFloat("Turn Speed", &movement.turnSpeed, 1.0f, 0.0f, 720.0f, "%.0f deg/s");
        }
    }

    void renderPhysicsBodyComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<PhysicsBody>(entity)) return;

        if (renderComponentHeader("Physics Body")) {
            const auto& body = registry.get<PhysicsBody>(entity);

            ImGui::TextDisabled("Body ID: %u", body.id);
            ImGui::TextDisabled("(Physics properties controlled by Jolt)");
        }
    }

    void renderRenderableRefComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<RenderableRef>(entity)) return;

        if (renderComponentHeader("Renderable Reference")) {
            auto& ref = registry.get<RenderableRef>(entity);

            int idx = static_cast<int>(ref.sceneIndex);
            if (ImGui::InputInt("Scene Index", &idx)) {
                ref.sceneIndex = static_cast<size_t>(std::max(0, idx));
            }
        }
    }

    void renderMeshRendererComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<MeshRenderer>(entity)) return;

        if (renderComponentHeader("Mesh Renderer")) {
            auto& mesh = registry.get<MeshRenderer>(entity);

            // Display handles as IDs (would need resource registry to show names)
            int meshId = static_cast<int>(mesh.mesh);
            if (ImGui::InputInt("Mesh Handle", &meshId)) {
                mesh.mesh = static_cast<MeshHandle>(std::max(0, meshId));
            }

            int matId = static_cast<int>(mesh.material);
            if (ImGui::InputInt("Material Handle", &matId)) {
                mesh.material = static_cast<MaterialHandle>(std::max(0, matId));
            }

            int submesh = static_cast<int>(mesh.submeshIndex);
            if (ImGui::InputInt("Submesh Index", &submesh)) {
                mesh.submeshIndex = static_cast<uint32_t>(std::max(0, submesh));
            }

            ImGui::Checkbox("Casts Shadow", &mesh.castsShadow);
            ImGui::Checkbox("Receive Shadow", &mesh.receiveShadow);

            // Render layer dropdown
            const char* layerNames[] = {"Default", "Terrain", "Water", "Vegetation", "Character", "UI", "Debug"};
            int currentLayer = 0;
            uint32_t layerVal = static_cast<uint32_t>(mesh.layer);
            for (int i = 0; i < 7; i++) {
                if (layerVal == (1u << i)) {
                    currentLayer = i;
                    break;
                }
            }
            if (ImGui::Combo("Render Layer", &currentLayer, layerNames, IM_ARRAYSIZE(layerNames))) {
                mesh.layer = static_cast<RenderLayer>(1u << currentLayer);
            }
        }
    }

    void renderCameraComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<CameraComponent>(entity)) return;

        if (renderComponentHeader("Camera")) {
            auto& cam = registry.get<CameraComponent>(entity);

            ImGui::DragFloat("FOV", &cam.fov, 0.5f, 1.0f, 179.0f, "%.1f deg");
            ImGui::DragFloat("Near Plane", &cam.nearPlane, 0.01f, 0.001f, 100.0f, "%.3f");
            ImGui::DragFloat("Far Plane", &cam.farPlane, 10.0f, 1.0f, 100000.0f, "%.0f");
            ImGui::InputInt("Priority", &cam.priority);

            // Main camera toggle
            bool isMain = registry.all_of<MainCamera>(entity);
            if (ImGui::Checkbox("Main Camera", &isMain)) {
                if (isMain) {
                    // Remove MainCamera from all other entities first
                    auto view = registry.view<MainCamera>();
                    for (auto other : view) {
                        registry.remove<MainCamera>(other);
                    }
                    registry.emplace<MainCamera>(entity);
                } else {
                    registry.remove<MainCamera>(entity);
                }
            }
        }
    }

    void renderAABBBoundsComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AABBBounds>(entity)) return;

        if (renderComponentHeader("AABB Bounds")) {
            auto& bounds = registry.get<AABBBounds>(entity);

            editVec3("Min", bounds.min);
            editVec3("Max", bounds.max);

            ImGui::Separator();
            glm::vec3 center = bounds.center();
            glm::vec3 extents = bounds.extents();
            ImGui::TextDisabled("Center: (%.2f, %.2f, %.2f)", center.x, center.y, center.z);
            ImGui::TextDisabled("Extents: (%.2f, %.2f, %.2f)", extents.x, extents.y, extents.z);
        }
    }

    void renderLODGroupComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<LODGroup>(entity)) return;

        if (renderComponentHeader("LOD Group")) {
            auto& lod = registry.get<LODGroup>(entity);

            ImGui::Text("Current LOD: %d", lod.currentLOD);
            ImGui::Text("LOD Levels: %zu", lod.switchDistances.size());

            if (ImGui::TreeNode("LOD Distances")) {
                for (size_t i = 0; i < lod.switchDistances.size(); i++) {
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::DragFloat(("##lod" + std::to_string(i)).c_str(),
                                     &lod.switchDistances[i], 1.0f, 0.0f, 10000.0f, "%.0f m");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X") && lod.switchDistances.size() > 1) {
                        lod.switchDistances.erase(lod.switchDistances.begin() + i);
                        lod.lodMeshes.erase(lod.lodMeshes.begin() + i);
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("+ Add LOD Level")) {
                    float lastDist = lod.switchDistances.empty() ? 50.0f : lod.switchDistances.back() * 2.0f;
                    lod.switchDistances.push_back(lastDist);
                    lod.lodMeshes.push_back(InvalidMesh);
                }

                ImGui::TreePop();
            }
        }
    }

    void renderTagComponents(entt::registry& registry, entt::entity entity) {
        // Collect all tag components
        std::vector<std::string> tags;

        if (registry.all_of<PlayerTag>(entity)) tags.push_back("Player");
        if (registry.all_of<Grounded>(entity)) tags.push_back("Grounded");
        if (registry.all_of<DynamicObject>(entity)) tags.push_back("Dynamic Object");
        if (registry.all_of<PhysicsDriven>(entity)) tags.push_back("Physics Driven");
        if (registry.all_of<NPCTag>(entity)) tags.push_back("NPC");
        if (registry.all_of<LightEnabled>(entity)) tags.push_back("Light Enabled");
        if (registry.all_of<Selected>(entity)) tags.push_back("Selected");
        if (registry.all_of<MainCamera>(entity)) tags.push_back("Main Camera");
        if (registry.all_of<StaticObject>(entity)) tags.push_back("Static");
        if (registry.all_of<WasVisible>(entity)) tags.push_back("Was Visible");

        if (!tags.empty()) {
            if (renderComponentHeader("Tags")) {
                for (const auto& tag : tags) {
                    ImGui::BulletText("%s", tag.c_str());
                }
            }
        }
    }

    // Add component menu
    void renderAddComponentMenu(entt::registry& registry, entt::entity entity) {
        if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            ImGui::TextDisabled("Components");
            ImGui::Separator();

            if (!registry.all_of<Transform>(entity)) {
                if (ImGui::MenuItem("Transform")) {
                    registry.emplace<Transform>(entity);
                }
            }
            if (!registry.all_of<Velocity>(entity)) {
                if (ImGui::MenuItem("Velocity")) {
                    registry.emplace<Velocity>(entity);
                }
            }
            if (!registry.all_of<Hierarchy>(entity)) {
                if (ImGui::MenuItem("Hierarchy")) {
                    registry.emplace<Hierarchy>(entity);
                    registry.emplace_or_replace<WorldTransform>(entity);
                }
            }
            if (!registry.all_of<PointLight>(entity) && !registry.all_of<SpotLight>(entity)) {
                if (ImGui::MenuItem("Point Light")) {
                    registry.emplace<PointLight>(entity);
                    registry.emplace<LightEnabled>(entity);
                }
                if (ImGui::MenuItem("Spot Light")) {
                    registry.emplace<SpotLight>(entity);
                    registry.emplace<LightEnabled>(entity);
                }
            }
            if (!registry.all_of<Health>(entity)) {
                if (ImGui::MenuItem("Health")) {
                    registry.emplace<Health>(entity);
                }
            }
            if (!registry.all_of<AIState>(entity)) {
                if (ImGui::MenuItem("AI State")) {
                    registry.emplace<AIState>(entity);
                }
            }
            if (!registry.all_of<MovementSettings>(entity)) {
                if (ImGui::MenuItem("Movement Settings")) {
                    registry.emplace<MovementSettings>(entity);
                }
            }
            if (!registry.all_of<PatrolPath>(entity)) {
                if (ImGui::MenuItem("Patrol Path")) {
                    registry.emplace<PatrolPath>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Rendering");

            if (!registry.all_of<MeshRenderer>(entity)) {
                if (ImGui::MenuItem("Mesh Renderer")) {
                    registry.emplace<MeshRenderer>(entity);
                }
            }
            if (!registry.all_of<CameraComponent>(entity)) {
                if (ImGui::MenuItem("Camera")) {
                    registry.emplace<CameraComponent>(entity);
                }
            }
            if (!registry.all_of<AABBBounds>(entity)) {
                if (ImGui::MenuItem("AABB Bounds")) {
                    registry.emplace<AABBBounds>(entity);
                }
            }
            if (!registry.all_of<LODGroup>(entity)) {
                if (ImGui::MenuItem("LOD Group")) {
                    LODGroup lod;
                    lod.switchDistances = {50.0f, 100.0f, 200.0f};
                    lod.lodMeshes = {InvalidMesh, InvalidMesh, InvalidMesh};
                    registry.emplace<LODGroup>(entity, std::move(lod));
                }
            }
            if (!registry.all_of<Billboard>(entity)) {
                if (ImGui::MenuItem("Billboard")) {
                    registry.emplace<Billboard>(entity);
                }
            }
            if (!registry.all_of<StaticObject>(entity)) {
                if (ImGui::MenuItem("Static Object (Tag)")) {
                    registry.emplace<StaticObject>(entity);
                }
            }

            ImGui::EndPopup();
        }
    }
};
