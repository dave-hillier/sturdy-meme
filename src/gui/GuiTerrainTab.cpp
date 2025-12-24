#include "GuiTerrainTab.h"
#include "core/interfaces/ITerrainControl.h"
#include "TerrainSystem.h"

#include <imgui.h>

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
    ImGui::Text("STREAMING");
    ImGui::PopStyleColor();

    uint32_t activeTiles = 0;
    uint32_t maxTiles = 64;  // MAX_ACTIVE_TILES constant
    if (const auto* tileCachePtr = terrain.getTileCache()) {
        activeTiles = tileCachePtr->getActiveTileCount();
    }

    // Active tiles with color coding
    float tileUsage = static_cast<float>(activeTiles) / static_cast<float>(maxTiles);
    ImVec4 tileColor = tileUsage < 0.5f ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) :
                       tileUsage < 0.8f ? ImVec4(0.9f, 0.9f, 0.4f, 1.0f) :
                                          ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
    ImGui::Text("Height Tiles:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, tileColor);
    ImGui::Text("%u / %u (%.0f%%)", activeTiles, maxTiles, tileUsage * 100.0f);
    ImGui::PopStyleColor();

    // LOD info
    ImGui::Text("LOD Ranges: <1km:LOD0, 1-2km:LOD1, 2-4km:LOD2");

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
