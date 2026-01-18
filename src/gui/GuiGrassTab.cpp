#include "GuiGrassTab.h"
#include "core/interfaces/IGrassControl.h"
#include "vegetation/GrassConstants.h"
#include <imgui.h>

void GuiGrassTab::render(IGrassControl& grass) {
    // System Overview
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("GRASS SYSTEM");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::TextDisabled("Continuous stochastic culling - no discrete LOD levels");

    ImGui::Spacing();
    ImGui::Separator();

    // Statistics section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("STATISTICS");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Fixed 5x5 tile dispatch around camera (not using tile manager)
    constexpr uint32_t TILES_PER_AXIS = 5;
    constexpr uint32_t activeTiles = TILES_PER_AXIS * TILES_PER_AXIS;

    // Calculate potential and max instances
    uint32_t gridSize = GrassConstants::TILE_GRID_SIZE + 1; // +1 for edge coverage
    uint32_t bladesPerTile = gridSize * gridSize;
    uint32_t potentialBlades = activeTiles * bladesPerTile;
    uint32_t maxInstances = GrassConstants::MAX_INSTANCES;

    ImGui::Text("Active Tiles: %u (%ux%u grid)", activeTiles, TILES_PER_AXIS, TILES_PER_AXIS);
    ImGui::Text("Potential Blades: ~%uk", potentialBlades / 1000);
    ImGui::Text("Max Instances: %uk", maxInstances / 1000);

    // Instance usage bar
    float usage = potentialBlades > 0 ?
        static_cast<float>(maxInstances) / static_cast<float>(potentialBlades) : 0.0f;
    usage = usage > 1.0f ? 1.0f : usage;

    ImGui::Spacing();
    char usageLabel[64];
    snprintf(usageLabel, sizeof(usageLabel), "Budget: %uk / ~%uk potential",
             maxInstances / 1000, potentialBlades / 1000);
    ImGui::ProgressBar(usage, ImVec2(-1, 0), usageLabel);

    ImGui::Spacing();
    ImGui::Separator();

    // Culling Parameters
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("CULLING DISTANCES");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::Text("Full Density: < %.0fm", GrassConstants::CULL_START_DISTANCE);
    ImGui::Text("Fade to Zero: %.0fm - %.0fm",
                GrassConstants::CULL_START_DISTANCE,
                GrassConstants::CULL_END_DISTANCE);
    ImGui::Text("Falloff Power: %.1f (cubic)", GrassConstants::CULL_POWER);

    // Visual representation of distance falloff
    ImGui::Spacing();
    ImGui::Text("Density Falloff:");

    // Draw a simple density curve
    const int numSamples = 50;
    float values[numSamples];
    for (int i = 0; i < numSamples; i++) {
        float dist = (static_cast<float>(i) / numSamples) * GrassConstants::CULL_END_DISTANCE * 1.2f;
        if (dist <= GrassConstants::CULL_START_DISTANCE) {
            values[i] = 1.0f;
        } else if (dist >= GrassConstants::CULL_END_DISTANCE) {
            values[i] = 0.0f;
        } else {
            float t = (dist - GrassConstants::CULL_START_DISTANCE) /
                      (GrassConstants::CULL_END_DISTANCE - GrassConstants::CULL_START_DISTANCE);
            values[i] = 1.0f - powf(t, 1.0f / GrassConstants::CULL_POWER);
        }
    }
    ImGui::PlotLines("##DensityFalloff", values, numSamples, 0, nullptr, 0.0f, 1.0f, ImVec2(-1, 60));

    ImGui::Spacing();
    ImGui::Separator();

    // Tile Configuration
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("TILE CONFIGURATION");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::Text("Grid Size: %u x %u per tile", GrassConstants::TILE_GRID_SIZE, GrassConstants::TILE_GRID_SIZE);
    ImGui::Text("Tile Size: %.1fm x %.1fm", GrassConstants::TILE_SIZE, GrassConstants::TILE_SIZE);
    ImGui::Text("Blade Spacing: %.2fm", GrassConstants::SPACING);
    ImGui::Text("Total Coverage: ~%.0fm x %.0fm", GrassConstants::TILE_SIZE * 5, GrassConstants::TILE_SIZE * 5);

    ImGui::Spacing();
    ImGui::Separator();

    // Debug Visualization section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("DEBUG");
    ImGui::PopStyleColor();
    ImGui::Separator();

    bool debugEnabled = grass.isDebugVisualizationEnabled();
    if (ImGui::Checkbox("Enable Debug Overlay", &debugEnabled)) {
        grass.setDebugVisualizationEnabled(debugEnabled);
    }

    bool tileBoundsEnabled = grass.isTileBoundsVisualizationEnabled();
    if (ImGui::Checkbox("Show Tile Boundaries", &tileBoundsEnabled)) {
        grass.setTileBoundsVisualizationEnabled(tileBoundsEnabled);
    }
}
