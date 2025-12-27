#include "GuiTreeTab.h"
#include "core/interfaces/ITreeControl.h"
#include "vegetation/TreeSystem.h"
#include "vegetation/TreeOptions.h"
#include "vegetation/TreeLODSystem.h"
#include "vegetation/TreeImpostorAtlas.h"
#include "vegetation/TreeRenderer.h"
#include "vegetation/ImpostorCullSystem.h"
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
            ImGui::Text("LOD Mode:");
            ImGui::Checkbox("Use Screen-Space Error", &settings.useScreenSpaceError);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use FOV-aware screen-space error for LOD selection.\n"
                                  "Gives consistent quality across resolutions and zoom levels.\n"
                                  "When disabled, uses fixed distance thresholds.");
            }

            ImGui::Spacing();
            if (settings.useScreenSpaceError) {
                // Screen-space error LOD controls
                ImGui::Text("Screen-Space Error Thresholds:");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  (High error = close/large, Low error = far/small)");

                ImGui::SliderFloat("Detail Threshold", &settings.errorThresholdFull, 0.5f, 20.0f, "%.1f px");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Minimum screen error for full geometry.\n"
                                      "Lower = more trees use full geometry (higher quality).\n"
                                      "Higher = fewer trees use full geometry (better performance).");
                }

                // Clamp impostor error to be less than full detail error
                float maxImpostorError = settings.errorThresholdFull - 0.1f;
                if (settings.errorThresholdImpostor > maxImpostorError) {
                    settings.errorThresholdImpostor = maxImpostorError;
                }
                ImGui::SliderFloat("Impostor Threshold", &settings.errorThresholdImpostor, 0.1f, maxImpostorError, "%.2f px");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Maximum screen error for pure impostor.\n"
                                      "Lower = impostors used only for distant trees.\n"
                                      "Blend zone exists between Detail and Impostor thresholds.");
                }

                // Clamp cull error to be less than impostor error
                float maxCullError = settings.errorThresholdImpostor - 0.01f;
                if (settings.errorThresholdCull > maxCullError) {
                    settings.errorThresholdCull = maxCullError;
                }
                ImGui::SliderFloat("Cull Threshold", &settings.errorThresholdCull, 0.01f, maxCullError, "%.3f px");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Maximum screen error for culling.\n"
                                      "Lower = only cull extremely distant trees.\n"
                                      "Higher = more aggressive culling (better performance).");
                }
            } else {
                // Distance-based LOD controls
                ImGui::Text("Distance Thresholds:");
                ImGui::SliderFloat("Full Detail Dist", &settings.fullDetailDistance, 0.0f, 500.0f, "%.1f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Trees closer than this use full geometry.");
                }
                ImGui::SliderFloat("Impostor Dist", &settings.impostorDistance, 0.0f, 10000.0f, "%.0f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Trees beyond this distance are culled.");
                }
                ImGui::SliderFloat("Hysteresis", &settings.hysteresis, 0.0f, 20.0f, "%.1f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Dead zone to prevent flickering at LOD boundaries.");
                }

                ImGui::Spacing();
                ImGui::Text("Blending:");
                ImGui::SliderFloat("Blend Range", &settings.blendRange, 0.0f, 50.0f, "%.1f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Distance over which to blend between geometry and impostor.");
                }
                ImGui::SliderFloat("Blend Exponent", &settings.blendExponent, 0.0f, 3.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Blend curve: 1.0 = linear, >1 = faster falloff.");
                }
            }

            // Adaptive LOD Settings
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
            ImGui::Text("Adaptive LOD (Performance Budget):");
            ImGui::PopStyleColor();

            auto& adaptiveLOD = treeLOD->getAdaptiveLODState();
            ImGui::Checkbox("Enable Adaptive LOD", &adaptiveLOD.enabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically adjust LOD quality based on scene complexity.\n"
                                  "Sparse scenes (single tree) get higher quality.\n"
                                  "Dense scenes reduce quality to maintain performance.");
            }

            if (adaptiveLOD.enabled) {
                int budget = static_cast<int>(adaptiveLOD.leafBudget);
                if (ImGui::SliderInt("Leaf Budget", &budget, 50000, 2000000, "%d leaves")) {
                    adaptiveLOD.leafBudget = static_cast<uint32_t>(budget);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Target maximum leaves per frame.\n"
                                      "Lower = more aggressive quality scaling.\n"
                                      "Higher = allows more leaves before reducing quality.");
                }

                ImGui::SliderFloat("Smoothing", &adaptiveLOD.scaleSmoothing, 0.01f, 0.3f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How quickly quality adapts to scene changes.\n"
                                      "Lower = smoother transitions.\n"
                                      "Higher = faster response.");
                }

                // Display current state
                ImGui::Spacing();
                float budgetRatio = static_cast<float>(adaptiveLOD.lastFrameLeafCount) /
                                    static_cast<float>(adaptiveLOD.leafBudget) * 100.0f;
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                                   "Leaves: %u / %u (%.1f%%)",
                                   adaptiveLOD.lastFrameLeafCount,
                                   adaptiveLOD.leafBudget,
                                   budgetRatio);
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                                   "Quality Scale: %.2fx", adaptiveLOD.adaptiveScale);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.7f, 1.0f));
            ImGui::Text("Reduced Detail LOD (LOD1):");
            ImGui::PopStyleColor();

            ImGui::Checkbox("Enable LOD1", &settings.enableReducedDetailLOD);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable intermediate LOD with reduced geometry.\n"
                                  "Uses fewer, larger leaves at medium distance.\n"
                                  "Bridges gap between full detail and impostor.");
            }

            if (settings.enableReducedDetailLOD) {
                ImGui::Indent();

                if (settings.useScreenSpaceError) {
                    // Clamp reduced threshold between full and impostor
                    float minReduced = settings.errorThresholdImpostor + 0.1f;
                    float maxReduced = settings.errorThresholdFull - 0.1f;
                    if (settings.errorThresholdReduced < minReduced) {
                        settings.errorThresholdReduced = minReduced;
                    }
                    if (settings.errorThresholdReduced > maxReduced) {
                        settings.errorThresholdReduced = maxReduced;
                    }
                    ImGui::SliderFloat("LOD1 Threshold", &settings.errorThresholdReduced, minReduced, maxReduced, "%.2f px");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Screen error threshold for LOD1.\n"
                                          "Must be between Detail (%.1f) and Impostor (%.1f).\n"
                                          "Trees with error below this use reduced geometry.",
                                          settings.errorThresholdFull, settings.errorThresholdImpostor);
                    }
                } else {
                    // Distance-based threshold
                    float maxDist = settings.fullDetailDistance - 10.0f;
                    if (settings.reducedDetailDistance > maxDist) {
                        settings.reducedDetailDistance = maxDist;
                    }
                    ImGui::SliderFloat("LOD1 Distance", &settings.reducedDetailDistance, 50.0f, maxDist, "%.0f m");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Trees beyond this distance use reduced geometry.\n"
                                          "Must be less than Full Detail Distance (%.0f m).",
                                          settings.fullDetailDistance);
                    }
                }

                ImGui::Spacing();
                ImGui::Text("LOD1 Leaf Settings:");
                ImGui::SliderFloat("Leaf Scale", &settings.reducedDetailLeafScale, 1.0f, 4.0f, "%.1fx");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Size multiplier for LOD1 leaves.\n"
                                      "Larger leaves compensate for reduced count.\n"
                                      "Default: 2x (half leaves, double size).");
                }

                ImGui::SliderFloat("Leaf Density", &settings.reducedDetailLeafDensity, 0.1f, 1.0f, "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Fraction of leaves to render in LOD1.\n"
                                      "0.5 = 50%% of leaves (every other leaf).\n"
                                      "Lower = better performance, less detail.");
                }

                // Show effective leaf coverage
                float coverage = settings.reducedDetailLeafScale * settings.reducedDetailLeafScale * settings.reducedDetailLeafDensity;
                ImGui::TextColored(
                    coverage >= 0.9f ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) :
                    coverage >= 0.7f ? ImVec4(1.0f, 1.0f, 0.5f, 1.0f) :
                                       ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    "Effective coverage: %.0f%%", coverage * 100.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Approximate visual coverage compared to LOD0.\n"
                                      "= scale^2 * density\n"
                                      "100%% = same coverage as full detail.");
                }

                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::Separator();
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
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("Octahedral Impostor Atlas:");
            ImGui::PopStyleColor();
            ImGui::Checkbox("Frame Blending", &settings.enableFrameBlending);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Blend between 3 nearest frames for smooth transitions.\n"
                                  "Eliminates popping when view angle changes.\n"
                                  "Slightly more expensive (3 texture lookups).");
            }
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "  8x8 grid = 64 views");

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

            // Temporal coherence toggle (Phase 5)
            auto* impostorCull = treeControl.getSystems().impostorCull();
            if (impostorCull) {
                bool temporal = impostorCull->isTemporalEnabled();
                if (ImGui::Checkbox("Temporal Coherence", &temporal)) {
                    impostorCull->setTemporalEnabled(temporal);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reuse visibility data across frames:\n"
                                      "- Skips culling when camera stationary\n"
                                      "- Partial updates when moving slowly\n"
                                      "- Full update on significant movement");
                }
                if (temporal) {
                    auto& tempSettings = impostorCull->getTemporalSettings();
                    ImGui::SliderFloat("Position Threshold", &tempSettings.positionThreshold, 1.0f, 20.0f, "%.1f m");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Camera movement distance that triggers full visibility update");
                    }
                    ImGui::SliderFloat("Rotation Threshold", &tempSettings.rotationThreshold, 2.0f, 30.0f, "%.1f deg");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Camera rotation angle that triggers full visibility update");
                    }
                    ImGui::SliderFloat("Partial Update", &tempSettings.partialUpdateFraction, 0.05f, 0.5f, "%.0f%%",
                                       ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Fraction of trees updated per frame during partial mode");
                    }
                }
            }

            // Shadow LOD Settings
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
            ImGui::Text("Shadow LOD:");
            ImGui::PopStyleColor();

            auto& shadowSettings = settings.shadow;
            ImGui::Checkbox("Cascade-Aware Shadows", &shadowSettings.enableCascadeLOD);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use different LOD levels for near vs far shadow cascades.\n"
                                  "Far cascades use impostors only, reducing draw calls.");
            }

            if (shadowSettings.enableCascadeLOD) {
                int geomCutoff = static_cast<int>(shadowSettings.geometryCascadeCutoff);
                if (ImGui::SliderInt("Geometry Cutoff", &geomCutoff, 1, 4)) {
                    shadowSettings.geometryCascadeCutoff = static_cast<uint32_t>(geomCutoff);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cascades 0-%d render full tree geometry.\n"
                                      "Cascades %d-3 render impostors only.",
                                      geomCutoff - 1, geomCutoff);
                }

                int leafCutoff = static_cast<int>(shadowSettings.leafCascadeCutoff);
                if (ImGui::SliderInt("Leaf Cutoff", &leafCutoff, 1, 4)) {
                    shadowSettings.leafCascadeCutoff = static_cast<uint32_t>(leafCutoff);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cascades 0-%d render leaf shadows.\n"
                                      "Cascades %d-3 skip leaf shadows (branches/impostors only).",
                                      leafCutoff - 1, leafCutoff);
                }

                // Show current cascade configuration
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Cascade config:");
                for (int c = 0; c < 4; c++) {
                    const char* mode;
                    ImVec4 color;
                    if (static_cast<uint32_t>(c) < shadowSettings.geometryCascadeCutoff) {
                        if (static_cast<uint32_t>(c) < shadowSettings.leafCascadeCutoff) {
                            mode = "full";
                            color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                        } else {
                            mode = "branches";
                            color = ImVec4(1.0f, 1.0f, 0.5f, 1.0f);
                        }
                    } else {
                        mode = "impostor";
                        color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(color, "[%d:%s]", c, mode);
                }
            }

            // GPU Branch Shadow Culling toggle
            if (treeRenderer && treeRenderer->isBranchShadowCullingAvailable()) {
                ImGui::Spacing();
                bool gpuCulling = treeRenderer->isBranchShadowCullingEnabled();
                if (ImGui::Checkbox("GPU Branch Shadow Culling", &gpuCulling)) {
                    treeRenderer->setBranchShadowCullingEnabled(gpuCulling);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Use GPU compute shader to cull branch shadows.\n"
                                      "Reduces draw calls from O(n) per tree to O(archetypes).\n"
                                      "Disable to use fallback per-tree rendering.");
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
                if (ImGui::Button("Preview Atlas")) {
                    ImGui::OpenPopup("AtlasPreview");
                }

                if (ImGui::BeginPopup("AtlasPreview")) {
                    ImGui::Text("Octahedral Impostor Atlas (8x8 grid, 256px cells)");
                    ImGui::Separator();

                    // Atlas selection dropdown
                    static int selectedArchetype = 0;
                    static int selectedTextureType = 0;
                    const char* textureTypes[] = { "Albedo", "Normal/Depth/AO" };

                    ImGui::SetNextItemWidth(200);
                    if (ImGui::BeginCombo("Archetype",
                            selectedArchetype < static_cast<int>(atlas->getArchetypeCount())
                                ? atlas->getArchetype(selectedArchetype)->name.c_str()
                                : "None")) {
                        for (size_t i = 0; i < atlas->getArchetypeCount(); i++) {
                            const auto* archetype = atlas->getArchetype(static_cast<uint32_t>(i));
                            if (archetype) {
                                bool isSelected = (selectedArchetype == static_cast<int>(i));
                                if (ImGui::Selectable(archetype->name.c_str(), isSelected)) {
                                    selectedArchetype = static_cast<int>(i);
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150);
                    ImGui::Combo("Type", &selectedTextureType, textureTypes, IM_ARRAYSIZE(textureTypes));

                    // Clamp selection to valid range
                    if (selectedArchetype >= static_cast<int>(atlas->getArchetypeCount())) {
                        selectedArchetype = 0;
                    }

                    // Get the appropriate descriptor set
                    VkDescriptorSet previewSet = VK_NULL_HANDLE;
                    if (selectedTextureType == 0) {
                        previewSet = atlas->getPreviewDescriptorSet(static_cast<uint32_t>(selectedArchetype));
                    } else {
                        previewSet = atlas->getNormalPreviewDescriptorSet(static_cast<uint32_t>(selectedArchetype));
                    }

                    if (previewSet != VK_NULL_HANDLE) {
                        ImGui::Spacing();

                        // Draw atlas with grid overlay
                        float scale = 0.4f;
                        ImVec2 imageSize(OctahedralAtlasConfig::ATLAS_WIDTH * scale,
                                        OctahedralAtlasConfig::ATLAS_HEIGHT * scale);

                        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                        ImGui::Image(reinterpret_cast<ImTextureID>(previewSet), imageSize);

                        // Draw grid lines
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        float cellSize = imageSize.x / OctahedralAtlasConfig::GRID_SIZE;
                        ImU32 gridColor = IM_COL32(255, 255, 255, 80);

                        // Vertical lines
                        for (int x = 0; x <= OctahedralAtlasConfig::GRID_SIZE; x++) {
                            float px = cursorPos.x + x * cellSize;
                            drawList->AddLine(ImVec2(px, cursorPos.y),
                                            ImVec2(px, cursorPos.y + imageSize.y), gridColor);
                        }
                        // Horizontal lines
                        for (int y = 0; y <= OctahedralAtlasConfig::GRID_SIZE; y++) {
                            float py = cursorPos.y + y * cellSize;
                            drawList->AddLine(ImVec2(cursorPos.x, py),
                                            ImVec2(cursorPos.x + imageSize.x, py), gridColor);
                        }

                        ImGui::Spacing();
                        ImGui::Text("Hemi-octahedral mapping: 64 views (8x8)");
                        ImGui::Text("UV encodes view direction on upper hemisphere");
                    } else {
                        ImGui::Text("No preview available for this selection");
                    }

                    ImGui::EndPopup();
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
