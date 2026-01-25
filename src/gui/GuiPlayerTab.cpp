#include "GuiPlayerTab.h"
#include "core/interfaces/IPlayerControl.h"
#include "SceneBuilder.h"
#include "animation/AnimatedCharacter.h"
#include "npc/NPCSimulation.h"
#include "npc/NPCData.h"

#include <imgui.h>

void GuiPlayerTab::render(IPlayerControl& playerControl, PlayerSettings& settings) {
    ImGui::Spacing();

    auto& sceneBuilder = playerControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        ImGui::TextDisabled("No animated character loaded");
        return;
    }

    // Cape section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("CAPE");
    ImGui::PopStyleColor();

    ImGui::Checkbox("Enable Cape", &settings.capeEnabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle cape visibility and simulation");
    }

    ImGui::Checkbox("Show Cape Colliders", &settings.showCapeColliders);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visualize body colliders used for cape collision");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Weapons section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 0.9f, 1.0f));
    ImGui::Text("WEAPONS");
    ImGui::PopStyleColor();

    ImGui::Checkbox("Show Sword", &settings.showSword);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle sword visibility in right hand");
    }

    ImGui::Checkbox("Show Shield", &settings.showShield);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle shield visibility on left arm");
    }

    ImGui::Checkbox("Show Hand Axes", &settings.showWeaponAxes);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show RGB axis indicators on hand bones (R=X, G=Y, B=Z)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Character LOD section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("CHARACTER LOD");
    ImGui::PopStyleColor();

    auto& character = sceneBuilder.getAnimatedCharacter();
    {
        uint32_t currentLOD = character.getLODLevel();

        // LOD level display with color coding
        const char* lodNames[] = {"LOD0 (High)", "LOD1 (Medium)", "LOD2 (Low)", "LOD3 (Distant)"};
        ImVec4 lodColors[] = {
            ImVec4(0.2f, 1.0f, 0.2f, 1.0f),   // Green - high detail
            ImVec4(0.8f, 0.8f, 0.2f, 1.0f),   // Yellow - medium
            ImVec4(1.0f, 0.5f, 0.2f, 1.0f),   // Orange - low
            ImVec4(1.0f, 0.2f, 0.2f, 1.0f)    // Red - distant
        };

        ImGui::Text("Current LOD:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, lodColors[currentLOD]);
        ImGui::Text("%s", lodNames[currentLOD]);
        ImGui::PopStyleColor();

        // Bone count (skeleton LOD)
        uint32_t activeBones = character.getActiveBoneCount();
        uint32_t totalBones = character.getTotalBoneCount();
        ImGui::Text("Active Bones: %u / %u", activeBones, totalBones);
        if (activeBones < totalBones) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(-%u%%)",
                100 - (activeBones * 100 / totalBones));
        }

        // Animation skip status
        bool animSkipped = character.isAnimationUpdateSkipped();
        ImGui::Text("Animation Update:");
        ImGui::SameLine();
        if (animSkipped) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("SKIPPED (using cached)");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
            ImGui::Text("ACTIVE");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // Force LOD override
        ImGui::Checkbox("Force LOD Level", &settings.forceLODLevel);
        if (settings.forceLODLevel) {
            int forcedLOD = static_cast<int>(settings.forcedLOD);
            if (ImGui::SliderInt("Forced LOD", &forcedLOD, 0, 3, lodNames[forcedLOD])) {
                settings.forcedLOD = static_cast<uint32_t>(forcedLOD);
            }
            character.setLODLevel(settings.forcedLOD);

            // LOD2+ skips animation updates (every 2-4 frames in real system)
            // For testing, we skip entirely at LOD2+ to make the effect visible
            bool shouldSkip = (settings.forcedLOD >= 2);
            character.setSkipAnimationUpdate(shouldSkip);

            ImGui::SameLine();
            if (shouldSkip) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(anim frozen)");
            }
        } else {
            // When not forcing, ensure animation runs normally
            character.setSkipAnimationUpdate(false);
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Test: Move character then force LOD2/3 to see animation freeze");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cape info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("CAPE INFO");
    ImGui::PopStyleColor();

    ImGui::BulletText("Cloth simulation: Verlet integration");
    ImGui::BulletText("Body colliders: Spheres + Capsules");
    ImGui::BulletText("Attachments: Shoulders + Upper back");

    // NPC LOD section
    if (auto* npcSim = sceneBuilder.getNPCSimulation()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
        ImGui::Text("NPC LOD");
        ImGui::PopStyleColor();

        const auto& npcData = npcSim->getData();
        size_t npcCount = npcData.count();

        if (npcCount == 0) {
            ImGui::TextDisabled("No NPCs in scene");
        } else {
            // Count NPCs per LOD level
            uint32_t virtualCount = 0, bulkCount = 0, realCount = 0;
            for (size_t i = 0; i < npcCount; ++i) {
                switch (npcData.lodLevels[i]) {
                    case NPCLODLevel::Virtual: virtualCount++; break;
                    case NPCLODLevel::Bulk: bulkCount++; break;
                    case NPCLODLevel::Real: realCount++; break;
                }
            }

            // LOD colors
            ImVec4 colorReal(0.2f, 1.0f, 0.2f, 1.0f);     // Green
            ImVec4 colorBulk(1.0f, 0.8f, 0.2f, 1.0f);     // Yellow
            ImVec4 colorVirtual(1.0f, 0.3f, 0.3f, 1.0f);  // Red

            ImGui::Text("Total NPCs: %zu", npcCount);

            // Summary counts
            ImGui::TextColored(colorReal, "Real (<25m):");
            ImGui::SameLine();
            ImGui::Text("%u", realCount);
            ImGui::SameLine();
            ImGui::TextColored(colorBulk, "  Bulk (25-50m):");
            ImGui::SameLine();
            ImGui::Text("%u", bulkCount);
            ImGui::SameLine();
            ImGui::TextColored(colorVirtual, "  Virtual (>50m):");
            ImGui::SameLine();
            ImGui::Text("%u", virtualCount);

            ImGui::Spacing();

            // Per-NPC details (collapsible)
            if (ImGui::TreeNode("NPC Details")) {
                for (size_t i = 0; i < npcCount; ++i) {
                    const char* lodName = "Unknown";
                    ImVec4 lodColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                    switch (npcData.lodLevels[i]) {
                        case NPCLODLevel::Real:
                            lodName = "Real";
                            lodColor = colorReal;
                            break;
                        case NPCLODLevel::Bulk:
                            lodName = "Bulk";
                            lodColor = colorBulk;
                            break;
                        case NPCLODLevel::Virtual:
                            lodName = "Virtual";
                            lodColor = colorVirtual;
                            break;
                    }

                    ImGui::Text("NPC %zu:", i);
                    ImGui::SameLine();
                    ImGui::TextColored(lodColor, "%s", lodName);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(frames: %u)", npcData.framesSinceUpdate[i]);
                }
                ImGui::TreePop();
            }

            // LOD toggle
            bool lodEnabled = npcSim->isLODEnabled();
            if (ImGui::Checkbox("Enable NPC LOD", &lodEnabled)) {
                const_cast<NPCSimulation*>(npcSim)->setLODEnabled(lodEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Virtual: >50m, no render, update every ~10s\n"
                                  "Bulk: 25-50m, reduced updates ~1s\n"
                                  "Real: <25m, full animation every frame");
            }
        }
    }
}
