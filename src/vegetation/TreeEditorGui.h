#pragma once

#include "BillboardCapture.h"
#include "TreePresets.h"
#include <memory>
#include <string>
#include <vector>

class Renderer;
class Camera;

class TreeEditorGui {
public:
    TreeEditorGui() = default;
    ~TreeEditorGui() = default;

    // Initialize presets from directory
    void loadPresets(const std::string& resourcePath);

    // Render the tree editor as a separate ImGui window
    void render(Renderer& renderer, const Camera& camera);

    // Place tree in front of camera on terrain
    void placeTreeAtCamera(Renderer& renderer, const Camera& camera);

    bool isVisible() const { return visible; }
    void setVisible(bool v) { visible = v; }
    void toggleVisibility() { visible = !visible; }

private:
    void renderAlgorithmSection(Renderer& renderer);
    void renderBarkSection(Renderer& renderer);
    void renderSpaceColonisationSection(Renderer& renderer);
    void renderTrunkSection(Renderer& renderer);
    void renderBranchSection(Renderer& renderer);
    void renderVariationSection(Renderer& renderer);
    void renderLeafSection(Renderer& renderer);
    void renderSeedSection(Renderer& renderer);
    void renderTransformSection(Renderer& renderer, const Camera& camera);
    void renderPresets(Renderer& renderer);
    void renderBillboardSection(Renderer& renderer);

    bool visible = true;

    // Loaded presets from JSON files
    std::vector<TreePreset> presets;
    int selectedPresetIndex = -1;

    // Billboard capture state
    std::unique_ptr<BillboardCapture> billboardCapture;
    int billboardResolution = 512;  // 256, 512, or 1024
    std::string lastExportPath;
    bool captureInProgress = false;
    std::string captureStatus;
};
