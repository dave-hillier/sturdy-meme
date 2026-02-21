#include "GuiTileLoaderTab.h"
#include "Camera.h"
#include "core/interfaces/ITerrainControl.h"
#include "terrain/TerrainSystem.h"
#include "terrain/TerrainTileCache.h"
#include "physics/PhysicsTerrainTileManager.h"

#include <imgui.h>
#include <unordered_map>
#include <string>

void GuiTileLoaderTab::render(ITerrainControl& terrain, PhysicsTerrainTileManager* physicsTerrainTiles,
                               const Camera& camera, State& state) {
    const TerrainTileCache* tileCache = terrain.getTerrainSystem().getTileCache();

    if (!tileCache) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Tile cache not enabled");
        return;
    }

    // Mode selection
    ImGui::Text("View Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("GPU", state.viewMode == TileViewMode::GPU)) {
        state.viewMode = TileViewMode::GPU;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Active GPU tiles (uploaded to VRAM for shader sampling)");
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("CPU", state.viewMode == TileViewMode::CPU)) {
        state.viewMode = TileViewMode::CPU;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("All tiles with CPU data (includes GPU tiles + CPU-only + base LOD)");
    }
    ImGui::SameLine();
    bool hasPhysics = physicsTerrainTiles != nullptr;
    ImGui::BeginDisabled(!hasPhysics);
    if (ImGui::RadioButton("Physics", state.viewMode == TileViewMode::Physics)) {
        state.viewMode = TileViewMode::Physics;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(hasPhysics ?
            "Physics collision tiles (Jolt heightfield bodies)" :
            "Physics not initialized");
    }

    ImGui::Spacing();

    // Grid configuration
    constexpr int GRID_SIZE = 32;
    constexpr float CELL_SIZE = 16.0f;

    // LOD colors (distinct colors for each LOD level)
    const ImU32 lodColors[] = {
        IM_COL32(80, 200, 80, 255),   // LOD0 - Green (highest detail)
        IM_COL32(80, 150, 220, 255),  // LOD1 - Blue
        IM_COL32(220, 180, 60, 255),  // LOD2 - Yellow/Orange
        IM_COL32(180, 80, 180, 255),  // LOD3 - Purple (lowest detail)
    };
    const ImU32 emptyColor = IM_COL32(40, 40, 50, 255);
    const ImU32 gridLineColor = IM_COL32(60, 60, 70, 255);
    const ImU32 playerColor = IM_COL32(255, 100, 100, 255);

    // Get camera position for player marker
    glm::vec3 camPos = camera.getPosition();
    float terrainSize = tileCache->getTerrainSize();

    // Calculate player grid position (0-31)
    float playerGridX = (camPos.x / terrainSize + 0.5f) * GRID_SIZE;
    float playerGridZ = (camPos.z / terrainSize + 0.5f) * GRID_SIZE;

    // Legend
    ImGui::Text("LOD Legend:");
    ImGui::SameLine();
    for (int i = 0; i < 4; i++) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(lodColors[i]));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(lodColors[i]));
        ImGui::SmallButton(std::to_string(i).c_str());
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();

    // Player position info
    ImGui::Text("Camera: (%.0f, %.0f, %.0f)", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Grid pos: (%.1f, %.1f)", playerGridX, playerGridZ);

    // Create a lookup for loaded tiles: key = (x << 16 | z), value = lod
    std::unordered_map<uint32_t, uint32_t> tileMap;  // (x,z) -> lod at LOD0 grid scale

    // Helper lambda to populate tileMap from tile coord/lod pairs
    auto addTileToMap = [&](int coordX, int coordZ, uint32_t lod) {
        int scale = 1 << lod;
        int baseX = coordX * scale;
        int baseZ = coordZ * scale;
        for (int dz = 0; dz < scale; dz++) {
            for (int dx = 0; dx < scale; dx++) {
                int gx = baseX + dx;
                int gz = baseZ + dz;
                if (gx >= 0 && gx < GRID_SIZE && gz >= 0 && gz < GRID_SIZE) {
                    uint32_t key = (static_cast<uint32_t>(gx) << 16) | static_cast<uint32_t>(gz);
                    tileMap[key] = lod;
                }
            }
        }
    };

    // Get tiles based on view mode
    uint32_t tileCount = 0;
    if (state.viewMode == TileViewMode::GPU) {
        const auto& activeTiles = tileCache->getActiveTiles();
        tileCount = static_cast<uint32_t>(activeTiles.size());

        // Process tiles in order: LOD3 -> LOD0 (finer detail overwrites coarser)
        for (int targetLod = 3; targetLod >= 0; targetLod--) {
            for (const TerrainTile* tile : activeTiles) {
                if (!tile || !tile->loaded || tile->lod != static_cast<uint32_t>(targetLod)) continue;
                addTileToMap(tile->coord.x, tile->coord.z, tile->lod);
            }
        }
    } else if (state.viewMode == TileViewMode::CPU) {
        auto cpuTiles = tileCache->getAllCPUTiles();
        tileCount = static_cast<uint32_t>(cpuTiles.size());

        // Count tiles per LOD for diagnostics
        int lodCounts[4] = {0, 0, 0, 0};
        for (const TerrainTile* tile : cpuTiles) {
            if (tile && tile->lod < 4) lodCounts[tile->lod]++;
        }
        ImGui::Text("  Tiles: LOD0=%d LOD1=%d LOD2=%d LOD3=%d",
                   lodCounts[0], lodCounts[1], lodCounts[2], lodCounts[3]);

        // Button to copy tile info to clipboard
        if (ImGui::Button("Copy Tiles to Clipboard")) {
            std::string tileInfo;
            for (int lod = 0; lod < 4; lod++) {
                tileInfo += "LOD" + std::to_string(lod) + ":\n";
                for (const TerrainTile* tile : cpuTiles) {
                    if (tile && tile->lod == static_cast<uint32_t>(lod)) {
                        int scale = 1 << lod;
                        int baseX = tile->coord.x * scale;
                        int baseZ = tile->coord.z * scale;
                        tileInfo += "  coord(" + std::to_string(tile->coord.x) + "," + std::to_string(tile->coord.z) + ")";
                        tileInfo += " -> grid(" + std::to_string(baseX) + "-" + std::to_string(baseX + scale - 1);
                        tileInfo += "," + std::to_string(baseZ) + "-" + std::to_string(baseZ + scale - 1) + ")\n";
                    }
                }
            }
            ImGui::SetClipboardText(tileInfo.c_str());
        }

        // Process tiles in order: LOD3 -> LOD0
        for (int targetLod = 3; targetLod >= 0; targetLod--) {
            for (const TerrainTile* tile : cpuTiles) {
                if (!tile || tile->lod != static_cast<uint32_t>(targetLod)) continue;
                addTileToMap(tile->coord.x, tile->coord.z, tile->lod);
            }
        }
    } else if (state.viewMode == TileViewMode::Physics && physicsTerrainTiles) {
        const auto& physicsTiles = physicsTerrainTiles->getLoadedTiles();
        tileCount = static_cast<uint32_t>(physicsTiles.size());

        // Process tiles in order: LOD3 -> LOD0
        for (int targetLod = 3; targetLod >= 0; targetLod--) {
            for (const auto& [key, entry] : physicsTiles) {
                if (entry.lod != static_cast<uint32_t>(targetLod)) continue;
                addTileToMap(entry.tileX, entry.tileZ, entry.lod);
            }
        }
    }

    // Tile statistics
    const char* modeLabel = (state.viewMode == TileViewMode::GPU) ? "GPU" :
                           (state.viewMode == TileViewMode::CPU) ? "CPU" : "Physics";
    ImGui::Text("%s tiles: %u / %u", modeLabel, tileCount, 64u);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Draw the tile grid
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

    // Draw cells
    for (int z = 0; z < GRID_SIZE; z++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            ImVec2 cellMin(gridOrigin.x + x * CELL_SIZE, gridOrigin.y + z * CELL_SIZE);
            ImVec2 cellMax(cellMin.x + CELL_SIZE, cellMin.y + CELL_SIZE);

            uint32_t key = (static_cast<uint32_t>(x) << 16) | static_cast<uint32_t>(z);
            auto it = tileMap.find(key);

            ImU32 color = emptyColor;
            if (it != tileMap.end()) {
                uint32_t lod = it->second;
                if (lod < 4) {
                    color = lodColors[lod];
                }
            }

            drawList->AddRectFilled(cellMin, cellMax, color);
            drawList->AddRect(cellMin, cellMax, gridLineColor);

            // Tooltip on hover showing cell coords and LOD
            if (ImGui::IsMouseHoveringRect(cellMin, cellMax)) {
                ImGui::BeginTooltip();
                ImGui::Text("Cell (%d, %d)", x, z);
                if (it != tileMap.end()) {
                    ImGui::Text("LOD: %u", it->second);
                } else {
                    ImGui::Text("Empty");
                }
                ImGui::EndTooltip();
            }
        }
    }

    // Draw player marker
    if (playerGridX >= 0 && playerGridX < GRID_SIZE &&
        playerGridZ >= 0 && playerGridZ < GRID_SIZE) {
        ImVec2 playerPos(
            gridOrigin.x + playerGridX * CELL_SIZE,
            gridOrigin.y + playerGridZ * CELL_SIZE
        );
        // Draw a crosshair/circle for player
        float markerRadius = CELL_SIZE * 0.4f;
        drawList->AddCircleFilled(playerPos, markerRadius, playerColor);
        drawList->AddCircle(playerPos, markerRadius + 1, IM_COL32(255, 255, 255, 200), 12, 2.0f);
    }

    // Reserve space for the grid
    ImGui::Dummy(ImVec2(GRID_SIZE * CELL_SIZE, GRID_SIZE * CELL_SIZE));
}
