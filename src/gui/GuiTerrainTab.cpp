#include "GuiTerrainTab.h"
#include "core/interfaces/ITerrainControl.h"
#include "TerrainSystem.h"
#include "TerrainTileCache.h"

#include <imgui.h>
#include <algorithm>

void GuiTerrainTab::render(ITerrainControl& terrainControl) {
    ImGui::Spacing();

    // Terrain info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 0.6f, 1.0f));
    ImGui::Text("TERRAIN SYSTEM");
    ImGui::PopStyleColor();

    const auto& terrain = terrainControl.getTerrainSystem();
    const auto& config = terrain.getConfig();

    ImGui::Text("Size: %.0f x %.0f meters", config.size, config.size);
    ImGui::Text("Height Scale: %.1f", config.heightScale);

    // Triangle count with color coding
    uint32_t triangleCount = terrainControl.getTerrainNodeCount();
    ImVec4 triColor = triangleCount < 100000 ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                      triangleCount < 500000 ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                               ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
    ImGui::Text("Triangles:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, triColor);
    if (triangleCount >= 1000000) {
        ImGui::Text("%.2fM", triangleCount / 1000000.0f);
    } else if (triangleCount >= 1000) {
        ImGui::Text("%.1fK", triangleCount / 1000.0f);
    } else {
        ImGui::Text("%u", triangleCount);
    }
    ImGui::PopStyleColor();

    // CBT depth info
    ImGui::Text("Max Depth: %d (min edge: %.1fm)", config.maxDepth,
                config.size / (1 << (config.maxDepth / 2)));
    ImGui::Text("Min Depth: %d", config.minDepth);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // LOD parameters (modifiable at runtime)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("LOD PARAMETERS");
    ImGui::PopStyleColor();

    auto& terrainMut = terrainControl.getTerrainSystem();
    TerrainConfig cfg = terrainMut.getConfig();
    bool configChanged = false;

    if (ImGui::SliderFloat("Split Threshold", &cfg.splitThreshold, 1.0f, 256.0f, "%.0f px")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Screen-space edge length (pixels) to trigger subdivision");
    }

    if (ImGui::SliderFloat("Merge Threshold", &cfg.mergeThreshold, 1.0f, 256.0f, "%.0f px")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Screen-space edge length (pixels) to trigger merge");
    }

    if (ImGui::SliderFloat("Flatness Scale", &cfg.flatnessScale, 0.0f, 5.0f, "%.1f")) {
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Curvature LOD: 0=disabled, 2=flat areas use 3x threshold");
    }

    int maxDepth = cfg.maxDepth;
    if (ImGui::SliderInt("Max Depth", &maxDepth, 16, 32)) {
        cfg.maxDepth = maxDepth;
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum subdivision depth (higher = finer detail, more triangles)");
    }

    int minDepth = cfg.minDepth;
    if (ImGui::SliderInt("Min Depth", &minDepth, 1, 10)) {
        cfg.minDepth = minDepth;
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimum subdivision depth (base tessellation level)");
    }

    int spreadFactor = static_cast<int>(cfg.spreadFactor);
    if (ImGui::SliderInt("Spread Factor", &spreadFactor, 1, 32)) {
        cfg.spreadFactor = static_cast<uint32_t>(spreadFactor);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Temporal spreading: process 1/N triangles per frame (1 = all, higher = less GPU work per frame)");
    }

    if (configChanged) {
        terrainMut.setConfig(cfg);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Debug toggles
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("DEBUG");
    ImGui::PopStyleColor();

    bool terrainEnabled = terrainControl.isTerrainEnabled();
    if (ImGui::Checkbox("Enable Terrain", &terrainEnabled)) {
        terrainControl.setTerrainEnabled(terrainEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle terrain rendering on/off");
    }

    bool wireframe = terrainControl.isTerrainWireframeMode();
    if (ImGui::Checkbox("Wireframe Mode", &wireframe)) {
        terrainControl.toggleTerrainWireframe();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show terrain mesh wireframe overlay");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Meshlet rendering
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.9f, 1.0f));
    ImGui::Text("MESHLET RENDERING");
    ImGui::PopStyleColor();

    bool meshletsEnabled = terrainMut.isMeshletsEnabled();
    if (ImGui::Checkbox("Enable Meshlets", &meshletsEnabled)) {
        terrainMut.setMeshletsEnabled(meshletsEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use pre-tessellated meshlets per CBT leaf for higher resolution");
    }

    if (meshletsEnabled) {
        int meshletLevel = terrainMut.getMeshletSubdivisionLevel();
        if (ImGui::SliderInt("Meshlet Level", &meshletLevel, 0, 6)) {
            terrainMut.setMeshletSubdivisionLevel(meshletLevel);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Subdivision level per meshlet (0=1, 1=4, 2=16, 3=64, 4=256 triangles)");
        }

        uint32_t meshletTris = terrainMut.getMeshletTriangleCount();
        ImGui::Text("Triangles per leaf: %u", meshletTris);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Optimization toggles
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.8f, 1.0f));
    ImGui::Text("OPTIMIZATIONS");
    ImGui::PopStyleColor();

    bool skipFrameOpt = terrainMut.isSkipFrameOptimizationEnabled();
    if (ImGui::Checkbox("Skip-Frame (Camera Still)", &skipFrameOpt)) {
        terrainMut.setSkipFrameOptimization(skipFrameOpt);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Skip subdivision compute when camera is stationary");
    }

    bool gpuCulling = terrainMut.isGpuCullingEnabled();
    if (ImGui::Checkbox("GPU Frustum Culling", &gpuCulling)) {
        terrainMut.setGpuCulling(gpuCulling);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use GPU frustum culling with stream compaction for split phase");
    }

    // Debug status (phase alternates every frame so not useful to display)
    ImGui::Text("Status: %s", terrainMut.isCurrentlySkipping() ? "SKIPPING" : "ACTIVE");

    ImGui::Spacing();

    // Height query demo
    ImGui::Text("Height at origin: %.2f", terrainControl.getTerrainHeightAt(0.0f, 0.0f));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Streaming stats (Ghost of Tsushima style)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("HEIGHTMAP STREAMING");
    ImGui::PopStyleColor();

    const auto* tileCachePtr = terrain.getTileCache();
    if (tileCachePtr) {
        const auto& stats = tileCachePtr->getStats();

        // Active tiles with color coding
        float tileUsage = static_cast<float>(stats.totalTilesLoaded) / static_cast<float>(stats.maxActiveTiles);
        ImVec4 tileColor = tileUsage < 0.5f ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                           tileUsage < 0.8f ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                              ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
        ImGui::Text("Active Tiles:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, tileColor);
        ImGui::Text("%u / %u (%.0f%%)", stats.totalTilesLoaded, stats.maxActiveTiles, tileUsage * 100.0f);
        ImGui::PopStyleColor();

        // Per-LOD breakdown with color coding
        ImVec4 lodColors[4] = {
            ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // LOD0 - bright green (highest detail)
            ImVec4(0.5f, 0.9f, 0.3f, 1.0f),  // LOD1 - yellow-green
            ImVec4(0.9f, 0.7f, 0.2f, 1.0f),  // LOD2 - orange
            ImVec4(0.7f, 0.4f, 0.2f, 1.0f)   // LOD3 - brown (lowest detail)
        };

        ImGui::Text("Per-LOD:");
        for (int i = 0; i < 4; i++) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, lodColors[i]);
            ImGui::Text("L%d:%u", i, stats.tilesLoadedPerLOD[i]);
            ImGui::PopStyleColor();
        }

        // Streaming status
        if (stats.pendingLoads > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.4f, 1.0f));
            ImGui::Text("Streaming: %u pending (+%u/frame)", stats.pendingLoads, stats.tilesLoadedThisFrame);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::Text("Streaming: idle");
            ImGui::PopStyleColor();
        }

        // Initial load status
        if (!stats.initialLoadComplete) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.2f, 1.0f));
            ImGui::Text("Loading base coverage...");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // Mini tile grid visualization
        if (ImGui::CollapsingHeader("Tile Grid Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 gridStart = ImGui::GetCursorScreenPos();

            // Grid settings - show a simplified 8x8 view of the terrain
            const int gridCells = 8;
            const float cellSize = 20.0f;
            const float gridSize = gridCells * cellSize;

            // LOD colors for visualization
            ImU32 lodColorsU32[4] = {
                IM_COL32(50, 200, 50, 255),   // LOD0 - bright green
                IM_COL32(130, 230, 80, 255),  // LOD1 - yellow-green
                IM_COL32(230, 180, 50, 255),  // LOD2 - orange
                IM_COL32(180, 100, 50, 255)   // LOD3 - brown
            };
            ImU32 noTileColor = IM_COL32(50, 50, 60, 255);  // Dark gray - no tile
            ImU32 gridLineColor = IM_COL32(80, 80, 100, 128);

            // Get terrain grid dimensions
            uint32_t lod0TilesX = tileCachePtr->getLOD0TilesX();
            uint32_t lod0TilesZ = tileCachePtr->getLOD0TilesZ();

            // Calculate scaling (how many LOD0 tiles per grid cell)
            int tilesPerCell = std::max(1u, lod0TilesX / gridCells);

            // Draw grid cells
            for (int gz = 0; gz < gridCells; gz++) {
                for (int gx = 0; gx < gridCells; gx++) {
                    // Map grid cell to terrain tile coordinates
                    int tileX = gx * tilesPerCell + tilesPerCell / 2;
                    int tileZ = gz * tilesPerCell + tilesPerCell / 2;

                    // Get the LOD at this position
                    int lod = tileCachePtr->getTileLODAt(tileX, tileZ);

                    ImVec2 cellMin(gridStart.x + gx * cellSize, gridStart.y + gz * cellSize);
                    ImVec2 cellMax(cellMin.x + cellSize - 1, cellMin.y + cellSize - 1);

                    ImU32 cellColor = (lod >= 0 && lod < 4) ? lodColorsU32[lod] : noTileColor;
                    drawList->AddRectFilled(cellMin, cellMax, cellColor);
                    drawList->AddRect(cellMin, cellMax, gridLineColor);
                }
            }

            // Reserve space for grid
            ImGui::Dummy(ImVec2(gridSize, gridSize));

            // Legend
            ImGui::Text("Legend:");
            for (int i = 0; i < 4; i++) {
                ImGui::SameLine();
                ImVec2 pos = ImGui::GetCursorScreenPos();
                drawList->AddRectFilled(pos, ImVec2(pos.x + 12, pos.y + 12), lodColorsU32[i]);
                ImGui::Dummy(ImVec2(14, 12));
                ImGui::SameLine();
                ImGui::Text("L%d", i);
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tile cache not available");
    }

    ImGui::Spacing();

    // Render distance controls
    TerrainConfig streamCfg = terrainMut.getConfig();
    bool streamConfigChanged = false;

    if (ImGui::SliderFloat("Load Radius", &streamCfg.tileLoadRadius, 500.0f, 8000.0f, "%.0f m")) {
        // Ensure unload radius stays larger than load radius
        if (streamCfg.tileUnloadRadius < streamCfg.tileLoadRadius + 500.0f) {
            streamCfg.tileUnloadRadius = streamCfg.tileLoadRadius + 500.0f;
        }
        streamConfigChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance from camera to load high-resolution height tiles");
    }

    if (ImGui::SliderFloat("Unload Radius", &streamCfg.tileUnloadRadius, 1000.0f, 10000.0f, "%.0f m")) {
        // Ensure unload radius stays larger than load radius
        if (streamCfg.tileUnloadRadius < streamCfg.tileLoadRadius + 500.0f) {
            streamCfg.tileUnloadRadius = streamCfg.tileLoadRadius + 500.0f;
        }
        streamConfigChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance from camera to unload tiles (should be > load radius)");
    }

    if (streamConfigChanged) {
        terrainMut.setConfig(streamCfg);
    }
}
