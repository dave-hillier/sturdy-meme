#include "GuiHierarchyPanel.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>

namespace {

// Get display name for an entity
std::string getEntityDisplayName(ecs::World& world, ecs::Entity entity) {
    // Check for DebugName component first
    if (world.has<ecs::DebugName>(entity)) {
        const auto& name = world.get<ecs::DebugName>(entity);
        if (name.name) {
            return name.name;
        }
    }

    // Generate name based on components/tags
    if (world.has<ecs::PlayerTag>(entity)) {
        return "Player";
    }
    if (world.has<ecs::CapeTag>(entity)) {
        return "Cape";
    }
    if (world.has<ecs::FlagPoleTag>(entity)) {
        return "Flag Pole";
    }
    if (world.has<ecs::FlagClothTag>(entity)) {
        return "Flag Cloth";
    }
    if (world.has<ecs::OrbTag>(entity)) {
        return "Emissive Orb";
    }
    if (world.has<ecs::WellEntranceTag>(entity)) {
        return "Well Entrance";
    }
    if (world.has<ecs::WeaponTag>(entity)) {
        auto& weapon = world.get<ecs::WeaponTag>(entity);
        return weapon.slot == ecs::WeaponSlot::RightHand ? "Sword" : "Shield";
    }
    if (world.has<ecs::NPCTag>(entity)) {
        return "NPC";
    }
    if (world.has<ecs::PointLightComponent>(entity)) {
        return "Point Light";
    }
    if (world.has<ecs::SpotLightComponent>(entity)) {
        return "Spot Light";
    }
    if (world.has<ecs::DirectionalLightComponent>(entity)) {
        return "Directional Light";
    }
    if (world.has<ecs::TreeData>(entity)) {
        return "Tree";
    }
    if (world.has<ecs::MeshRef>(entity)) {
        return "Mesh";
    }

    // Default: use entity ID
    return "Entity " + std::to_string(static_cast<uint32_t>(entity));
}

// Get icon character for entity type
const char* getEntityIcon(ecs::World& world, ecs::Entity entity) {
    if (world.has<ecs::PlayerTag>(entity)) return "[P]";
    if (world.has<ecs::CapeTag>(entity)) return "[C]";
    if (world.has<ecs::FlagPoleTag>(entity) || world.has<ecs::FlagClothTag>(entity)) return "[F]";
    if (world.has<ecs::OrbTag>(entity)) return "[O]";
    if (world.has<ecs::WeaponTag>(entity)) return "[W]";
    if (world.has<ecs::NPCTag>(entity)) return "[N]";
    if (world.has<ecs::LightSourceTag>(entity) ||
        world.has<ecs::PointLightComponent>(entity) ||
        world.has<ecs::SpotLightComponent>(entity) ||
        world.has<ecs::DirectionalLightComponent>(entity)) return "[L]";
    if (world.has<ecs::TreeData>(entity)) return "[T]";
    if (world.has<ecs::MeshRef>(entity)) return "[M]";
    return "[ ]";
}

// Get color for entity type
ImVec4 getEntityColor(ecs::World& world, ecs::Entity entity) {
    if (world.has<ecs::PlayerTag>(entity)) return ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
    if (world.has<ecs::NPCTag>(entity)) return ImVec4(0.3f, 0.7f, 0.9f, 1.0f);
    if (world.has<ecs::LightSourceTag>(entity) ||
        world.has<ecs::PointLightComponent>(entity) ||
        world.has<ecs::SpotLightComponent>(entity)) return ImVec4(1.0f, 0.9f, 0.4f, 1.0f);
    if (world.has<ecs::TreeData>(entity)) return ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
    if (world.has<ecs::OrbTag>(entity)) return ImVec4(1.0f, 0.6f, 0.3f, 1.0f);
    return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
}

// Check if entity matches filter
bool matchesFilter(ecs::World& world, ecs::Entity entity, const char* filter) {
    if (filter[0] == '\0') return true;

    std::string name = getEntityDisplayName(world, entity);

    // Convert to lowercase for case-insensitive search
    std::string lowerName = name;
    std::string lowerFilter = filter;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    return lowerName.find(lowerFilter) != std::string::npos;
}

// Recursive function to render entity and its children as a tree
void renderEntityNode(ecs::World& world, ecs::Entity entity, SceneEditorState& state, const char* filter) {
    // Skip if filtered out (but still check children)
    bool passesFilter = matchesFilter(world, entity, filter);

    // Check if any children pass the filter
    bool hasVisibleChildren = false;
    if (world.has<ecs::Children>(entity)) {
        const auto& children = world.get<ecs::Children>(entity);
        for (ecs::Entity child : children.entities) {
            if (matchesFilter(world, child, filter)) {
                hasVisibleChildren = true;
                break;
            }
        }
    }

    // Skip this node entirely if it doesn't pass filter and has no visible children
    if (!passesFilter && !hasVisibleChildren) return;

    std::string name = getEntityDisplayName(world, entity);
    const char* icon = getEntityIcon(world, entity);
    ImVec4 color = getEntityColor(world, entity);

    bool hasChildren = world.has<ecs::Children>(entity) && !world.get<ecs::Children>(entity).empty();
    bool isSelected = state.isSelected(entity);
    bool isExpanded = state.isExpanded(entity);

    // Determine tree node flags
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (isExpanded && hasChildren) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    // If filter is active and this node doesn't match but has matching children, auto-expand
    if (filter[0] != '\0' && !passesFilter && hasVisibleChildren) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // Push unique ID
    ImGui::PushID(static_cast<int>(entity));

    // Draw the tree node
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    std::string label = std::string(icon) + " " + name;

    bool nodeOpen = false;
    if (hasChildren) {
        nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
    } else {
        ImGui::TreeNodeEx(label.c_str(), flags);
    }
    ImGui::PopStyleColor();

    // Handle selection
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeyCtrl) {
            // Ctrl+click: add to selection
            if (state.isSelected(entity)) {
                // Remove from selection
                if (state.selectedEntity == entity) {
                    state.selectedEntity = ecs::NullEntity;
                }
                auto& multi = state.multiSelection;
                multi.erase(std::remove(multi.begin(), multi.end(), entity), multi.end());
            } else {
                state.addToSelection(entity);
            }
        } else {
            // Normal click: single select
            state.select(entity);
        }
    }

    // Track expand/collapse state
    if (hasChildren && ImGui::IsItemToggledOpen()) {
        state.toggleExpanded(entity);
    }

    // Drag source for reparenting
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("ENTITY_DRAG", &entity, sizeof(ecs::Entity));
        ImGui::Text("Move %s", name.c_str());
        state.draggedEntity = entity;
        ImGui::EndDragDropSource();
    }

    // Drop target for reparenting
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
            ecs::Entity droppedEntity = *static_cast<const ecs::Entity*>(payload->Data);
            if (droppedEntity != entity) {
                // Reparent: attach droppedEntity to this entity
                ecs::systems::attachToParent(world, droppedEntity, entity);

                // Ensure parent has Children component
                if (!world.has<ecs::Children>(entity)) {
                    world.add<ecs::Children>(entity);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Tooltip with entity info
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(entity));
        if (world.has<ecs::Transform>(entity)) {
            const auto& transform = world.get<ecs::Transform>(entity);
            glm::vec3 pos = transform.position();
            ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        }
        ImGui::EndTooltip();
    }

    // Render children recursively
    if (nodeOpen && hasChildren) {
        const auto& children = world.get<ecs::Children>(entity);
        for (ecs::Entity child : children.entities) {
            if (world.valid(child)) {
                renderEntityNode(world, child, state, filter);
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // anonymous namespace

void GuiHierarchyPanel::renderCreateMenuBar(ISceneControl& sceneControl, SceneEditorState& state) {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("Create")) {
        ecs::World* world = sceneControl.getECSWorld();
        if (world) {
            if (ImGui::MenuItem("Empty Entity")) {
                ecs::Entity e = world->create();
                world->add<ecs::Transform>(e);
                world->add<ecs::LocalTransform>(e);
                world->add<ecs::DebugName>(e, "Empty");
                state.select(e);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Point Light")) {
                ecs::Entity e = world->create();
                world->add<ecs::Transform>(e);
                world->add<ecs::LocalTransform>(e);
                world->add<ecs::PointLightComponent>(e, glm::vec3(1.0f), 1.0f, 10.0f);
                world->add<ecs::LightSourceTag>(e);
                world->add<ecs::DebugName>(e, "New Point Light");
                state.select(e);
            }
            if (ImGui::MenuItem("Spot Light")) {
                ecs::Entity e = world->create();
                world->add<ecs::Transform>(e);
                world->add<ecs::LocalTransform>(e);
                world->add<ecs::SpotLightComponent>(e, glm::vec3(1.0f), 1.0f);
                world->add<ecs::LightSourceTag>(e);
                world->add<ecs::DebugName>(e, "New Spot Light");
                state.select(e);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void GuiHierarchyPanel::render(ISceneControl& sceneControl, SceneEditorState& state) {
    ecs::World* world = sceneControl.getECSWorld();

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("HIERARCHY");
    ImGui::PopStyleColor();

    if (!world) {
        ImGui::TextDisabled("ECS World not available");
        return;
    }

    // Entity count
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu entities)", world->size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar
    if (ImGui::Button("Create")) {
        state.showCreateEntityPopup = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && state.selectedEntity != ecs::NullEntity) {
        // Delete selected entity
        ecs::systems::detachFromParent(*world, state.selectedEntity);
        world->destroy(state.selectedEntity);
        state.clearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Expand All")) {
        // Expand all nodes
        for (auto entity : world->view<ecs::Children>()) {
            state.setExpanded(entity, true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Collapse All")) {
        state.expandedNodes.clear();
    }

    // Create entity popup
    if (state.showCreateEntityPopup) {
        ImGui::OpenPopup("Create Entity");
        state.showCreateEntityPopup = false;
    }

    if (ImGui::BeginPopup("Create Entity")) {
        if (ImGui::MenuItem("Empty Entity")) {
            ecs::Entity newEntity = world->create();
            world->add<ecs::Transform>(newEntity);
            world->add<ecs::LocalTransform>(newEntity);
            state.select(newEntity);
        }
        if (ImGui::MenuItem("Empty Child") && state.selectedEntity != ecs::NullEntity) {
            ecs::Entity newEntity = world->create();
            world->add<ecs::Transform>(newEntity);
            world->add<ecs::LocalTransform>(newEntity);
            ecs::systems::attachToParent(*world, newEntity, state.selectedEntity);
            if (!world->has<ecs::Children>(state.selectedEntity)) {
                world->add<ecs::Children>(state.selectedEntity);
            }
            state.select(newEntity);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light")) {
            ecs::Entity newEntity = world->create();
            world->add<ecs::Transform>(newEntity);
            world->add<ecs::LocalTransform>(newEntity);
            world->add<ecs::PointLightComponent>(newEntity, glm::vec3(1.0f), 1.0f, 10.0f);
            world->add<ecs::LightSourceTag>(newEntity);
            state.select(newEntity);
        }
        if (ImGui::MenuItem("Spot Light")) {
            ecs::Entity newEntity = world->create();
            world->add<ecs::Transform>(newEntity);
            world->add<ecs::LocalTransform>(newEntity);
            world->add<ecs::SpotLightComponent>(newEntity, glm::vec3(1.0f), 1.0f);
            world->add<ecs::LightSourceTag>(newEntity);
            state.select(newEntity);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    // Filter input
    if (state.showHierarchyFilter) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##hierarchyFilter", "Filter...", state.hierarchyFilterText, sizeof(state.hierarchyFilterText));
        ImGui::Spacing();
    }

    // Entity tree
    if (ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), true)) {
        // Collect root entities (no parent)
        std::vector<ecs::Entity> rootEntities;
        for (auto entity : world->view<ecs::Transform>(entt::exclude<ecs::Parent>)) {
            rootEntities.push_back(entity);
        }

        // Sort root entities by type for better organization
        std::sort(rootEntities.begin(), rootEntities.end(), [&](ecs::Entity a, ecs::Entity b) {
            // Priority order: Player > NPCs > Lights > Trees > Others
            auto getPriority = [&](ecs::Entity e) -> int {
                if (world->has<ecs::PlayerTag>(e)) return 0;
                if (world->has<ecs::NPCTag>(e)) return 1;
                if (world->has<ecs::LightSourceTag>(e)) return 2;
                if (world->has<ecs::TreeData>(e)) return 3;
                return 4;
            };
            int pa = getPriority(a);
            int pb = getPriority(b);
            if (pa != pb) return pa < pb;
            return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
        });

        // Render each root entity
        for (ecs::Entity entity : rootEntities) {
            if (world->valid(entity)) {
                renderEntityNode(*world, entity, state, state.hierarchyFilterText);
            }
        }

        // Drop target for root level (unparent)
        ImGui::Dummy(ImVec2(-1, 20));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
                ecs::Entity droppedEntity = *static_cast<const ecs::Entity*>(payload->Data);
                // Detach from parent (make root)
                ecs::systems::detachFromParent(*world, droppedEntity);
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
}
