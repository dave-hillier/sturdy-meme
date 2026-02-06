#include "GuiGizmo.h"
#include "scene/Camera.h"
#include "core/interfaces/ISceneControl.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace {

// Convert SceneEditorState transform mode to ImGuizmo operation
ImGuizmo::OPERATION getOperation(SceneEditorState::TransformMode mode) {
    switch (mode) {
        case SceneEditorState::TransformMode::Translate:
            return ImGuizmo::TRANSLATE;
        case SceneEditorState::TransformMode::Rotate:
            return ImGuizmo::ROTATE;
        case SceneEditorState::TransformMode::Scale:
            return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE;
}

// Convert SceneEditorState transform space to ImGuizmo mode
ImGuizmo::MODE getMode(SceneEditorState::TransformSpace space) {
    switch (space) {
        case SceneEditorState::TransformSpace::Local:
            return ImGuizmo::LOCAL;
        case SceneEditorState::TransformSpace::World:
            return ImGuizmo::WORLD;
    }
    return ImGuizmo::LOCAL;
}

} // anonymous namespace

bool GuiGizmo::render(const Camera& camera, ISceneControl& sceneControl, SceneEditorState& state) {
    ecs::World* world = sceneControl.getECSWorld();
    if (!world) return false;

    // Check if we have a valid selection
    if (state.selectedEntity == ecs::NullEntity) return false;
    if (!world->valid(state.selectedEntity)) return false;
    if (!world->has<ecs::Transform>(state.selectedEntity)) return false;

    // Get view and projection matrices
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix();

    // Get entity transform
    auto& transform = world->get<ecs::Transform>(state.selectedEntity);
    glm::mat4 modelMatrix = transform.matrix;

    // Set up ImGuizmo for this frame
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();

    // Get the main viewport for full-window gizmo rendering
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    // Configure gizmo appearance
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

    // Get operation and mode from state
    ImGuizmo::OPERATION operation = getOperation(state.transformMode);
    ImGuizmo::MODE mode = getMode(state.transformSpace);

    // Render the gizmo and check for manipulation
    bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        mode,
        glm::value_ptr(modelMatrix)
    );

    // If manipulated, update the entity transform
    if (manipulated) {
        // Decompose the new matrix
        glm::vec3 translation, scale, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose(modelMatrix, scale, rotation, translation, skew, perspective);

        // Update LocalTransform if available, otherwise update world Transform directly
        if (world->has<ecs::LocalTransform>(state.selectedEntity)) {
            auto& local = world->get<ecs::LocalTransform>(state.selectedEntity);

            // If entity has a parent, we need to convert world delta to local
            if (world->has<ecs::Parent>(state.selectedEntity)) {
                auto& parent = world->get<ecs::Parent>(state.selectedEntity);
                if (parent.valid() && world->has<ecs::Transform>(parent.entity)) {
                    // Get parent's world transform
                    glm::mat4 parentWorld = world->get<ecs::Transform>(parent.entity).matrix;
                    glm::mat4 parentInverse = glm::inverse(parentWorld);

                    // Convert to local space
                    glm::mat4 localMatrix = parentInverse * modelMatrix;
                    glm::decompose(localMatrix, scale, rotation, translation, skew, perspective);
                }
            }

            local.position = translation;
            local.rotation = rotation;
            local.scale = scale;

            // Update world transforms
            ecs::systems::updateWorldTransforms(*world);
        } else {
            // Direct world transform update
            transform.matrix = modelMatrix;
        }
    }

    return manipulated;
}

void GuiGizmo::renderViewCube(const Camera& camera, int position, float size) {
    glm::mat4 view = camera.getViewMatrix();

    // Calculate position based on corner
    ImGuiIO& io = ImGui::GetIO();
    float x = 0, y = 0;

    switch (position) {
        case 0: // top-left
            x = size / 2 + 10;
            y = size / 2 + 10;
            break;
        case 1: // top-right (default)
            x = io.DisplaySize.x - size / 2 - 10;
            y = size / 2 + 10;
            break;
        case 2: // bottom-left
            x = size / 2 + 10;
            y = io.DisplaySize.y - size / 2 - 10;
            break;
        case 3: // bottom-right
            x = io.DisplaySize.x - size / 2 - 10;
            y = io.DisplaySize.y - size / 2 - 10;
            break;
    }

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::ViewManipulate(
        glm::value_ptr(const_cast<glm::mat4&>(view)),
        8.0f,  // Camera distance for the cube
        ImVec2(x - size / 2, y - size / 2),
        ImVec2(size, size),
        0x10101010  // Background color (transparent dark)
    );
}

bool GuiGizmo::isOver() {
    return ImGuizmo::IsOver();
}

bool GuiGizmo::isUsing() {
    return ImGuizmo::IsUsing();
}
