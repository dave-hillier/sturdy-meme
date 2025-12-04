#include "TreeEditorGui.h"
#include "Renderer.h"

#include <imgui.h>
#include <glm/glm.hpp>

void TreeEditorGui::render(Renderer& renderer) {
    if (!visible) return;

    auto& treeSystem = renderer.getTreeEditSystem();

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(ImVec2(380, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 720), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Tree Editor", &visible, windowFlags)) {
        // Enable/disable toggle
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
        ImGui::Text("TREE EDITOR MODE");
        ImGui::PopStyleColor();

        bool enabled = treeSystem.isEnabled();
        if (ImGui::Checkbox("Enable Tree Editor", &enabled)) {
            treeSystem.setEnabled(enabled);
        }

        if (!enabled) {
            ImGui::TextDisabled("Enable to edit procedural tree");
            ImGui::End();
            return;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Visualization options
        bool wireframe = treeSystem.isWireframeMode();
        if (ImGui::Checkbox("Wireframe Mode", &wireframe)) {
            treeSystem.setWireframeMode(wireframe);
        }

        bool showLeaves = treeSystem.getShowLeaves();
        if (ImGui::Checkbox("Show Leaves", &showLeaves)) {
            treeSystem.setShowLeaves(showLeaves);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        renderTrunkSection(renderer);
        renderBranchSection(renderer);
        renderVariationSection(renderer);
        renderLeafSection(renderer);
        renderSeedSection(renderer);
        renderTransformSection(renderer);
        renderPresets(renderer);
    }
    ImGui::End();
}

void TreeEditorGui::renderTrunkSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.5f, 0.3f, 1.0f));
    ImGui::Text("TRUNK");
    ImGui::PopStyleColor();

    if (ImGui::SliderFloat("Height", &params.trunkHeight, 1.0f, 20.0f)) changed = true;
    if (ImGui::SliderFloat("Radius", &params.trunkRadius, 0.1f, 1.0f)) changed = true;
    if (ImGui::SliderFloat("Taper", &params.trunkTaper, 0.1f, 1.0f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderBranchSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.4f, 0.3f, 1.0f));
    ImGui::Text("BRANCHES");
    ImGui::PopStyleColor();

    if (ImGui::SliderInt("Levels", &params.branchLevels, 1, 5)) changed = true;
    if (ImGui::SliderInt("Children/Branch", &params.childrenPerBranch, 1, 8)) changed = true;
    if (ImGui::SliderFloat("Branching Angle", &params.branchingAngle, 10.0f, 80.0f, "%.0f deg")) changed = true;
    if (ImGui::SliderFloat("Spread", &params.branchingSpread, 30.0f, 360.0f, "%.0f deg")) changed = true;
    if (ImGui::SliderFloat("Length Ratio", &params.branchLengthRatio, 0.3f, 0.9f)) changed = true;
    if (ImGui::SliderFloat("Radius Ratio", &params.branchRadiusRatio, 0.3f, 0.8f)) changed = true;
    if (ImGui::SliderFloat("Start Height", &params.branchStartHeight, 0.2f, 0.8f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderVariationSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("VARIATION");
    ImGui::PopStyleColor();

    if (ImGui::SliderFloat("Gnarliness", &params.gnarliness, 0.0f, 1.0f)) changed = true;
    if (ImGui::SliderFloat("Twist", &params.twistAngle, 0.0f, 45.0f, "%.0f deg")) changed = true;
    if (ImGui::SliderFloat("Growth Influence", &params.growthInfluence, -1.0f, 1.0f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderLeafSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    ImGui::Text("LEAVES");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Generate Leaves", &params.generateLeaves)) changed = true;
    if (params.generateLeaves) {
        if (ImGui::SliderFloat("Leaf Size", &params.leafSize, 0.1f, 1.0f)) changed = true;
        if (ImGui::SliderInt("Leaves/Branch", &params.leavesPerBranch, 1, 20)) changed = true;
        if (ImGui::SliderInt("Start Level", &params.leafStartLevel, 1, params.branchLevels)) changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderSeedSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("SEED");
    ImGui::PopStyleColor();

    int seed = static_cast<int>(params.seed);
    if (ImGui::InputInt("Seed", &seed)) {
        params.seed = static_cast<uint32_t>(seed);
        changed = true;
    }

    if (ImGui::Button("Random Seed")) {
        params.seed = static_cast<uint32_t>(rand());
        changed = true;
    }

    ImGui::Spacing();

    // Regenerate button
    if (changed || ImGui::Button("Regenerate Tree", ImVec2(-1, 30))) {
        treeSystem.regenerateTree();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void TreeEditorGui::renderTransformSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("TRANSFORM");
    ImGui::PopStyleColor();

    glm::vec3 pos = treeSystem.getPosition();
    float position[3] = {pos.x, pos.y, pos.z};
    if (ImGui::DragFloat3("Position", position, 0.5f)) {
        treeSystem.setPosition(glm::vec3(position[0], position[1], position[2]));
    }

    float scale = treeSystem.getScale();
    if (ImGui::SliderFloat("Scale", &scale, 0.1f, 5.0f)) {
        treeSystem.setScale(scale);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void TreeEditorGui::renderPresets(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.6f, 1.0f));
    ImGui::Text("PRESETS");
    ImGui::PopStyleColor();

    if (ImGui::Button("Oak", ImVec2(60, 0))) {
        params.trunkHeight = 8.0f;
        params.trunkRadius = 0.4f;
        params.branchLevels = 4;
        params.childrenPerBranch = 4;
        params.branchingAngle = 40.0f;
        params.branchingSpread = 120.0f;
        params.gnarliness = 0.3f;
        params.leafSize = 0.25f;
        treeSystem.regenerateTree();
    }
    ImGui::SameLine();
    if (ImGui::Button("Pine", ImVec2(60, 0))) {
        params.trunkHeight = 12.0f;
        params.trunkRadius = 0.3f;
        params.trunkTaper = 0.8f;
        params.branchLevels = 3;
        params.childrenPerBranch = 6;
        params.branchingAngle = 65.0f;
        params.branchingSpread = 360.0f;
        params.branchLengthRatio = 0.5f;
        params.gnarliness = 0.1f;
        params.leafSize = 0.15f;
        treeSystem.regenerateTree();
    }
    ImGui::SameLine();
    if (ImGui::Button("Willow", ImVec2(60, 0))) {
        params.trunkHeight = 6.0f;
        params.trunkRadius = 0.35f;
        params.branchLevels = 4;
        params.childrenPerBranch = 5;
        params.branchingAngle = 50.0f;
        params.branchLengthRatio = 0.8f;
        params.gnarliness = 0.5f;
        params.growthInfluence = -0.3f;  // Drooping
        params.leafSize = 0.2f;
        treeSystem.regenerateTree();
    }
    if (ImGui::Button("Shrub", ImVec2(60, 0))) {
        params.trunkHeight = 2.0f;
        params.trunkRadius = 0.15f;
        params.branchLevels = 3;
        params.childrenPerBranch = 5;
        params.branchingAngle = 45.0f;
        params.branchStartHeight = 0.1f;
        params.gnarliness = 0.4f;
        params.leafSize = 0.3f;
        treeSystem.regenerateTree();
    }
    ImGui::SameLine();
    if (ImGui::Button("Birch", ImVec2(60, 0))) {
        params.trunkHeight = 10.0f;
        params.trunkRadius = 0.2f;
        params.trunkTaper = 0.9f;
        params.branchLevels = 3;
        params.childrenPerBranch = 3;
        params.branchingAngle = 30.0f;
        params.branchStartHeight = 0.5f;
        params.gnarliness = 0.15f;
        params.leafSize = 0.2f;
        treeSystem.regenerateTree();
    }
}
