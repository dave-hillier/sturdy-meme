#include "GuiGrassTab.h"
#include "core/interfaces/IGrassControl.h"
#include "vegetation/GrassLODStrategy.h"
#include <imgui.h>

void GuiGrassTab::render(IGrassControl& grass) {
    // LOD Strategy Selection
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("LOD STRATEGY");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Current strategy info
    ImGui::Text("Current: %s", grass.getLODStrategyName().c_str());

    // Preset selection
    static const char* presetNames[] = {"Default", "Performance", "Quality", "Ultra"};
    IGrassControl::LODPreset currentPreset = grass.getLODPreset();
    int presetIndex = static_cast<int>(currentPreset);

    ImGui::Text("Preset:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("##LODPreset", &presetIndex, presetNames, 4)) {
        grass.setLODPreset(static_cast<IGrassControl::LODPreset>(presetIndex));
    }

    // Show preset descriptions
    const char* descriptions[] = {
        "Balanced quality and performance",
        "Optimized for lower-end hardware",
        "Higher density, longer draw distance",
        "Maximum quality, demanding on GPU"
    };
    ImGui::TextDisabled("%s", descriptions[presetIndex]);

    ImGui::Spacing();
    ImGui::Separator();

    // Statistics section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("STATISTICS");
    ImGui::PopStyleColor();
    ImGui::Separator();

    uint32_t activeTiles = grass.getActiveTileCount();
    uint32_t pendingLoads = grass.getPendingLoadCount();
    uint32_t totalLoaded = grass.getTotalLoadedTiles();
    uint32_t numLODs = grass.getNumLODLevels();

    ImGui::Text("Active Tiles: %u", activeTiles);
    ImGui::Text("Pending Loads: %u", pendingLoads);
    ImGui::Text("Total Loaded: %u", totalLoaded);

    // Per-LOD statistics
    ImGui::Spacing();
    ImGui::Text("Per-LOD Breakdown:");

    // Color-coded LOD bars
    ImVec4 lodColors[] = {
        ImVec4(0.2f, 0.8f, 0.2f, 1.0f),   // LOD0 - Green
        ImVec4(0.8f, 0.8f, 0.2f, 1.0f),   // LOD1 - Yellow
        ImVec4(0.8f, 0.5f, 0.2f, 1.0f),   // LOD2 - Orange
        ImVec4(0.8f, 0.2f, 0.2f, 1.0f),   // LOD3 - Red
    };

    for (uint32_t lod = 0; lod < numLODs && lod < 4; ++lod) {
        uint32_t lodCount = grass.getActiveTileCountAtLOD(lod);
        float tileSize = grass.getTileSizeForLOD(lod);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, lodColors[lod]);
        char label[64];
        snprintf(label, sizeof(label), "LOD%u: %u tiles (%.0fm)", lod, lodCount, tileSize);

        float fraction = activeTiles > 0 ? static_cast<float>(lodCount) / static_cast<float>(activeTiles) : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Configuration section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("CONFIGURATION");
    ImGui::PopStyleColor();
    ImGui::Separator();

    int maxLoadsPerFrame = static_cast<int>(grass.getMaxLoadsPerFrame());
    ImGui::Text("Max Tiles/Frame:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderInt("##MaxLoads", &maxLoadsPerFrame, 1, 10)) {
        grass.setMaxLoadsPerFrame(static_cast<uint32_t>(maxLoadsPerFrame));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum grass tiles to load per frame.\nHigher = faster loading, but may cause hitches");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Debug Visualization section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("DEBUG VISUALIZATION");
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

    // LOD strategy details (collapsible)
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("LOD Strategy Details")) {
        const IGrassLODStrategy* strategy = grass.getLODStrategy();
        if (strategy) {
            ImGui::Indent();
            ImGui::Text("Name: %s", strategy->getName().c_str());
            ImGui::Text("Description: %s", strategy->getDescription().c_str());
            ImGui::Text("Max Draw Distance: %.0fm", strategy->getMaxDrawDistance());
            ImGui::Text("Transition Zone: %.1fm", strategy->getTransitionZoneSize());
            ImGui::Text("LOD Hysteresis: %.2f", strategy->getLODHysteresis());
            ImGui::Text("Tile Fade-In: %.2fs", strategy->getTileFadeInDuration());

            ImGui::Spacing();
            ImGui::Text("LOD Level Details:");
            for (uint32_t lod = 0; lod < numLODs; ++lod) {
                float endDist = strategy->getLODEndDistance(lod);
                float tileSize = strategy->getTileSize(lod);
                float spacing = strategy->getSpacingMultiplier(lod);
                uint32_t tilesPerAxis = strategy->getTilesPerAxis(lod);

                ImGui::BulletText("LOD%u: dist=%.0fm, tile=%.0fm, spacing=%.1fx, grid=%ux%u",
                    lod, endDist, tileSize, spacing, tilesPerAxis, tilesPerAxis);
            }
            ImGui::Unindent();
        } else {
            ImGui::TextDisabled("No strategy loaded");
        }
    }
}
