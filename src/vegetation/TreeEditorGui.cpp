#include "TreeEditorGui.h"
#include "Renderer.h"
#include "Camera.h"
#include "TreeGenerator.h"
#include "BillboardCapture.h"
#include "TreeEditSystem.h"
#include "SceneManager.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <cmath>
#include <ctime>

// Helper arrays for combo boxes
static const char* algorithmNames[] = { "Recursive", "Space Colonisation" };
static const char* shapeNames[] = { "Sphere", "Hemisphere", "Cone", "Cylinder", "Ellipsoid", "Box" };
static const char* barkTypeNames[] = { "Oak", "Birch", "Pine", "Willow" };
static const char* leafTypeNames[] = { "Oak", "Ash", "Aspen", "Pine" };
static const char* billboardModeNames[] = { "Single", "Double" };

void TreeEditorGui::placeTreeAtCamera(Renderer& renderer, const Camera& camera) {
    auto& treeSystem = renderer.getTreeEditSystem();

    // Get camera position and forward direction
    glm::vec3 camPos = camera.getPosition();
    glm::vec3 forward = camera.getFront();

    // Project forward onto XZ plane and normalize
    glm::vec3 forwardXZ = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));

    // Place tree 15 meters in front of camera
    float distance = 15.0f;
    glm::vec3 treePos = camPos + forwardXZ * distance;

    // Get terrain height at that position
    float terrainHeight = renderer.getTerrainHeightAt(treePos.x, treePos.z);
    treePos.y = terrainHeight;

    treeSystem.setPosition(treePos);

    // Always regenerate when placing to ensure fresh mesh
    // This fixes potential corruption from initial generation during init
    treeSystem.regenerateTree();

    // Enable tree editor if not already enabled
    if (!treeSystem.isEnabled()) {
        treeSystem.setEnabled(true);
    }
}

void TreeEditorGui::render(Renderer& renderer, const Camera& camera) {
    if (!visible) return;

    auto& treeSystem = renderer.getTreeEditSystem();

    // Auto-enable tree edit system when window is visible
    if (!treeSystem.isEnabled()) {
        treeSystem.setEnabled(true);
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowPos(ImVec2(380, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 720), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Tree Editor", &visible, windowFlags)) {
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

        renderAlgorithmSection(renderer);
        renderBarkSection(renderer);

        auto& params = treeSystem.getParameters();
        if (params.algorithm == TreeAlgorithm::SpaceColonisation) {
            renderSpaceColonisationSection(renderer);
        } else {
            renderTrunkSection(renderer);
            renderBranchSection(renderer);
            renderVariationSection(renderer);
        }

        renderLeafSection(renderer);
        renderSeedSection(renderer);
        renderTransformSection(renderer, camera);
        renderPresets(renderer);
        renderBillboardSection(renderer);
    }
    ImGui::End();
}

void TreeEditorGui::renderAlgorithmSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.3f, 1.0f));
    ImGui::Text("ALGORITHM");
    ImGui::PopStyleColor();

    int currentAlgo = static_cast<int>(params.algorithm);
    if (ImGui::Combo("Algorithm", &currentAlgo, algorithmNames, IM_ARRAYSIZE(algorithmNames))) {
        params.algorithm = static_cast<TreeAlgorithm>(currentAlgo);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderBarkSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.5f, 0.3f, 1.0f));
    ImGui::Text("BARK");
    ImGui::PopStyleColor();

    int barkType = static_cast<int>(params.barkType);
    if (ImGui::Combo("Bark Type", &barkType, barkTypeNames, IM_ARRAYSIZE(barkTypeNames))) {
        params.barkType = static_cast<BarkType>(barkType);
        changed = true;
    }

    // Bark tint color picker
    float barkTint[3] = { params.barkTint.r, params.barkTint.g, params.barkTint.b };
    if (ImGui::ColorEdit3("Bark Tint", barkTint)) {
        params.barkTint = glm::vec3(barkTint[0], barkTint[1], barkTint[2]);
        changed = true;
    }

    if (ImGui::Checkbox("Textured", &params.barkTextured)) {
        changed = true;
    }

    // Texture scale
    float texScale[2] = { params.barkTextureScale.x, params.barkTextureScale.y };
    if (ImGui::SliderFloat2("Texture Scale", texScale, 0.5f, 5.0f)) {
        params.barkTextureScale = glm::vec2(texScale[0], texScale[1]);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
}

void TreeEditorGui::renderSpaceColonisationSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();
    auto& params = treeSystem.getParameters();
    auto& scParams = params.spaceColonisation;
    bool changed = false;

    // Crown Shape Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("CROWN VOLUME");
    ImGui::PopStyleColor();

    int crownShape = static_cast<int>(scParams.crownShape);
    if (ImGui::Combo("Crown Shape", &crownShape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
        scParams.crownShape = static_cast<VolumeShape>(crownShape);
        changed = true;
    }

    if (ImGui::SliderFloat("Crown Radius", &scParams.crownRadius, 1.0f, 10.0f)) changed = true;
    if (ImGui::SliderFloat("Crown Height", &scParams.crownHeight, 1.0f, 10.0f)) changed = true;

    if (scParams.crownShape == VolumeShape::Ellipsoid) {
        float scale[3] = { scParams.crownScale.x, scParams.crownScale.y, scParams.crownScale.z };
        if (ImGui::SliderFloat3("Crown Scale", scale, 0.5f, 2.0f)) {
            scParams.crownScale = glm::vec3(scale[0], scale[1], scale[2]);
            changed = true;
        }
    }

    float offset[3] = { scParams.crownOffset.x, scParams.crownOffset.y, scParams.crownOffset.z };
    if (ImGui::SliderFloat3("Crown Offset", offset, -3.0f, 3.0f)) {
        scParams.crownOffset = glm::vec3(offset[0], offset[1], offset[2]);
        changed = true;
    }

    if (ImGui::SliderFloat("Exclusion Radius", &scParams.crownExclusionRadius, 0.0f, 3.0f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Trunk Section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.5f, 0.3f, 1.0f));
    ImGui::Text("TRUNK");
    ImGui::PopStyleColor();

    if (ImGui::SliderFloat("Trunk Height", &scParams.trunkHeight, 0.5f, 10.0f)) changed = true;
    if (ImGui::SliderInt("Trunk Segments", &scParams.trunkSegments, 1, 10)) changed = true;
    if (ImGui::SliderFloat("Base Thickness", &scParams.baseThickness, 0.1f, 1.0f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Algorithm Parameters
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("ALGORITHM PARAMS");
    ImGui::PopStyleColor();

    if (ImGui::SliderInt("Attraction Points", &scParams.attractionPointCount, 100, 2000)) changed = true;
    if (ImGui::SliderFloat("Attraction Dist", &scParams.attractionDistance, 0.5f, 8.0f)) changed = true;
    if (ImGui::SliderFloat("Kill Distance", &scParams.killDistance, 0.1f, 2.0f)) changed = true;
    if (ImGui::SliderFloat("Segment Length", &scParams.segmentLength, 0.1f, 1.0f)) changed = true;
    if (ImGui::SliderInt("Max Iterations", &scParams.maxIterations, 50, 500)) changed = true;

    ImGui::Spacing();

    // Tropism
    if (ImGui::SliderFloat("Tropism Strength", &scParams.tropismStrength, 0.0f, 0.5f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Thickness Model
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.4f, 1.0f));
    ImGui::Text("BRANCH THICKNESS");
    ImGui::PopStyleColor();

    if (ImGui::SliderFloat("Thickness Power", &scParams.thicknessPower, 1.5f, 3.0f)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Da Vinci's rule: 2.0 = area conserving");
    }
    if (ImGui::SliderFloat("Min Thickness", &scParams.minThickness, 0.01f, 0.1f)) changed = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Root System
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.4f, 0.3f, 1.0f));
    ImGui::Text("ROOT SYSTEM");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Generate Roots", &scParams.generateRoots)) changed = true;

    if (scParams.generateRoots) {
        int rootShape = static_cast<int>(scParams.rootShape);
        if (ImGui::Combo("Root Shape", &rootShape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
            scParams.rootShape = static_cast<VolumeShape>(rootShape);
            changed = true;
        }
        if (ImGui::SliderFloat("Root Radius", &scParams.rootRadius, 0.5f, 5.0f)) changed = true;
        if (ImGui::SliderFloat("Root Depth", &scParams.rootDepth, 0.5f, 4.0f)) changed = true;
        if (ImGui::SliderInt("Root Points", &scParams.rootAttractionPointCount, 50, 500)) changed = true;
        if (ImGui::SliderFloat("Root Tropism", &scParams.rootTropismStrength, 0.0f, 0.8f)) changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Geometry Quality
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
    ImGui::Text("GEOMETRY QUALITY");
    ImGui::PopStyleColor();

    if (ImGui::SliderInt("Radial Segments", &scParams.radialSegments, 4, 16)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Segments around branch circumference");
    }
    if (ImGui::SliderInt("Curve Subdivisions", &scParams.curveSubdivisions, 1, 8)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Subdivisions for smooth curved branches");
    }
    if (ImGui::SliderFloat("Smoothing", &scParams.smoothingStrength, 0.0f, 1.0f)) changed = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Amount of curve smoothing applied");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (changed) {
        treeSystem.regenerateTree();
    }
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

    if (ImGui::SliderInt("Levels", &params.branchLevels, 1, 5)) {
        // Clamp leafStartLevel to not exceed branchLevels
        if (params.leafStartLevel > params.branchLevels) {
            params.leafStartLevel = params.branchLevels;
        }
        changed = true;
    }
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
        // Leaf texture type
        int leafType = static_cast<int>(params.leafType);
        if (ImGui::Combo("Leaf Type", &leafType, leafTypeNames, IM_ARRAYSIZE(leafTypeNames))) {
            params.leafType = static_cast<LeafType>(leafType);
            changed = true;
        }

        // Leaf tint color
        float leafTint[3] = { params.leafTint.r, params.leafTint.g, params.leafTint.b };
        if (ImGui::ColorEdit3("Leaf Tint", leafTint)) {
            params.leafTint = glm::vec3(leafTint[0], leafTint[1], leafTint[2]);
            changed = true;
        }

        // Billboard mode
        int billboardMode = static_cast<int>(params.leafBillboard);
        if (ImGui::Combo("Billboard Mode", &billboardMode, billboardModeNames, IM_ARRAYSIZE(billboardModeNames))) {
            params.leafBillboard = static_cast<BillboardMode>(billboardMode);
            changed = true;
        }

        if (ImGui::SliderFloat("Leaf Size", &params.leafSize, 0.1f, 5.0f)) changed = true;
        if (ImGui::SliderFloat("Size Variance", &params.leafSizeVariance, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderInt("Leaves/Branch", &params.leavesPerBranch, 1, 20)) changed = true;
        if (ImGui::SliderFloat("Leaf Angle", &params.leafAngle, 0.0f, 90.0f, "%.0f deg")) changed = true;
        if (ImGui::SliderFloat("Leaf Start", &params.leafStart, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderInt("Start Level", &params.leafStartLevel, 1, params.branchLevels)) changed = true;
        if (ImGui::SliderFloat("Alpha Test", &params.leafAlphaTest, 0.0f, 1.0f)) changed = true;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Discard pixels with alpha below this threshold");
        }
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

void TreeEditorGui::renderTransformSection(Renderer& renderer, const Camera& camera) {
    auto& treeSystem = renderer.getTreeEditSystem();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("TRANSFORM");
    ImGui::PopStyleColor();

    // Place at camera button
    if (ImGui::Button("Place at Camera (P)", ImVec2(-1, 0))) {
        placeTreeAtCamera(renderer, camera);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Place tree 15m in front of camera on terrain");
    }

    ImGui::Spacing();

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

    if (params.algorithm == TreeAlgorithm::Recursive) {
        // Recursive algorithm presets (use legacy parameters)
        if (ImGui::Button("Oak", ImVec2(60, 0))) {
            params.usePerLevelParams = false;
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
            params.usePerLevelParams = false;
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
            params.usePerLevelParams = false;
            params.trunkHeight = 6.0f;
            params.trunkRadius = 0.35f;
            params.branchLevels = 4;
            params.childrenPerBranch = 5;
            params.branchingAngle = 50.0f;
            params.branchLengthRatio = 0.8f;
            params.gnarliness = 0.5f;
            params.growthInfluence = -0.3f;
            params.leafSize = 0.2f;
            treeSystem.regenerateTree();
        }
        if (ImGui::Button("Shrub", ImVec2(60, 0))) {
            params.usePerLevelParams = false;
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
            params.usePerLevelParams = false;
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
    } else {
        // Space colonisation presets
        auto& sc = params.spaceColonisation;

        if (ImGui::Button("Sphere Oak", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Sphere;
            sc.crownRadius = 4.0f;
            sc.crownHeight = 4.0f;
            sc.trunkHeight = 3.0f;
            sc.baseThickness = 0.35f;
            sc.attractionPointCount = 600;
            sc.attractionDistance = 3.0f;
            sc.killDistance = 0.5f;
            sc.segmentLength = 0.25f;
            sc.tropismStrength = 0.1f;
            sc.generateRoots = false;
            params.leafSize = 0.25f;
            treeSystem.regenerateTree();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cone Pine", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Cone;
            sc.crownRadius = 3.0f;
            sc.crownHeight = 7.0f;
            sc.crownOffset = glm::vec3(0.0f, -1.0f, 0.0f);
            sc.trunkHeight = 4.0f;
            sc.baseThickness = 0.25f;
            sc.attractionPointCount = 800;
            sc.attractionDistance = 2.5f;
            sc.killDistance = 0.4f;
            sc.segmentLength = 0.2f;
            sc.tropismStrength = 0.15f;
            sc.generateRoots = false;
            params.leafSize = 0.12f;
            treeSystem.regenerateTree();
        }

        if (ImGui::Button("Hemisphere", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Hemisphere;
            sc.crownRadius = 5.0f;
            sc.crownHeight = 5.0f;
            sc.crownOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            sc.trunkHeight = 2.5f;
            sc.baseThickness = 0.4f;
            sc.attractionPointCount = 700;
            sc.attractionDistance = 3.5f;
            sc.killDistance = 0.5f;
            sc.segmentLength = 0.3f;
            sc.tropismStrength = 0.05f;
            sc.generateRoots = false;
            params.leafSize = 0.3f;
            treeSystem.regenerateTree();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ellipsoid", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Ellipsoid;
            sc.crownRadius = 3.0f;
            sc.crownScale = glm::vec3(1.5f, 1.0f, 1.5f);
            sc.crownOffset = glm::vec3(0.0f, 0.5f, 0.0f);
            sc.trunkHeight = 4.0f;
            sc.baseThickness = 0.3f;
            sc.attractionPointCount = 500;
            sc.attractionDistance = 3.0f;
            sc.killDistance = 0.45f;
            sc.segmentLength = 0.25f;
            sc.tropismStrength = 0.1f;
            sc.generateRoots = false;
            params.leafSize = 0.22f;
            treeSystem.regenerateTree();
        }

        if (ImGui::Button("With Roots", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Sphere;
            sc.crownRadius = 3.5f;
            sc.crownHeight = 3.5f;
            sc.crownOffset = glm::vec3(0.0f, 0.0f, 0.0f);
            sc.trunkHeight = 2.5f;
            sc.baseThickness = 0.35f;
            sc.attractionPointCount = 500;
            sc.attractionDistance = 2.8f;
            sc.killDistance = 0.4f;
            sc.segmentLength = 0.25f;
            sc.tropismStrength = 0.1f;
            sc.generateRoots = true;
            sc.rootShape = VolumeShape::Hemisphere;
            sc.rootRadius = 2.5f;
            sc.rootDepth = 1.5f;
            sc.rootAttractionPointCount = 250;
            sc.rootTropismStrength = 0.4f;
            params.leafSize = 0.2f;
            treeSystem.regenerateTree();
        }
        ImGui::SameLine();
        if (ImGui::Button("Bonsai", ImVec2(80, 0))) {
            sc.crownShape = VolumeShape::Hemisphere;
            sc.crownRadius = 1.5f;
            sc.crownHeight = 1.5f;
            sc.crownExclusionRadius = 0.3f;
            sc.crownOffset = glm::vec3(0.3f, 0.0f, 0.0f);
            sc.trunkHeight = 1.0f;
            sc.trunkSegments = 2;
            sc.baseThickness = 0.15f;
            sc.attractionPointCount = 300;
            sc.attractionDistance = 1.5f;
            sc.killDistance = 0.2f;
            sc.segmentLength = 0.1f;
            sc.tropismStrength = 0.05f;
            sc.generateRoots = false;
            params.leafSize = 0.15f;
            treeSystem.regenerateTree();
        }
    }
}

void TreeEditorGui::renderBillboardSection(Renderer& renderer) {
    auto& treeSystem = renderer.getTreeEditSystem();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("BILLBOARD EXPORT");
    ImGui::PopStyleColor();

    // Resolution selector
    static const char* resolutionLabels[] = { "256x256", "512x512", "1024x1024" };
    static const int resolutionValues[] = { 256, 512, 1024 };
    int currentResIdx = (billboardResolution == 256) ? 0 : (billboardResolution == 512) ? 1 : 2;

    if (ImGui::Combo("Resolution", &currentResIdx, resolutionLabels, 3)) {
        billboardResolution = resolutionValues[currentResIdx];
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Resolution per billboard view (17 views total)\nAtlas will be 5x4 = 20 cells");
    }

    ImGui::Spacing();

    // Info about the capture
    ImGui::TextDisabled("Captures: 8 side + 8 angled + 1 top = 17");
    ImGui::TextDisabled("Atlas size: %dx%d pixels", billboardResolution * 5, billboardResolution * 4);

    ImGui::Spacing();

    // Generate button
    if (captureInProgress) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Generate Billboard Atlas", ImVec2(-1, 30))) {
        captureInProgress = true;
        captureStatus = "Initializing...";

        // Initialize billboard capture if needed
        if (!billboardCapture) {
            billboardCapture = std::make_unique<BillboardCapture>();

            BillboardCapture::InitInfo initInfo{};
            initInfo.device = renderer.getDevice();
            initInfo.physicalDevice = renderer.getPhysicalDevice();
            initInfo.allocator = renderer.getVulkanContext().getAllocator();
            initInfo.descriptorPool = renderer.getDescriptorPool();
            initInfo.shaderPath = renderer.getShaderPath();
            initInfo.graphicsQueue = renderer.getGraphicsQueue();
            initInfo.commandPool = renderer.getCommandPool();

            if (!billboardCapture->init(initInfo)) {
                captureStatus = "Failed to initialize capture system";
                captureInProgress = false;
                billboardCapture.reset();
            }
        }

        if (billboardCapture) {
            captureStatus = "Rendering captures...";

            // Wait for GPU to finish any pending work
            vkDeviceWaitIdle(renderer.getDevice());

            // Generate the atlas
            BillboardAtlas atlas;
            bool success = billboardCapture->generateAtlas(
                treeSystem.getBranchMesh(),
                treeSystem.getLeafMesh(),
                treeSystem.getParameters(),
                treeSystem.getBarkColorTexture(),
                treeSystem.getBarkNormalTexture(),
                treeSystem.getBarkAOTexture(),
                treeSystem.getBarkRoughnessTexture(),
                treeSystem.getLeafTexture(),
                billboardResolution,
                atlas
            );

            if (success) {
                // Generate filename with timestamp
                time_t now = time(nullptr);
                struct tm* timeinfo = localtime(&now);
                char filename[256];
                snprintf(filename, sizeof(filename), "tree_billboard_%04d%02d%02d_%02d%02d%02d.png",
                        timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                        timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

                lastExportPath = filename;

                if (BillboardCapture::saveAtlasToPNG(atlas, lastExportPath)) {
                    captureStatus = "Saved: " + lastExportPath;
                } else {
                    captureStatus = "Failed to save PNG";
                }
            } else {
                captureStatus = "Failed to generate atlas";
            }
        }

        captureInProgress = false;
    }

    if (captureInProgress) {
        ImGui::EndDisabled();
    }

    // Show status
    if (!captureStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", captureStatus.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}
