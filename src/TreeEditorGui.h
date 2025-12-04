#pragma once

class Renderer;

class TreeEditorGui {
public:
    TreeEditorGui() = default;
    ~TreeEditorGui() = default;

    // Render the tree editor as a separate ImGui window
    void render(Renderer& renderer);

    bool isVisible() const { return visible; }
    void setVisible(bool v) { visible = v; }
    void toggleVisibility() { visible = !visible; }

private:
    void renderTrunkSection(Renderer& renderer);
    void renderBranchSection(Renderer& renderer);
    void renderVariationSection(Renderer& renderer);
    void renderLeafSection(Renderer& renderer);
    void renderSeedSection(Renderer& renderer);
    void renderTransformSection(Renderer& renderer);
    void renderPresets(Renderer& renderer);

    bool visible = false;
};
