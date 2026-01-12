#pragma once

#include <entt/entt.hpp>
#include <imgui.h>
#include <string>
#include <functional>
#include "../ecs/Components.h"
#include "../ecs/SceneGraphSystem.h"

// Scene Graph Tab - Unity-like hierarchy view for entities
// Displays entities in a tree structure with parent/child relationships
class GuiSceneGraphTab {
public:
    // Callback types for entity operations
    using EntityCallback = std::function<void(entt::entity)>;
    using CreateEntityCallback = std::function<entt::entity(const std::string&, entt::entity)>;

    // Render the scene graph hierarchy window
    void render(entt::registry& registry) {
        // Search/filter bar
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "Search entities...", searchBuffer_, sizeof(searchBuffer_))) {
            searchFilter_ = searchBuffer_;
        }

        ImGui::Separator();

        // Toolbar
        if (ImGui::Button("+ Entity")) {
            createEntityPopup_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Expand All")) {
            expandAll(registry);
        }
        ImGui::SameLine();
        if (ImGui::Button("Collapse All")) {
            collapseAll(registry);
        }

        ImGui::Separator();

        // Entity count
        ImGui::TextDisabled("Entities: %zu", SceneGraph::countEntitiesInHierarchy(registry));

        ImGui::Separator();

        // Scrollable tree view
        ImGui::BeginChild("EntityTree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        // Render root entities and their children
        auto roots = SceneGraph::getRootEntities(registry);
        if (roots.empty()) {
            // Show all entities without hierarchy as roots
            registry.view<entt::entity>().each([&](auto entity) {
                if (!registry.all_of<Hierarchy>(entity)) {
                    renderEntityNode(registry, entity);
                }
            });
        }
        for (auto root : roots) {
            renderEntityNode(registry, root);
        }

        // Also show entities without Hierarchy component
        registry.view<Transform>().each([&](auto entity, auto&) {
            if (!registry.all_of<Hierarchy>(entity)) {
                renderEntityNode(registry, entity);
            }
        });

        // Handle drag-drop to root (unparent)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
                entt::entity draggedEntity = *static_cast<entt::entity*>(payload->Data);
                SceneGraph::removeParent(registry, draggedEntity);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndChild();

        // Handle popups
        handleCreateEntityPopup(registry);
        handleContextMenu(registry);
    }

    // Get currently selected entity
    entt::entity getSelectedEntity() const { return selectedEntity_; }

    // Set selection externally
    void setSelectedEntity(entt::entity entity) { selectedEntity_ = entity; }

    // Clear selection
    void clearSelection(entt::registry& registry) {
        SceneGraph::clearSelection(registry);
        selectedEntity_ = entt::null;
    }

private:
    entt::entity selectedEntity_{entt::null};
    entt::entity contextMenuEntity_{entt::null};
    char searchBuffer_[256] = "";
    std::string searchFilter_;
    bool createEntityPopup_ = false;
    char newEntityName_[128] = "New Entity";

    // Render a single entity node in the tree
    void renderEntityNode(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity)) return;

        // Get entity info
        std::string name = SceneGraph::getEntityName(registry, entity);
        std::string icon = SceneGraph::getEntityIcon(registry, entity);

        // Apply search filter
        if (!searchFilter_.empty()) {
            std::string lowerName = name;
            std::string lowerFilter = searchFilter_;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
            if (lowerName.find(lowerFilter) == std::string::npos) {
                // Check if any children match
                if (registry.all_of<Hierarchy>(entity)) {
                    const auto& hierarchy = registry.get<Hierarchy>(entity);
                    bool childMatch = false;
                    for (auto child : hierarchy.children) {
                        if (entityMatchesFilter(registry, child)) {
                            childMatch = true;
                            break;
                        }
                    }
                    if (!childMatch) return;
                } else {
                    return;
                }
            }
        }

        // Check if entity has children
        bool hasChildren = false;
        if (registry.all_of<Hierarchy>(entity)) {
            hasChildren = registry.get<Hierarchy>(entity).hasChildren();
        }

        // Check if expanded
        bool isExpanded = registry.all_of<TreeExpanded>(entity);

        // Build tree node flags
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;

        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (SceneGraph::isSelected(registry, entity)) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (isExpanded) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        // Generate unique ID for this node
        ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));

        // Render visibility toggle
        bool visible = true;
        if (registry.all_of<EntityInfo>(entity)) {
            visible = registry.get<EntityInfo>(entity).visible;
        }
        if (ImGui::Checkbox("##vis", &visible)) {
            if (registry.all_of<EntityInfo>(entity)) {
                registry.get<EntityInfo>(entity).visible = visible;
            }
        }
        ImGui::SameLine();

        // Color based on entity type
        ImVec4 color = getEntityColor(registry, entity);
        ImGui::PushStyleColor(ImGuiCol_Text, color);

        // Format display string with icon
        std::string displayName = "[" + icon + "] " + name;

        // Render tree node
        bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);

        ImGui::PopStyleColor();

        // Handle selection
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (ImGui::GetIO().KeyCtrl) {
                // Ctrl+click toggles selection
                if (SceneGraph::isSelected(registry, entity)) {
                    SceneGraph::deselectEntity(registry, entity);
                    if (selectedEntity_ == entity) {
                        selectedEntity_ = entt::null;
                    }
                } else {
                    SceneGraph::selectEntity(registry, entity);
                    selectedEntity_ = entity;
                }
            } else {
                // Regular click - single selection
                SceneGraph::clearSelection(registry);
                SceneGraph::selectEntity(registry, entity);
                selectedEntity_ = entity;
            }
        }

        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            contextMenuEntity_ = entity;
            renderContextMenuContent(registry, entity);
            ImGui::EndPopup();
        }

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ENTITY_DRAG", &entity, sizeof(entt::entity));
            ImGui::Text("Moving: %s", name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag target (for reparenting)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_DRAG")) {
                entt::entity draggedEntity = *static_cast<entt::entity*>(payload->Data);
                // Don't allow parenting to self or descendants
                if (draggedEntity != entity && !SceneGraph::isAncestorOf(registry, draggedEntity, entity)) {
                    SceneGraph::setParent(registry, draggedEntity, entity);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Track expansion state
        if (hasChildren) {
            if (nodeOpen && !isExpanded) {
                registry.emplace_or_replace<TreeExpanded>(entity);
            } else if (!nodeOpen && isExpanded) {
                registry.remove<TreeExpanded>(entity);
            }
        }

        // Render children if node is open
        if (nodeOpen && hasChildren) {
            if (registry.all_of<Hierarchy>(entity)) {
                const auto& hierarchy = registry.get<Hierarchy>(entity);
                for (auto child : hierarchy.children) {
                    renderEntityNode(registry, child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // Check if entity or any descendants match search filter
    bool entityMatchesFilter(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity)) return false;

        std::string name = SceneGraph::getEntityName(registry, entity);
        std::string lowerName = name;
        std::string lowerFilter = searchFilter_;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

        if (lowerName.find(lowerFilter) != std::string::npos) {
            return true;
        }

        if (registry.all_of<Hierarchy>(entity)) {
            const auto& hierarchy = registry.get<Hierarchy>(entity);
            for (auto child : hierarchy.children) {
                if (entityMatchesFilter(registry, child)) {
                    return true;
                }
            }
        }

        return false;
    }

    // Get color for entity based on type
    ImVec4 getEntityColor(entt::registry& registry, entt::entity entity) {
        if (registry.all_of<PlayerTag>(entity)) return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // Green
        if (registry.all_of<PointLight>(entity) || registry.all_of<SpotLight>(entity))
            return ImVec4(1.0f, 0.9f, 0.3f, 1.0f);  // Yellow
        if (registry.all_of<NPCTag>(entity)) return ImVec4(0.8f, 0.4f, 0.8f, 1.0f);  // Purple
        if (registry.all_of<PhysicsBody>(entity)) return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);  // Blue
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White (default)
    }

    // Render context menu content
    void renderContextMenuContent(entt::registry& registry, entt::entity entity) {
        std::string name = SceneGraph::getEntityName(registry, entity);

        ImGui::Text("%s", name.c_str());
        ImGui::Separator();

        if (ImGui::MenuItem("Rename")) {
            // TODO: Implement rename dialog
        }

        if (ImGui::MenuItem("Duplicate")) {
            duplicateEntity(registry, entity);
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Create Child")) {
            if (ImGui::MenuItem("Empty Entity")) {
                SceneGraph::createEntity(registry, "Child Entity", entity);
            }
            if (ImGui::MenuItem("Point Light")) {
                auto child = SceneGraph::createEntity(registry, "Point Light", entity);
                registry.emplace<PointLight>(child);
                registry.emplace<LightEnabled>(child);
            }
            if (ImGui::MenuItem("Spot Light")) {
                auto child = SceneGraph::createEntity(registry, "Spot Light", entity);
                registry.emplace<SpotLight>(child);
                registry.emplace<LightEnabled>(child);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Unparent")) {
            SceneGraph::removeParent(registry, entity);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del")) {
            SceneGraph::deleteEntity(registry, entity);
            if (selectedEntity_ == entity) {
                selectedEntity_ = entt::null;
            }
        }
    }

    // Handle create entity popup
    void handleCreateEntityPopup(entt::registry& registry) {
        if (createEntityPopup_) {
            ImGui::OpenPopup("Create Entity");
            createEntityPopup_ = false;
        }

        if (ImGui::BeginPopupModal("Create Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", newEntityName_, sizeof(newEntityName_));

            ImGui::Separator();

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                auto newEntity = SceneGraph::createEntity(registry, newEntityName_);
                SceneGraph::clearSelection(registry);
                SceneGraph::selectEntity(registry, newEntity);
                selectedEntity_ = newEntity;
                strcpy(newEntityName_, "New Entity");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // Handle global context menu (right-click in empty space)
    void handleContextMenu(entt::registry& registry) {
        // This is handled per-item in renderEntityNode
    }

    // Expand all nodes
    void expandAll(entt::registry& registry) {
        auto view = registry.view<Hierarchy>();
        for (auto entity : view) {
            if (!registry.all_of<TreeExpanded>(entity)) {
                registry.emplace<TreeExpanded>(entity);
            }
        }
    }

    // Collapse all nodes
    void collapseAll(entt::registry& registry) {
        auto view = registry.view<TreeExpanded>();
        for (auto entity : view) {
            registry.remove<TreeExpanded>(entity);
        }
    }

    // Duplicate entity (shallow - doesn't duplicate children)
    void duplicateEntity(entt::registry& registry, entt::entity source) {
        if (!registry.valid(source)) return;

        std::string name = SceneGraph::getEntityName(registry, source) + " (Copy)";
        entt::entity parent = entt::null;
        if (registry.all_of<Hierarchy>(source)) {
            parent = registry.get<Hierarchy>(source).parent;
        }

        auto copy = SceneGraph::createEntity(registry, name, parent);

        // Copy common components
        if (registry.all_of<Transform>(source)) {
            registry.emplace_or_replace<Transform>(copy, registry.get<Transform>(source));
        }
        if (registry.all_of<PointLight>(source)) {
            registry.emplace_or_replace<PointLight>(copy, registry.get<PointLight>(source));
            registry.emplace_or_replace<LightEnabled>(copy);
        }
        if (registry.all_of<SpotLight>(source)) {
            registry.emplace_or_replace<SpotLight>(copy, registry.get<SpotLight>(source));
            registry.emplace_or_replace<LightEnabled>(copy);
        }

        SceneGraph::clearSelection(registry);
        SceneGraph::selectEntity(registry, copy);
        selectedEntity_ = copy;
    }
};
