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
        renderSkinnedMeshRendererComponent(registry, selectedEntity);
        renderCameraComponent(registry, selectedEntity);
        renderAABBBoundsComponent(registry, selectedEntity);
        renderLODGroupComponent(registry, selectedEntity);
        renderAnimatorComponent(registry, selectedEntity);
        renderAnimationStateComponent(registry, selectedEntity);
        renderFootIKComponent(registry, selectedEntity);
        renderLookAtIKComponent(registry, selectedEntity);
        renderParticleEmitterComponent(registry, selectedEntity);
        renderPhysicsMaterialComponent(registry, selectedEntity);
        renderTerrainPatchComponent(registry, selectedEntity);
        renderGrassVolumeComponent(registry, selectedEntity);
        renderWaterSurfaceComponent(registry, selectedEntity);
        renderTreeInstanceComponent(registry, selectedEntity);
        renderVegetationZoneComponent(registry, selectedEntity);
        renderWindZoneComponent(registry, selectedEntity);
        renderWeatherZoneComponent(registry, selectedEntity);
        renderFogVolumeComponent(registry, selectedEntity);
        renderOcclusionCullableComponent(registry, selectedEntity);
        renderCullBoundingSphereComponent(registry, selectedEntity);
        renderOccluderComponent(registry, selectedEntity);
        renderVisibilityCellComponent(registry, selectedEntity);
        renderCullingGroupComponent(registry, selectedEntity);
        renderDecalComponent(registry, selectedEntity);
        renderSpriteRendererComponent(registry, selectedEntity);
        renderRenderTargetComponent(registry, selectedEntity);
        renderReflectionProbeComponent(registry, selectedEntity);
        renderLightProbeComponent(registry, selectedEntity);
        renderLightProbeVolumeComponent(registry, selectedEntity);
        renderPortalSurfaceComponent(registry, selectedEntity);
        renderAudioSourceComponent(registry, selectedEntity);
        renderAudioListenerComponent(registry, selectedEntity);
        renderAmbientSoundZoneComponent(registry, selectedEntity);
        renderReverbZoneComponent(registry, selectedEntity);
        renderMusicTrackComponent(registry, selectedEntity);
        renderAudioMixerGroupComponent(registry, selectedEntity);
        renderAudioOcclusionComponent(registry, selectedEntity);
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

    void renderSkinnedMeshRendererComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<SkinnedMeshRenderer>(entity)) return;

        if (renderComponentHeader("Skinned Mesh Renderer")) {
            auto& skinned = registry.get<SkinnedMeshRenderer>(entity);

            int meshId = static_cast<int>(skinned.mesh);
            if (ImGui::InputInt("Mesh Handle", &meshId)) {
                skinned.mesh = static_cast<MeshHandle>(std::max(0, meshId));
            }

            int matId = static_cast<int>(skinned.material);
            if (ImGui::InputInt("Material Handle", &matId)) {
                skinned.material = static_cast<MaterialHandle>(std::max(0, matId));
            }

            int skelId = static_cast<int>(skinned.skeleton);
            if (ImGui::InputInt("Skeleton Handle", &skelId)) {
                skinned.skeleton = static_cast<SkeletonHandle>(std::max(0, skelId));
            }

            ImGui::DragFloat("Animation Time", &skinned.animationTime, 0.01f, 0.0f, 100.0f, "%.2f s");
        }
    }

    void renderAnimatorComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Animator>(entity)) return;

        if (renderComponentHeader("Animator")) {
            auto& animator = registry.get<Animator>(entity);

            const char* stateNames[] = {"Idle", "Walk", "Run", "Jump", "Fall", "Land", "Custom"};
            int currentState = static_cast<int>(animator.currentState);
            if (ImGui::Combo("Current State", &currentState, stateNames, IM_ARRAYSIZE(stateNames))) {
                animator.currentState = static_cast<Animator::State>(currentState);
            }

            ImGui::TextDisabled("Previous: %s", stateNames[static_cast<int>(animator.previousState)]);
            ImGui::DragFloat("State Time", &animator.stateTime, 0.01f, 0.0f, 100.0f, "%.2f s");
            ImGui::DragFloat("Transition Time", &animator.transitionTime, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("Movement Speed", &animator.movementSpeed, 0.1f, 0.0f, 20.0f, "%.1f m/s");
            ImGui::Checkbox("Grounded", &animator.grounded);
            ImGui::Checkbox("Jumping", &animator.jumping);
        }
    }

    void renderAnimationStateComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AnimationState>(entity)) return;

        if (renderComponentHeader("Animation State")) {
            auto& state = registry.get<AnimationState>(entity);

            int animId = static_cast<int>(state.currentAnimation);
            if (ImGui::InputInt("Current Animation", &animId)) {
                state.currentAnimation = static_cast<AnimationHandle>(std::max(0, animId));
            }

            int nextAnimId = static_cast<int>(state.nextAnimation);
            if (ImGui::InputInt("Next Animation", &nextAnimId)) {
                state.nextAnimation = static_cast<AnimationHandle>(std::max(-1, nextAnimId));
            }

            ImGui::DragFloat("Time", &state.time, 0.01f, 0.0f, 100.0f, "%.2f s");
            ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 5.0f, "%.2f x");
            ImGui::DragFloat("Blend Weight", &state.blendWeight, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::DragFloat("Blend Duration", &state.blendDuration, 0.01f, 0.01f, 2.0f, "%.2f s");
            ImGui::Checkbox("Looping", &state.looping);
            ImGui::Checkbox("Playing", &state.playing);
        }
    }

    void renderFootIKComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<FootIK>(entity)) return;

        if (renderComponentHeader("Foot IK")) {
            auto& footIK = registry.get<FootIK>(entity);

            ImGui::Checkbox("Enabled", &footIK.enabled);
            ImGui::DragFloat("Pelvis Offset", &footIK.pelvisOffset, 0.01f, -1.0f, 1.0f, "%.2f m");

            if (ImGui::TreeNode("Left Foot")) {
                editVec3("Position", footIK.leftFoot.position);
                editVec3("Normal", footIK.leftFoot.normal, 0.01f);
                ImGui::DragFloat("Weight", &footIK.leftFoot.weight, 0.01f, 0.0f, 1.0f);
                ImGui::Checkbox("Active", &footIK.leftFoot.active);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Right Foot")) {
                editVec3("Position", footIK.rightFoot.position);
                editVec3("Normal", footIK.rightFoot.normal, 0.01f);
                ImGui::DragFloat("Weight", &footIK.rightFoot.weight, 0.01f, 0.0f, 1.0f);
                ImGui::Checkbox("Active", &footIK.rightFoot.active);
                ImGui::TreePop();
            }
        }
    }

    void renderLookAtIKComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<LookAtIK>(entity)) return;

        if (renderComponentHeader("Look-At IK")) {
            auto& lookAt = registry.get<LookAtIK>(entity);

            ImGui::Checkbox("Enabled", &lookAt.enabled);

            // Target entity selector
            uint32_t targetId = (lookAt.target != entt::null) ? static_cast<uint32_t>(lookAt.target) : ~0u;
            ImGui::TextDisabled("Target Entity: %s",
                (lookAt.target != entt::null) ? std::to_string(targetId).c_str() : "None");

            editVec3("Target Position", lookAt.targetPosition);
            ImGui::DragFloat("Weight", &lookAt.weight, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Max Yaw", &lookAt.maxYaw, 1.0f, 0.0f, 180.0f, "%.0f deg");
            ImGui::DragFloat("Max Pitch", &lookAt.maxPitch, 1.0f, 0.0f, 90.0f, "%.0f deg");
        }
    }

    void renderParticleEmitterComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<ParticleEmitter>(entity)) return;

        if (renderComponentHeader("Particle Emitter")) {
            auto& emitter = registry.get<ParticleEmitter>(entity);

            int sysId = static_cast<int>(emitter.system);
            if (ImGui::InputInt("System Handle", &sysId)) {
                emitter.system = static_cast<ParticleSystemHandle>(std::max(0, sysId));
            }

            ImGui::Checkbox("Playing", &emitter.playing);
            ImGui::Checkbox("Looping", &emitter.looping);
            ImGui::DragFloat("Playback Speed", &emitter.playbackSpeed, 0.01f, 0.0f, 5.0f, "%.2f x");
            ImGui::DragFloat("Elapsed Time", &emitter.elapsedTime, 0.1f, 0.0f, 1000.0f, "%.1f s");

            int maxP = static_cast<int>(emitter.maxParticles);
            if (ImGui::InputInt("Max Particles", &maxP)) {
                emitter.maxParticles = static_cast<uint32_t>(std::max(1, maxP));
            }

            ImGui::Separator();
            ImGui::Text("Emission");

            const char* shapeNames[] = {"Point", "Sphere", "Box", "Cone"};
            int shape = static_cast<int>(emitter.emitShape);
            if (ImGui::Combo("Shape", &shape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                emitter.emitShape = static_cast<ParticleEmitter::Shape>(shape);
            }

            ImGui::DragFloat("Emit Radius", &emitter.emitRadius, 0.1f, 0.0f, 100.0f);
            editVec3("Emit Size", emitter.emitSize);
            ImGui::DragFloat("Emit Rate", &emitter.emitRate, 1.0f, 0.0f, 10000.0f, "%.0f /s");
            ImGui::DragFloat("Burst Count", &emitter.burstCount, 1.0f, 0.0f, 1000.0f);
        }
    }

    void renderPhysicsMaterialComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<PhysicsMaterial>(entity)) return;

        if (renderComponentHeader("Physics Material")) {
            auto& mat = registry.get<PhysicsMaterial>(entity);

            ImGui::DragFloat("Friction", &mat.friction, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Restitution", &mat.restitution, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Density", &mat.density, 0.1f, 0.01f, 100.0f, "%.2f kg/m3");
        }
    }

    // ========================================================================
    // Environment Component Editors (Phase 5)
    // ========================================================================

    void renderTerrainPatchComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<TerrainPatch>(entity)) return;

        if (renderComponentHeader("Terrain Patch")) {
            auto& patch = registry.get<TerrainPatch>(entity);

            ImGui::InputInt("Tile X", &patch.tileX);
            ImGui::InputInt("Tile Z", &patch.tileZ);

            int lod = static_cast<int>(patch.lod);
            if (ImGui::InputInt("LOD", &lod)) {
                patch.lod = static_cast<uint32_t>(std::max(0, lod));
            }

            ImGui::DragFloat("World Size", &patch.worldSize, 1.0f, 1.0f, 1024.0f, "%.0f m");
            ImGui::DragFloat("Height Scale", &patch.heightScale, 1.0f, 0.1f, 1000.0f);
            ImGui::Checkbox("Has Holes", &patch.hasHoles);
            ImGui::Checkbox("Visible", &patch.visible);
            ImGui::TextDisabled("Array Layer: %d", patch.arrayLayerIndex);
        }
    }

    void renderGrassVolumeComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<GrassVolume>(entity)) return;

        if (renderComponentHeader("Grass Volume")) {
            auto& grass = registry.get<GrassVolume>(entity);

            editVec3("Center", grass.center);
            editVec3("Extents", grass.extents);
            ImGui::DragFloat("Density", &grass.density, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("Height Min", &grass.heightMin, 0.01f, 0.01f, 1.0f, "%.2f m");
            ImGui::DragFloat("Height Max", &grass.heightMax, 0.01f, 0.01f, 2.0f, "%.2f m");
            ImGui::DragFloat("Spacing", &grass.spacing, 0.01f, 0.1f, 2.0f, "%.2f m");

            int lod = static_cast<int>(grass.lod);
            if (ImGui::InputInt("LOD", &lod)) {
                grass.lod = static_cast<uint32_t>(std::clamp(lod, 0, 2));
            }

            ImGui::Checkbox("Wind Enabled", &grass.windEnabled);
            ImGui::Checkbox("Snow Mask Enabled", &grass.snowMaskEnabled);
        }
    }

    void renderWaterSurfaceComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<WaterSurface>(entity)) return;

        if (renderComponentHeader("Water Surface")) {
            auto& water = registry.get<WaterSurface>(entity);

            const char* typeNames[] = {"Ocean", "Coastal Ocean", "River", "Muddy River",
                                       "Clear Stream", "Lake", "Swamp", "Tropical", "Custom"};
            int type = static_cast<int>(water.type);
            if (ImGui::Combo("Type", &type, typeNames, IM_ARRAYSIZE(typeNames))) {
                water.type = static_cast<WaterType>(type);
            }

            ImGui::DragFloat("Height", &water.height, 0.1f, -100.0f, 1000.0f, "%.1f m");
            ImGui::DragFloat("Depth", &water.depth, 0.5f, 0.1f, 500.0f, "%.1f m");

            float color[4] = {water.color.r, water.color.g, water.color.b, water.color.a};
            if (ImGui::ColorEdit4("Color", color)) {
                water.color = glm::vec4(color[0], color[1], color[2], color[3]);
            }

            if (ImGui::TreeNode("Wave Parameters")) {
                ImGui::DragFloat("Amplitude", &water.waveAmplitude, 0.1f, 0.0f, 10.0f);
                ImGui::DragFloat("Wavelength", &water.waveLength, 1.0f, 1.0f, 200.0f);
                ImGui::DragFloat("Steepness", &water.waveSteepness, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Speed", &water.waveSpeed, 0.1f, 0.0f, 10.0f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Material")) {
                ImGui::DragFloat("Specular Roughness", &water.specularRoughness, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Absorption", &water.absorptionScale, 0.1f, 0.0f, 10.0f);
                ImGui::DragFloat("Scattering", &water.scatteringScale, 0.1f, 0.0f, 10.0f);
                ImGui::DragFloat("Fresnel Power", &water.fresnelPower, 0.1f, 0.1f, 20.0f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Features")) {
                ImGui::Checkbox("FFT Ocean", &water.hasFFT);
                ImGui::Checkbox("Caustics", &water.hasCaustics);
                ImGui::Checkbox("Foam", &water.hasFoam);
                ImGui::Checkbox("Flow Map", &water.hasFlowMap);
                if (water.hasFlowMap) {
                    ImGui::DragFloat("Flow Strength", &water.flowStrength, 0.1f, 0.0f, 5.0f);
                    ImGui::DragFloat("Flow Speed", &water.flowSpeed, 0.1f, 0.0f, 5.0f);
                }
                ImGui::Checkbox("Tidal", &water.tidalEnabled);
                if (water.tidalEnabled) {
                    ImGui::DragFloat("Tidal Range", &water.tidalRange, 0.1f, 0.0f, 10.0f, "%.1f m");
                }
                ImGui::TreePop();
            }
        }
    }

    void renderTreeInstanceComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<TreeInstance>(entity)) return;

        if (renderComponentHeader("Tree Instance")) {
            auto& tree = registry.get<TreeInstance>(entity);

            const char* archetypes[] = {"Oak", "Pine", "Ash", "Aspen", "Birch", "Custom"};
            int arch = static_cast<int>(tree.archetype);
            if (ImGui::Combo("Archetype", &arch, archetypes, IM_ARRAYSIZE(archetypes))) {
                tree.archetype = static_cast<TreeArchetype>(arch);
            }

            ImGui::DragFloat("Scale", &tree.scale, 0.1f, 0.1f, 10.0f);
            ImGui::DragFloat("Rotation", &tree.rotation, 1.0f, 0.0f, 360.0f, "%.0f deg");

            int mesh = static_cast<int>(tree.meshIndex);
            if (ImGui::InputInt("Mesh Index", &mesh)) {
                tree.meshIndex = static_cast<uint32_t>(std::max(0, mesh));
            }

            int imp = static_cast<int>(tree.impostorIndex);
            if (ImGui::InputInt("Impostor Index", &imp)) {
                tree.impostorIndex = static_cast<uint32_t>(std::max(0, imp));
            }

            ImGui::Checkbox("Has Collision", &tree.hasCollision);
            ImGui::Checkbox("Casts Shadow", &tree.castsShadow);

            // Show LOD state if present
            if (registry.all_of<TreeLODState>(entity)) {
                auto& lod = registry.get<TreeLODState>(entity);
                ImGui::Separator();
                const char* lodLevels[] = {"Full Detail", "Impostor", "Blending"};
                ImGui::TextDisabled("LOD: %s", lodLevels[static_cast<int>(lod.level)]);
                ImGui::TextDisabled("Blend: %.2f", lod.blendFactor);
                ImGui::TextDisabled("Distance: %.1f m", lod.distanceToCamera);
            }
        }
    }

    void renderVegetationZoneComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<VegetationZone>(entity)) return;

        if (renderComponentHeader("Vegetation Zone")) {
            auto& zone = registry.get<VegetationZone>(entity);

            editVec3("Center", zone.center);
            editVec3("Extents", zone.extents);
            ImGui::DragFloat("Tree Density", &zone.treeDensity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Bush Density", &zone.bushDensity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Grass Density", &zone.grassDensity, 0.1f, 0.0f, 5.0f);
            ImGui::Checkbox("Auto Populate", &zone.autoPopulate);

            if (ImGui::TreeNode("Allowed Trees")) {
                ImGui::Text("%zu archetypes", zone.allowedTrees.size());
                ImGui::TreePop();
            }
        }
    }

    void renderWindZoneComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<WindZone>(entity)) return;

        if (renderComponentHeader("Wind Zone")) {
            auto& wind = registry.get<WindZone>(entity);

            editVec3("Direction", wind.direction, 0.01f);
            // Normalize direction
            if (glm::length(wind.direction) > 0.001f) {
                wind.direction = glm::normalize(wind.direction);
            }

            ImGui::DragFloat("Strength", &wind.strength, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Turbulence", &wind.turbulence, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Gust Frequency", &wind.gustFrequency, 0.1f, 0.0f, 5.0f, "%.1f Hz");
            ImGui::DragFloat("Gust Strength", &wind.gustStrength, 0.1f, 0.0f, 10.0f);

            if (!wind.isGlobal) {
                editVec3("Extents", wind.extents);
            }
            ImGui::Checkbox("Global", &wind.isGlobal);
        }
    }

    void renderWeatherZoneComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<WeatherZone>(entity)) return;

        if (renderComponentHeader("Weather Zone")) {
            auto& weather = registry.get<WeatherZone>(entity);

            const char* types[] = {"Clear", "Cloudy", "Rain", "Snow", "Fog", "Storm"};
            int type = static_cast<int>(weather.type);
            if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types))) {
                weather.type = static_cast<WeatherZone::Type>(type);
            }

            ImGui::DragFloat("Intensity", &weather.intensity, 0.1f, 0.0f, 2.0f);
            ImGui::DragFloat("Transition Radius", &weather.transitionRadius, 1.0f, 0.0f, 100.0f, "%.0f m");
            editVec3("Extents", weather.extents);
        }
    }

    void renderFogVolumeComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<FogVolume>(entity)) return;

        if (renderComponentHeader("Fog Volume")) {
            auto& fog = registry.get<FogVolume>(entity);

            editVec3("Extents", fog.extents);
            ImGui::DragFloat("Density", &fog.density, 0.001f, 0.0f, 1.0f, "%.3f");
            editColor3("Color", fog.color);
            ImGui::DragFloat("Height Falloff", &fog.heightFalloff, 0.001f, 0.0f, 0.1f, "%.4f");
            ImGui::Checkbox("Global", &fog.isGlobal);
        }
    }

    // ========================================================================
    // Occlusion Culling Component Editors (Phase 6)
    // ========================================================================

    void renderOcclusionCullableComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<OcclusionCullable>(entity)) return;

        if (renderComponentHeader("Occlusion Cullable")) {
            auto& cullable = registry.get<OcclusionCullable>(entity);

            ImGui::TextDisabled("Cull Index: %u", cullable.cullIndex);
            ImGui::TextDisabled("Was Visible: %s", cullable.wasVisibleLastFrame ? "Yes" : "No");
            ImGui::TextDisabled("Invisible Frames: %u", cullable.invisibleFrames);

            // Status indicator
            if (cullable.wasVisibleLastFrame) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "VISIBLE");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "CULLED");
            }
        }
    }

    void renderCullBoundingSphereComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<CullBoundingSphere>(entity)) return;

        if (renderComponentHeader("Cull Bounding Sphere")) {
            auto& sphere = registry.get<CullBoundingSphere>(entity);

            editVec3("Center Offset", sphere.center);
            ImGui::DragFloat("Radius", &sphere.radius, 0.1f, 0.01f, 1000.0f, "%.2f m");
        }
    }

    void renderOccluderComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Occluder>(entity)) return;

        if (renderComponentHeader("Occluder")) {
            auto& occluder = registry.get<Occluder>(entity);

            const char* shapes[] = {"Box", "Convex Hull", "Portal"};
            int shape = static_cast<int>(occluder.shape);
            if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
                occluder.shape = static_cast<Occluder::Shape>(shape);
            }

            ImGui::Checkbox("Always Occlude", &occluder.alwaysOcclude);

            // Show occluder status
            bool isOccluder = registry.all_of<IsOccluder>(entity);
            ImGui::TextDisabled("Active: %s", isOccluder ? "Yes" : "No");
        }
    }

    void renderVisibilityCellComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<VisibilityCell>(entity)) return;

        if (renderComponentHeader("Visibility Cell")) {
            auto& cell = registry.get<VisibilityCell>(entity);

            int cellId = static_cast<int>(cell.cellId);
            if (ImGui::InputInt("Cell ID", &cellId)) {
                cell.cellId = static_cast<uint32_t>(std::max(0, cellId));
            }

            editVec3("Center", cell.center);
            editVec3("Extents", cell.extents);

            ImGui::Text("PVS Cells: %zu", cell.potentiallyVisibleCells.size());

            if (ImGui::TreeNode("Visible Cells")) {
                for (size_t i = 0; i < cell.potentiallyVisibleCells.size(); i++) {
                    ImGui::Text("  Cell %u", cell.potentiallyVisibleCells[i]);
                }
                if (cell.potentiallyVisibleCells.empty()) {
                    ImGui::TextDisabled("  (none)");
                }
                ImGui::TreePop();
            }
        }
    }

    void renderCullingGroupComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<CullingGroup>(entity)) return;

        if (renderComponentHeader("Culling Group")) {
            auto& group = registry.get<CullingGroup>(entity);

            int groupId = static_cast<int>(group.groupId);
            if (ImGui::InputInt("Group ID", &groupId)) {
                group.groupId = static_cast<uint32_t>(std::max(0, groupId));
            }

            int priority = static_cast<int>(group.priority);
            if (ImGui::InputInt("Priority", &priority)) {
                group.priority = static_cast<uint32_t>(std::max(0, priority));
            }
        }
    }

    // ========================================================================
    // Extended Rendering Component Editors (Phase 7)
    // ========================================================================

    void renderDecalComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<Decal>(entity)) return;

        if (renderComponentHeader("Decal")) {
            auto& decal = registry.get<Decal>(entity);

            int matId = static_cast<int>(decal.material);
            if (ImGui::InputInt("Material Handle", &matId)) {
                decal.material = static_cast<MaterialHandle>(std::max(0, matId));
            }

            editVec3("Size", decal.size);
            ImGui::DragFloat("Fade Distance", &decal.fadeDistance, 0.5f, 0.0f, 100.0f, "%.1f m");
            ImGui::DragFloat("Angle Fade", &decal.angleFade, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Depth Bias", &decal.depthBias, 0.0001f, 0.0f, 0.01f, "%.4f");
            ImGui::InputInt("Sort Order", &decal.sortOrder);

            ImGui::Separator();
            ImGui::Text("Affects:");
            ImGui::Checkbox("Albedo", &decal.affectsAlbedo);
            ImGui::SameLine();
            ImGui::Checkbox("Normal", &decal.affectsNormal);
            ImGui::SameLine();
            ImGui::Checkbox("Roughness", &decal.affectsRoughness);
        }
    }

    void renderSpriteRendererComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<SpriteRenderer>(entity)) return;

        if (renderComponentHeader("Sprite Renderer")) {
            auto& sprite = registry.get<SpriteRenderer>(entity);

            int texId = static_cast<int>(sprite.texture);
            if (ImGui::InputInt("Texture Handle", &texId)) {
                sprite.texture = static_cast<TextureHandle>(std::max(0, texId));
            }

            int atlasId = static_cast<int>(sprite.atlasTexture);
            if (ImGui::InputInt("Atlas Texture", &atlasId)) {
                sprite.atlasTexture = static_cast<TextureHandle>(std::max(0, atlasId));
            }

            float size[2] = {sprite.size.x, sprite.size.y};
            if (ImGui::DragFloat2("Size", size, 0.1f, 0.01f, 100.0f)) {
                sprite.size = glm::vec2(size[0], size[1]);
            }

            float color[4] = {sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a};
            if (ImGui::ColorEdit4("Color", color)) {
                sprite.color = glm::vec4(color[0], color[1], color[2], color[3]);
            }

            const char* modes[] = {"None", "Face Camera", "Face Camera Y", "Fixed"};
            int mode = static_cast<int>(sprite.mode);
            if (ImGui::Combo("Billboard Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
                sprite.mode = static_cast<SpriteRenderer::Mode>(mode);
            }

            if (ImGui::TreeNode("Animation")) {
                int frames = static_cast<int>(sprite.frameCount);
                if (ImGui::InputInt("Frame Count", &frames)) {
                    sprite.frameCount = static_cast<uint32_t>(std::max(1, frames));
                }

                int current = static_cast<int>(sprite.currentFrame);
                if (ImGui::SliderInt("Current Frame", &current, 0, static_cast<int>(sprite.frameCount) - 1)) {
                    sprite.currentFrame = static_cast<uint32_t>(current);
                }

                ImGui::DragFloat("FPS", &sprite.framesPerSecond, 1.0f, 0.1f, 60.0f);
                ImGui::Checkbox("Animating", &sprite.animating);
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &sprite.loopAnimation);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Rendering")) {
                ImGui::Checkbox("Casts Shadow", &sprite.castsShadow);
                ImGui::Checkbox("Receives Shadow", &sprite.receiveShadow);
                ImGui::DragFloat("Sort Offset", &sprite.sortOffset, 0.01f, -10.0f, 10.0f);
                ImGui::TreePop();
            }
        }
    }

    void renderRenderTargetComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<RenderTarget>(entity)) return;

        if (renderComponentHeader("Render Target")) {
            auto& rt = registry.get<RenderTarget>(entity);

            int w = static_cast<int>(rt.width);
            int h = static_cast<int>(rt.height);
            if (ImGui::InputInt("Width", &w)) {
                rt.width = static_cast<uint32_t>(std::clamp(w, 1, 4096));
            }
            if (ImGui::InputInt("Height", &h)) {
                rt.height = static_cast<uint32_t>(std::clamp(h, 1, 4096));
            }

            const char* formats[] = {"RGBA8", "RGBA16F", "R32F", "Depth"};
            int format = static_cast<int>(rt.colorFormat);
            if (ImGui::Combo("Format", &format, formats, IM_ARRAYSIZE(formats))) {
                rt.colorFormat = static_cast<RenderTarget::Format>(format);
            }

            ImGui::Checkbox("Has Depth", &rt.hasDepth);

            const char* updateModes[] = {"Every Frame", "On Demand", "Interval"};
            int updateMode = static_cast<int>(rt.updateMode);
            if (ImGui::Combo("Update Mode", &updateMode, updateModes, IM_ARRAYSIZE(updateModes))) {
                rt.updateMode = static_cast<RenderTarget::UpdateMode>(updateMode);
            }

            if (rt.updateMode == RenderTarget::UpdateMode::Interval) {
                ImGui::DragFloat("Update Interval", &rt.updateInterval, 0.01f, 0.0f, 10.0f, "%.2f s");
            }

            if (rt.updateMode == RenderTarget::UpdateMode::OnDemand) {
                if (ImGui::Button("Request Update")) {
                    rt.needsUpdate = true;
                }
            }
        }
    }

    void renderReflectionProbeComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<ReflectionProbe>(entity)) return;

        if (renderComponentHeader("Reflection Probe")) {
            auto& probe = registry.get<ReflectionProbe>(entity);

            editVec3("Extents", probe.extents);
            ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.1f, 0.0f, 20.0f, "%.1f m");
            ImGui::DragFloat("Intensity", &probe.intensity, 0.1f, 0.0f, 5.0f);
            ImGui::InputInt("Priority", &probe.priority);

            const char* resolutions[] = {"64 (Low)", "128 (Medium)", "256 (High)", "512 (Very High)"};
            int res = static_cast<int>(probe.resolution);
            if (ImGui::Combo("Resolution", &res, resolutions, IM_ARRAYSIZE(resolutions))) {
                probe.resolution = static_cast<ReflectionProbe::Resolution>(res);
            }

            ImGui::Checkbox("Use Box Projection", &probe.useBoxProjection);
            if (probe.useBoxProjection) {
                editVec3("Box Offset", probe.boxProjection);
            }

            ImGui::Separator();
            ImGui::Checkbox("Realtime", &probe.realtime);
            if (probe.realtime) {
                ImGui::DragFloat("Update Interval", &probe.updateInterval, 0.1f, 0.0f, 10.0f, "%.1f s");
            }

            if (ImGui::Button("Force Capture")) {
                probe.needsCapture = true;
            }
        }
    }

    void renderLightProbeComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<LightProbe>(entity)) return;

        if (renderComponentHeader("Light Probe")) {
            auto& probe = registry.get<LightProbe>(entity);

            ImGui::DragFloat("Influence", &probe.influence, 0.5f, 0.1f, 100.0f, "%.1f m");
            ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.1f, 0.0f, 20.0f, "%.1f m");
            ImGui::InputInt("Priority", &probe.priority);

            ImGui::Separator();
            ImGui::Checkbox("Realtime", &probe.realtime);
            if (probe.realtime) {
                ImGui::DragFloat("Update Interval", &probe.updateInterval, 0.1f, 0.1f, 10.0f, "%.1f s");
            }

            if (ImGui::Button("Force Capture")) {
                probe.needsCapture = true;
            }

            // Show ambient (L00) coefficient
            if (ImGui::TreeNode("SH Coefficients")) {
                ImGui::TextDisabled("Ambient (L00):");
                float ambient[3] = {probe.shCoefficients[0].r, probe.shCoefficients[0].g, probe.shCoefficients[0].b};
                if (ImGui::ColorEdit3("##ambient", ambient)) {
                    probe.shCoefficients[0] = glm::vec3(ambient[0], ambient[1], ambient[2]);
                }
                ImGui::TreePop();
            }
        }
    }

    void renderLightProbeVolumeComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<LightProbeVolume>(entity)) return;

        if (renderComponentHeader("Light Probe Volume")) {
            auto& volume = registry.get<LightProbeVolume>(entity);

            editVec3("Extents", volume.extents);

            int count[3] = {volume.probeCount.x, volume.probeCount.y, volume.probeCount.z};
            if (ImGui::InputInt3("Probe Count", count)) {
                volume.probeCount = glm::ivec3(
                    std::max(1, count[0]),
                    std::max(1, count[1]),
                    std::max(1, count[2])
                );
            }

            ImGui::DragFloat("Probe Spacing", &volume.probeSpacing, 0.5f, 0.5f, 50.0f, "%.1f m");
            ImGui::Checkbox("Interpolate", &volume.interpolate);

            int totalProbes = volume.probeCount.x * volume.probeCount.y * volume.probeCount.z;
            ImGui::TextDisabled("Total probes: %d", totalProbes);
        }
    }

    void renderPortalSurfaceComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<PortalSurface>(entity)) return;

        if (renderComponentHeader("Portal Surface")) {
            auto& portal = registry.get<PortalSurface>(entity);

            ImGui::Checkbox("Is Mirror", &portal.isMirror);

            if (!portal.isMirror) {
                uint32_t targetId = (portal.targetPortal != entt::null) ?
                    static_cast<uint32_t>(portal.targetPortal) : ~0u;
                ImGui::TextDisabled("Target Portal: %s",
                    (portal.targetPortal != entt::null) ?
                        std::to_string(targetId).c_str() : "None");
            }

            ImGui::Checkbox("Two Sided", &portal.twoSided);
            ImGui::DragFloat("Clip Plane Offset", &portal.clipPlaneOffset, 0.001f, 0.0f, 0.1f, "%.3f");
        }
    }

    // ========================================================================
    // Audio Component Editors (Phase 8)
    // ========================================================================

    void renderAudioSourceComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AudioSource>(entity)) return;

        if (renderComponentHeader("Audio Source")) {
            auto& source = registry.get<AudioSource>(entity);

            int clipId = static_cast<int>(source.clip);
            if (ImGui::InputInt("Clip Handle", &clipId)) {
                source.clip = static_cast<AudioClipHandle>(std::max(0, clipId));
            }

            // Playback controls
            ImGui::Separator();
            ImGui::Text("Playback");
            bool wasPlaying = source.playing;
            ImGui::Checkbox("Playing", &source.playing);
            ImGui::SameLine();
            ImGui::Checkbox("Looping", &source.looping);
            ImGui::SameLine();
            ImGui::Checkbox("Paused", &source.paused);

            ImGui::DragFloat("Position", &source.playbackPosition, 0.1f, 0.0f, 1000.0f, "%.1f s");

            // Volume controls
            ImGui::Separator();
            ImGui::Text("Volume");
            ImGui::DragFloat("Volume", &source.volume, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Pitch", &source.pitch, 0.01f, 0.5f, 2.0f);
            if (!source.spatialize) {
                ImGui::DragFloat("Pan", &source.pan, 0.01f, -1.0f, 1.0f);
            }

            // 3D settings
            ImGui::Separator();
            ImGui::Text("Spatialization");
            ImGui::Checkbox("3D Spatial", &source.spatialize);
            if (source.spatialize) {
                ImGui::DragFloat("Min Distance", &source.minDistance, 0.5f, 0.0f, 100.0f, "%.1f m");
                ImGui::DragFloat("Max Distance", &source.maxDistance, 1.0f, 1.0f, 500.0f, "%.0f m");

                const char* rolloffs[] = {"Linear", "Logarithmic", "Custom"};
                int rolloff = static_cast<int>(source.rolloff);
                if (ImGui::Combo("Rolloff", &rolloff, rolloffs, IM_ARRAYSIZE(rolloffs))) {
                    source.rolloff = static_cast<AudioSource::Rolloff>(rolloff);
                }

                if (source.rolloff == AudioSource::Rolloff::Custom) {
                    ImGui::DragFloat("Rolloff Factor", &source.rolloffFactor, 0.1f, 0.1f, 10.0f);
                }
            }

            // Doppler
            if (ImGui::TreeNode("Doppler")) {
                ImGui::Checkbox("Enabled", &source.dopplerEnabled);
                if (source.dopplerEnabled) {
                    ImGui::DragFloat("Factor", &source.dopplerFactor, 0.1f, 0.0f, 5.0f);
                }
                ImGui::TreePop();
            }

            // Cone attenuation
            if (ImGui::TreeNode("Cone Attenuation")) {
                ImGui::DragFloat("Inner Angle", &source.coneInnerAngle, 1.0f, 0.0f, 360.0f, "%.0f deg");
                ImGui::DragFloat("Outer Angle", &source.coneOuterAngle, 1.0f, 0.0f, 360.0f, "%.0f deg");
                ImGui::DragFloat("Outer Volume", &source.coneOuterVolume, 0.01f, 0.0f, 1.0f);
                ImGui::TreePop();
            }

            // Flags
            if (ImGui::TreeNode("Flags")) {
                ImGui::InputInt("Priority", &source.priority);
                ImGui::Checkbox("Play On Awake", &source.playOnAwake);
                ImGui::Checkbox("Destroy On Complete", &source.destroyOnComplete);
                ImGui::TreePop();
            }
        }
    }

    void renderAudioListenerComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AudioListener>(entity)) return;

        if (renderComponentHeader("Audio Listener")) {
            auto& listener = registry.get<AudioListener>(entity);

            ImGui::DragFloat("Volume", &listener.volume, 0.01f, 0.0f, 2.0f);
            ImGui::Checkbox("Active", &listener.active);

            // Show active status
            bool isActive = registry.all_of<ActiveAudioListener>(entity);
            if (isActive) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ACTIVE LISTENER");
            } else {
                if (ImGui::Button("Make Active")) {
                    // Remove from others
                    auto view = registry.view<ActiveAudioListener>();
                    for (auto e : view) {
                        registry.remove<ActiveAudioListener>(e);
                    }
                    registry.emplace<ActiveAudioListener>(entity);
                    listener.active = true;
                }
            }

            editVec3("Velocity", listener.velocity);
        }
    }

    void renderAmbientSoundZoneComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AmbientSoundZone>(entity)) return;

        if (renderComponentHeader("Ambient Sound Zone")) {
            auto& zone = registry.get<AmbientSoundZone>(entity);

            int clipId = static_cast<int>(zone.clip);
            if (ImGui::InputInt("Clip Handle", &clipId)) {
                zone.clip = static_cast<AudioClipHandle>(std::max(0, clipId));
            }

            editVec3("Extents", zone.extents);
            ImGui::DragFloat("Fade Distance", &zone.fadeDistance, 0.5f, 0.0f, 50.0f, "%.1f m");
            ImGui::DragFloat("Volume", &zone.volume, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Looping", &zone.looping);

            // Status
            ImGui::Separator();
            ImGui::TextDisabled("Inside: %s", zone.currentlyInside ? "Yes" : "No");
            ImGui::TextDisabled("Current Volume: %.2f", zone.currentVolume);
        }
    }

    void renderReverbZoneComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<ReverbZone>(entity)) return;

        if (renderComponentHeader("Reverb Zone")) {
            auto& reverb = registry.get<ReverbZone>(entity);

            editVec3("Extents", reverb.extents);
            ImGui::DragFloat("Fade Distance", &reverb.fadeDistance, 0.5f, 0.0f, 50.0f, "%.1f m");

            const char* presets[] = {"None", "Room", "Hallway", "Cave", "Arena", "Hangar", "Forest", "Underwater", "Custom"};
            int preset = static_cast<int>(reverb.preset);
            if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
                reverb.preset = static_cast<ReverbZone::Preset>(preset);
            }

            if (reverb.preset == ReverbZone::Preset::Custom) {
                ImGui::DragFloat("Decay Time", &reverb.decayTime, 0.1f, 0.1f, 20.0f, "%.1f s");
                ImGui::DragFloat("Early Reflections", &reverb.earlyReflections, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Late Reverb", &reverb.lateReverb, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Diffusion", &reverb.diffusion, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("Density", &reverb.density, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat("HF Decay", &reverb.hfDecayRatio, 0.01f, 0.0f, 2.0f);
            }

            ImGui::Separator();
            ImGui::TextDisabled("Blend Weight: %.2f", reverb.blendWeight);
        }
    }

    void renderMusicTrackComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<MusicTrack>(entity)) return;

        if (renderComponentHeader("Music Track")) {
            auto& music = registry.get<MusicTrack>(entity);

            int clipId = static_cast<int>(music.clip);
            if (ImGui::InputInt("Clip Handle", &clipId)) {
                music.clip = static_cast<AudioClipHandle>(std::max(0, clipId));
            }

            ImGui::DragFloat("Volume", &music.volume, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Playing", &music.playing);
            ImGui::SameLine();
            ImGui::Checkbox("Looping", &music.looping);

            ImGui::DragFloat("Fade In", &music.fadeInDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
            ImGui::DragFloat("Fade Out", &music.fadeOutDuration, 0.1f, 0.0f, 10.0f, "%.1f s");

            const char* states[] = {"Stopped", "Fading In", "Playing", "Fading Out", "Crossfading"};
            ImGui::TextDisabled("State: %s", states[static_cast<int>(music.state)]);
            ImGui::TextDisabled("Progress: %.2f", music.crossfadeProgress);
        }
    }

    void renderAudioMixerGroupComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AudioMixerGroup>(entity)) return;

        if (renderComponentHeader("Audio Mixer Group")) {
            auto& mixer = registry.get<AudioMixerGroup>(entity);

            const char* groups[] = {"Master", "Music", "SFX", "Voice", "Ambient", "UI", "Custom"};
            int group = static_cast<int>(mixer.group);
            if (ImGui::Combo("Group", &group, groups, IM_ARRAYSIZE(groups))) {
                mixer.group = static_cast<AudioMixerGroup::Group>(group);
            }

            ImGui::DragFloat("Group Volume", &mixer.groupVolume, 0.01f, 0.0f, 1.0f);
        }
    }

    void renderAudioOcclusionComponent(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<AudioOcclusion>(entity)) return;

        if (renderComponentHeader("Audio Occlusion")) {
            auto& occlusion = registry.get<AudioOcclusion>(entity);

            ImGui::DragFloat("Occlusion", &occlusion.occlusionFactor, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Low Pass", &occlusion.lowPassFilter, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Auto Calculate", &occlusion.autoCalculate);
            if (occlusion.autoCalculate) {
                ImGui::DragFloat("Update Interval", &occlusion.updateInterval, 0.01f, 0.01f, 1.0f, "%.2f s");
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
        if (registry.all_of<PhysicsKinematic>(entity)) tags.push_back("Physics Kinematic");
        if (registry.all_of<PhysicsTrigger>(entity)) tags.push_back("Physics Trigger");
        if (registry.all_of<NPCTag>(entity)) tags.push_back("NPC");
        if (registry.all_of<LightEnabled>(entity)) tags.push_back("Light Enabled");
        if (registry.all_of<Selected>(entity)) tags.push_back("Selected");
        if (registry.all_of<MainCamera>(entity)) tags.push_back("Main Camera");
        if (registry.all_of<StaticObject>(entity)) tags.push_back("Static");
        if (registry.all_of<WasVisible>(entity)) tags.push_back("Was Visible");
        if (registry.all_of<NeverCull>(entity)) tags.push_back("Never Cull");
        if (registry.all_of<ShadowOnly>(entity)) tags.push_back("Shadow Only");
        if (registry.all_of<IsOccluder>(entity)) tags.push_back("Is Occluder");
        if (registry.all_of<IsReflectionProbe>(entity)) tags.push_back("Reflection Probe");
        if (registry.all_of<IsLightProbe>(entity)) tags.push_back("Light Probe");
        if (registry.all_of<IsAudioSource>(entity)) tags.push_back("Audio Source");
        if (registry.all_of<ActiveAudioListener>(entity)) tags.push_back("Active Audio Listener");

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

            ImGui::Separator();
            ImGui::TextDisabled("Animation");

            if (!registry.all_of<SkinnedMeshRenderer>(entity)) {
                if (ImGui::MenuItem("Skinned Mesh Renderer")) {
                    registry.emplace<SkinnedMeshRenderer>(entity);
                }
            }
            if (!registry.all_of<Animator>(entity)) {
                if (ImGui::MenuItem("Animator")) {
                    registry.emplace<Animator>(entity);
                }
            }
            if (!registry.all_of<AnimationState>(entity)) {
                if (ImGui::MenuItem("Animation State")) {
                    registry.emplace<AnimationState>(entity);
                }
            }
            if (!registry.all_of<FootIK>(entity)) {
                if (ImGui::MenuItem("Foot IK")) {
                    registry.emplace<FootIK>(entity);
                }
            }
            if (!registry.all_of<LookAtIK>(entity)) {
                if (ImGui::MenuItem("Look-At IK")) {
                    registry.emplace<LookAtIK>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Physics");

            if (!registry.all_of<PhysicsMaterial>(entity)) {
                if (ImGui::MenuItem("Physics Material")) {
                    registry.emplace<PhysicsMaterial>(entity);
                }
            }
            if (!registry.all_of<PhysicsKinematic>(entity)) {
                if (ImGui::MenuItem("Physics Kinematic (Tag)")) {
                    registry.emplace<PhysicsKinematic>(entity);
                }
            }
            if (!registry.all_of<PhysicsTrigger>(entity)) {
                if (ImGui::MenuItem("Physics Trigger (Tag)")) {
                    registry.emplace<PhysicsTrigger>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Effects");

            if (!registry.all_of<ParticleEmitter>(entity)) {
                if (ImGui::MenuItem("Particle Emitter")) {
                    registry.emplace<ParticleEmitter>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Environment");

            if (!registry.all_of<TerrainPatch>(entity)) {
                if (ImGui::MenuItem("Terrain Patch")) {
                    registry.emplace<TerrainPatch>(entity);
                }
            }
            if (!registry.all_of<GrassVolume>(entity)) {
                if (ImGui::MenuItem("Grass Volume")) {
                    registry.emplace<GrassVolume>(entity);
                }
            }
            if (!registry.all_of<WaterSurface>(entity)) {
                if (ImGui::MenuItem("Water Surface")) {
                    registry.emplace<WaterSurface>(entity);
                }
            }
            if (!registry.all_of<TreeInstance>(entity)) {
                if (ImGui::MenuItem("Tree Instance")) {
                    registry.emplace<TreeInstance>(entity);
                    registry.emplace_or_replace<TreeLODState>(entity);
                }
            }
            if (!registry.all_of<VegetationZone>(entity)) {
                if (ImGui::MenuItem("Vegetation Zone")) {
                    VegetationZone zone;
                    zone.allowedTrees = {TreeArchetype::Oak, TreeArchetype::Pine};
                    registry.emplace<VegetationZone>(entity, std::move(zone));
                }
            }
            if (!registry.all_of<WindZone>(entity)) {
                if (ImGui::MenuItem("Wind Zone")) {
                    registry.emplace<WindZone>(entity);
                }
            }
            if (!registry.all_of<WeatherZone>(entity)) {
                if (ImGui::MenuItem("Weather Zone")) {
                    registry.emplace<WeatherZone>(entity);
                }
            }
            if (!registry.all_of<FogVolume>(entity)) {
                if (ImGui::MenuItem("Fog Volume")) {
                    registry.emplace<FogVolume>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Occlusion Culling");

            if (!registry.all_of<OcclusionCullable>(entity)) {
                if (ImGui::MenuItem("Occlusion Cullable")) {
                    registry.emplace<OcclusionCullable>(entity);
                    registry.emplace_or_replace<CullBoundingSphere>(entity);
                }
            }
            if (!registry.all_of<CullBoundingSphere>(entity)) {
                if (ImGui::MenuItem("Cull Bounding Sphere")) {
                    registry.emplace<CullBoundingSphere>(entity);
                }
            }
            if (!registry.all_of<Occluder>(entity)) {
                if (ImGui::MenuItem("Occluder")) {
                    registry.emplace<Occluder>(entity);
                    registry.emplace_or_replace<IsOccluder>(entity);
                }
            }
            if (!registry.all_of<VisibilityCell>(entity)) {
                if (ImGui::MenuItem("Visibility Cell")) {
                    registry.emplace<VisibilityCell>(entity);
                }
            }
            if (!registry.all_of<CullingGroup>(entity)) {
                if (ImGui::MenuItem("Culling Group")) {
                    registry.emplace<CullingGroup>(entity);
                }
            }
            if (!registry.all_of<NeverCull>(entity)) {
                if (ImGui::MenuItem("Never Cull (Tag)")) {
                    registry.emplace<NeverCull>(entity);
                }
            }
            if (!registry.all_of<ShadowOnly>(entity)) {
                if (ImGui::MenuItem("Shadow Only (Tag)")) {
                    registry.emplace<ShadowOnly>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Extended Rendering");

            if (!registry.all_of<Decal>(entity)) {
                if (ImGui::MenuItem("Decal")) {
                    registry.emplace<Decal>(entity);
                    registry.emplace_or_replace<AABBBounds>(entity);
                }
            }
            if (!registry.all_of<SpriteRenderer>(entity)) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    registry.emplace<SpriteRenderer>(entity);
                    registry.emplace_or_replace<BoundingSphere>(entity);
                }
            }
            if (!registry.all_of<RenderTarget>(entity)) {
                if (ImGui::MenuItem("Render Target")) {
                    registry.emplace<RenderTarget>(entity);
                }
            }
            if (!registry.all_of<ReflectionProbe>(entity)) {
                if (ImGui::MenuItem("Reflection Probe")) {
                    registry.emplace<ReflectionProbe>(entity);
                    registry.emplace_or_replace<IsReflectionProbe>(entity);
                    registry.emplace_or_replace<AABBBounds>(entity);
                }
            }
            if (!registry.all_of<LightProbe>(entity)) {
                if (ImGui::MenuItem("Light Probe")) {
                    registry.emplace<LightProbe>(entity);
                    registry.emplace_or_replace<IsLightProbe>(entity);
                    registry.emplace_or_replace<BoundingSphere>(entity);
                }
            }
            if (!registry.all_of<LightProbeVolume>(entity)) {
                if (ImGui::MenuItem("Light Probe Volume")) {
                    registry.emplace<LightProbeVolume>(entity);
                }
            }
            if (!registry.all_of<PortalSurface>(entity)) {
                if (ImGui::MenuItem("Portal/Mirror")) {
                    registry.emplace<PortalSurface>(entity);
                    registry.emplace_or_replace<RenderTarget>(entity);
                    registry.emplace_or_replace<MeshRenderer>(entity);
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Audio");

            if (!registry.all_of<AudioSource>(entity)) {
                if (ImGui::MenuItem("Audio Source")) {
                    registry.emplace<AudioSource>(entity);
                    registry.emplace_or_replace<IsAudioSource>(entity);
                }
            }
            if (!registry.all_of<AudioListener>(entity)) {
                if (ImGui::MenuItem("Audio Listener")) {
                    registry.emplace<AudioListener>(entity);
                }
            }
            if (!registry.all_of<AmbientSoundZone>(entity)) {
                if (ImGui::MenuItem("Ambient Sound Zone")) {
                    registry.emplace<AmbientSoundZone>(entity);
                    registry.emplace_or_replace<AABBBounds>(entity);
                }
            }
            if (!registry.all_of<ReverbZone>(entity)) {
                if (ImGui::MenuItem("Reverb Zone")) {
                    registry.emplace<ReverbZone>(entity);
                    registry.emplace_or_replace<AABBBounds>(entity);
                }
            }
            if (!registry.all_of<MusicTrack>(entity)) {
                if (ImGui::MenuItem("Music Track")) {
                    registry.emplace<MusicTrack>(entity);
                }
            }
            if (!registry.all_of<AudioMixerGroup>(entity)) {
                if (ImGui::MenuItem("Audio Mixer Group")) {
                    registry.emplace<AudioMixerGroup>(entity);
                }
            }
            if (!registry.all_of<AudioOcclusion>(entity)) {
                if (ImGui::MenuItem("Audio Occlusion")) {
                    registry.emplace<AudioOcclusion>(entity);
                }
            }

            ImGui::EndPopup();
        }
    }
};
