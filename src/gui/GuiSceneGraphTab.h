#pragma once

#include <cstdint>

class ISceneControl;

/**
 * State for the scene graph tab - tracks selection and filter state
 */
struct SceneGraphTabState {
    int selectedObjectIndex = -1;         // Currently selected renderable index (-1 = none)
    char filterText[256] = "";            // Text filter for object list
    bool showTransformSection = true;     // Expand transform section
    bool showMaterialSection = true;      // Expand material section
    bool showInfoSection = true;          // Expand info section
};

namespace GuiSceneGraphTab {
    /**
     * Render the scene graph panel showing all renderables.
     * Allows selection and displays properties of selected objects.
     *
     * @param sceneControl Scene control interface for accessing renderables
     * @param state Tab state for selection and filtering
     */
    void render(ISceneControl& sceneControl, SceneGraphTabState& state);
}
