#include "GuiTreeTab.h"
#include "core/interfaces/ITreeControl.h"
#include "vegetation/TreeSystem.h"
#include "vegetation/TreeOptions.h"
#include "vegetation/TreeLODSystem.h"
#include "vegetation/TreeImpostorAtlas.h"
#include "vegetation/TreeRenderer.h"
#include "core/RendererSystems.h"

#include <imgui.h>

void GuiTreeTab::render(ITreeControl& treeControl) {
    auto* treeSystem = treeControl.getTreeSystem();
    if (!treeSystem) {
        ImGui::Text("Tree system not initialized");
        return;
    }

    ImGui::Spacing();

    // Tree selection header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
    ImGui::Text("TREE EDITOR");
    ImGui::PopStyleColor();

    ImGui::Text("Trees: %zu", treeSystem->getTreeCount());

    int selected = treeSystem->getSelectedTreeIndex();
    if (ImGui::InputInt("Selected", &selected)) {
        treeSystem->selectTree(selected);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // LOD Settings Section
    auto* treeLOD = treeControl.getSystems().treeLOD();
    if (treeLOD) {
        if (ImGui::CollapsingHeader("LOD Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& settings = treeLOD->getLODSettings();

            ImGui::Checkbox("Enable Impostors", &settings.enableImpostors);

            ImGui::Spacing();
            ImGui::Text("Distance Thresholds:");
            ImGui::SliderFloat("Full Detail Dist", &settings.fullDetailDistance, 0.0f, 500.0f, "%.1f");
            ImGui::SliderFloat("Impostor Dist", &settings.impostorDistance, 0.0f, 10000.0f, "%.0f");
            ImGui::SliderFloat("Hysteresis", &settings.hysteresis, 0.0f, 20.0f, "%.1f");

            ImGui::Spacing();
            ImGui::Text("Blending:");
            ImGui::SliderFloat("Blend Range", &settings.blendRange, 0.0f, 50.0f, "%.1f");
            ImGui::SliderFloat("Blend Exponent", &settings.blendExponent, 0.0f, 3.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("Impostor Appearance:");
            ImGui::SliderFloat("Brightness", &settings.impostorBrightness, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Normal Strength", &settings.normalStrength, 0.0f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Text("Seasonal Effects (Global):");
            ImGui::SliderFloat("Global Autumn", &settings.autumnHueShift, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Apply autumn colors to all tree impostors\n0 = summer, 1 = full autumn");
            }

            ImGui::Spacing();
            ImGui::Text("Debug:");

            // Show actual calculated elevation
            const auto& dbg = treeLOD->getDebugInfo();
            ImGui::Text("Camera Y: %.1f", dbg.cameraPos.y);
            ImGui::Text("Nearest tree: (%.1f, %.1f, %.1f) dist=%.1f",
                        dbg.nearestTreePos.x, dbg.nearestTreePos.y, dbg.nearestTreePos.z,
                        dbg.nearestTreeDistance);
            ImGui::Text("Calculated elevation: %.1f deg", dbg.calculatedElevation);
            if (dbg.calculatedElevation > 67.5f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(top-down)");
            } else if (dbg.calculatedElevation > 22.5f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(elevated)");
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "(horizon)");
            }

            ImGui::Spacing();
            ImGui::Checkbox("Override Elevation", &settings.enableDebugElevation);
            if (settings.enableDebugElevation) {
                ImGui::SliderFloat("Elevation Angle", &settings.debugElevation, -90.0f, 90.0f, "%.1f deg");
                if (settings.debugElevation > 67.5f) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  -> Top-down view (cell 8)");
                } else if (settings.debugElevation > 22.5f) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  -> Elevated view (row 1)");
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "  -> Horizon view (row 0)");
                }
            }

            ImGui::Checkbox("Show Cell Index Colors", &settings.debugShowCellIndex);
            if (settings.debugShowCellIndex) {
                ImGui::TextWrapped("Colors show which atlas cell is used:");
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Red=0 (+Z)");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Orange=45");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Yellow=90 (+X)");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Green=135");
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Cyan=180 (-Z)");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Blue=225");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.0f, 1.0f, 1.0f), "Purple=270 (-X)");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Magenta=315");
            }

            // Two-phase leaf culling toggle
            auto* treeRenderer = treeControl.getSystems().treeRenderer();
            if (treeRenderer) {
                bool twoPhase = treeRenderer->isTwoPhaseLeafCullingEnabled();
                if (ImGui::Checkbox("Two-Phase Leaf Culling", &twoPhase)) {
                    treeRenderer->setTwoPhaseLeafCulling(twoPhase);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Use efficient two-phase culling:\n"
                                      "1. Filter visible trees from cells\n"
                                      "2. Cull leaves only for visible trees");
                }
            }

            // Atlas preview
            auto* atlas = treeLOD->getImpostorAtlas();
            if (atlas && atlas->getArchetypeCount() > 0) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Impostor Atlas:");
                ImGui::SameLine();
                ImGui::Text("(%zu archetypes)", atlas->getArchetypeCount());

                // Show archetype info
                for (size_t i = 0; i < atlas->getArchetypeCount(); i++) {
                    const auto* archetype = atlas->getArchetype(static_cast<uint32_t>(i));
                    if (archetype) {
                        ImGui::BulletText("%s (r=%.1f, h=%.1f)",
                                         archetype->name.c_str(),
                                         archetype->boundingSphereRadius,
                                         archetype->treeHeight);
                    }
                }

                // Atlas texture preview button
                VkDescriptorSet previewSet = atlas->getPreviewDescriptorSet(0);
                if (previewSet != VK_NULL_HANDLE) {
                    if (ImGui::Button("Preview Atlas Texture")) {
                        ImGui::OpenPopup("AtlasPreview");
                    }

                    // Preview popup window
                    if (ImGui::BeginPopup("AtlasPreview")) {
                        ImGui::Text("Impostor Atlas (9x2 cells, 256px each)");
                        ImGui::Separator();

                        // Draw atlas with cell grid overlay
                        ImVec2 imageSize(ImpostorAtlasConfig::ATLAS_WIDTH * 0.5f,
                                        ImpostorAtlasConfig::ATLAS_HEIGHT * 0.5f);

                        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                        ImGui::Image(reinterpret_cast<ImTextureID>(previewSet), imageSize);

                        // Draw grid lines to show cells
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        float cellW = imageSize.x / ImpostorAtlasConfig::CELLS_PER_ROW;
                        float cellH = imageSize.y / ImpostorAtlasConfig::VERTICAL_LEVELS;
                        ImU32 gridColor = IM_COL32(255, 255, 255, 80);

                        // Vertical lines
                        for (int x = 0; x <= ImpostorAtlasConfig::CELLS_PER_ROW; x++) {
                            float px = cursorPos.x + x * cellW;
                            drawList->AddLine(ImVec2(px, cursorPos.y),
                                            ImVec2(px, cursorPos.y + imageSize.y), gridColor);
                        }
                        // Horizontal lines
                        for (int y = 0; y <= ImpostorAtlasConfig::VERTICAL_LEVELS; y++) {
                            float py = cursorPos.y + y * cellH;
                            drawList->AddLine(ImVec2(cursorPos.x, py),
                                            ImVec2(cursorPos.x + imageSize.x, py), gridColor);
                        }

                        ImGui::Spacing();
                        ImGui::Text("Row 0: 8 horizon views (0-315 deg) + top-down");
                        ImGui::Text("Row 1: 8 elevated views (45 deg elevation)");

                        ImGui::EndPopup();
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
    }

    // Presets
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("PRESETS");
    ImGui::PopStyleColor();

    if (ImGui::Button("Oak")) { treeSystem->loadPreset("oak"); }
    ImGui::SameLine();
    if (ImGui::Button("Pine")) { treeSystem->loadPreset("pine"); }
    ImGui::SameLine();
    if (ImGui::Button("Birch")) { treeSystem->loadPreset("birch"); }

    if (ImGui::Button("Willow")) { treeSystem->loadPreset("willow"); }
    ImGui::SameLine();
    if (ImGui::Button("Aspen")) { treeSystem->loadPreset("aspen"); }
    ImGui::SameLine();
    if (ImGui::Button("Bush")) { treeSystem->loadPreset("bush"); }

    ImGui::Spacing();
    ImGui::Separator();

    // Get current options
    const TreeOptions* currentOpts = treeSystem->getSelectedTreeOptions();
    if (!currentOpts) {
        ImGui::Text("No tree selected");
        return;
    }

    TreeOptions opts = *currentOpts;
    bool changed = false;

    // Seed
    int seed = static_cast<int>(opts.seed);
    if (ImGui::SliderInt("Seed", &seed, 0, 65535)) {
        opts.seed = static_cast<uint32_t>(seed);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Bark section
    if (ImGui::CollapsingHeader("Bark", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* barkTypes[] = {"birch", "oak", "pine", "willow"};
        int barkType = 1;  // Default to oak
        if (opts.bark.type == "birch") barkType = 0;
        else if (opts.bark.type == "oak") barkType = 1;
        else if (opts.bark.type == "pine") barkType = 2;
        else if (opts.bark.type == "willow") barkType = 3;
        if (ImGui::Combo("Bark Type", &barkType, barkTypes, 4)) {
            opts.bark.type = barkTypes[barkType];
            changed = true;
        }

        float tint[3] = {opts.bark.tint.r, opts.bark.tint.g, opts.bark.tint.b};
        if (ImGui::ColorEdit3("Bark Tint", tint)) {
            opts.bark.tint = glm::vec3(tint[0], tint[1], tint[2]);
            changed = true;
        }

        if (ImGui::Checkbox("Flat Shading", &opts.bark.flatShading)) changed = true;
        if (ImGui::Checkbox("Textured", &opts.bark.textured)) changed = true;

        float texScale[2] = {opts.bark.textureScale.x, opts.bark.textureScale.y};
        if (ImGui::SliderFloat2("Texture Scale", texScale, 0.5f, 10.0f)) {
            opts.bark.textureScale = glm::vec2(texScale[0], texScale[1]);
            changed = true;
        }
    }

    // Branches section
    if (ImGui::CollapsingHeader("Branches", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* treeTypes[] = {"Deciduous", "Evergreen"};
        int treeType = static_cast<int>(opts.type);
        if (ImGui::Combo("Tree Type", &treeType, treeTypes, 2)) {
            opts.type = static_cast<TreeType>(treeType);
            changed = true;
        }

        if (ImGui::SliderInt("Levels", &opts.branch.levels, 0, 3)) changed = true;

        // Per-level parameters
        for (int level = 0; level <= opts.branch.levels; ++level) {
            char header[32];
            snprintf(header, sizeof(header), "Level %d", level);

            if (ImGui::TreeNode(header)) {
                char label[64];

                if (level > 0) {
                    snprintf(label, sizeof(label), "Angle##%d", level);
                    if (ImGui::SliderFloat(label, &opts.branch.angle[level], 0.0f, 180.0f, "%.1f deg")) changed = true;

                    snprintf(label, sizeof(label), "Start##%d", level);
                    if (ImGui::SliderFloat(label, &opts.branch.start[level], 0.0f, 1.0f)) changed = true;
                }

                if (level < 3) {
                    snprintf(label, sizeof(label), "Children##%d", level);
                    // ez-tree: level 0 = 0-100, level 1 = 0-10, level 2 = 0-5
                    int maxChildren = (level == 0) ? 100 : (level == 1) ? 10 : 5;
                    if (ImGui::SliderInt(label, &opts.branch.children[level], 0, maxChildren)) changed = true;
                }

                snprintf(label, sizeof(label), "Length##%d", level);
                if (ImGui::SliderFloat(label, &opts.branch.length[level], 0.1f, 100.0f)) changed = true;

                snprintf(label, sizeof(label), "Radius##%d", level);
                if (ImGui::SliderFloat(label, &opts.branch.radius[level], 0.1f, 5.0f)) changed = true;

                snprintf(label, sizeof(label), "Sections##%d", level);
                if (ImGui::SliderInt(label, &opts.branch.sections[level], 1, 20)) changed = true;

                snprintf(label, sizeof(label), "Segments##%d", level);
                if (ImGui::SliderInt(label, &opts.branch.segments[level], 3, 16)) changed = true;

                snprintf(label, sizeof(label), "Taper##%d", level);
                if (ImGui::SliderFloat(label, &opts.branch.taper[level], 0.0f, 1.0f)) changed = true;

                snprintf(label, sizeof(label), "Twist##%d", level);
                if (ImGui::SliderFloat(label, &opts.branch.twist[level], -0.5f, 0.5f)) changed = true;

                snprintf(label, sizeof(label), "Gnarliness##%d", level);
                if (ImGui::SliderFloat(label, &opts.branch.gnarliness[level], -0.5f, 0.5f)) changed = true;

                ImGui::TreePop();
            }
        }

        // Growth force
        if (ImGui::TreeNode("Growth Force")) {
            float force[3] = {opts.branch.forceDirection.x, opts.branch.forceDirection.y, opts.branch.forceDirection.z};
            if (ImGui::SliderFloat3("Direction", force, -1.0f, 1.0f)) {
                opts.branch.forceDirection = glm::vec3(force[0], force[1], force[2]);
                changed = true;
            }
            if (ImGui::SliderFloat("Strength", &opts.branch.forceStrength, -0.1f, 0.1f)) changed = true;
            ImGui::TreePop();
        }
    }

    // Leaves section
    if (ImGui::CollapsingHeader("Leaves")) {
        const char* leafTypes[] = {"ash", "aspen", "pine", "oak"};
        int leafType = 3;  // Default to oak
        if (opts.leaves.type == "ash") leafType = 0;
        else if (opts.leaves.type == "aspen") leafType = 1;
        else if (opts.leaves.type == "pine") leafType = 2;
        else if (opts.leaves.type == "oak") leafType = 3;
        if (ImGui::Combo("Leaf Type", &leafType, leafTypes, 4)) {
            opts.leaves.type = leafTypes[leafType];
            changed = true;
        }

        const char* billboardModes[] = {"Single", "Double"};
        int billboard = static_cast<int>(opts.leaves.billboard);
        if (ImGui::Combo("Billboard", &billboard, billboardModes, 2)) {
            opts.leaves.billboard = static_cast<BillboardMode>(billboard);
            changed = true;
        }

        if (ImGui::SliderFloat("Angle", &opts.leaves.angle, 0.0f, 100.0f, "%.1f deg")) changed = true;
        if (ImGui::SliderInt("Count", &opts.leaves.count, 0, 100)) changed = true;
        if (ImGui::SliderFloat("Start", &opts.leaves.start, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderFloat("Size", &opts.leaves.size, 0.0f, 10.0f)) changed = true;
        if (ImGui::SliderFloat("Size Variance", &opts.leaves.sizeVariance, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderFloat("Alpha Test", &opts.leaves.alphaTest, 0.0f, 1.0f)) changed = true;

        float leafTint[3] = {opts.leaves.tint.r, opts.leaves.tint.g, opts.leaves.tint.b};
        if (ImGui::ColorEdit3("Leaf Tint", leafTint)) {
            opts.leaves.tint = glm::vec3(leafTint[0], leafTint[1], leafTint[2]);
            changed = true;
        }

        ImGui::Spacing();
        ImGui::Text("Seasonal Effects:");
        if (ImGui::SliderFloat("Autumn", &opts.leaves.autumnHueShift, 0.0f, 1.0f, "%.2f")) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shift leaf colors toward autumn tones\n0 = summer green, 1 = full autumn");
        }
    }

    // Apply changes
    if (changed) {
        treeSystem->updateSelectedTreeOptions(opts);
    }
}
