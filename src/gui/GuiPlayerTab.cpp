#include "GuiPlayerTab.h"
#include "core/interfaces/IPlayerControl.h"
#include "SceneBuilder.h"
#include "Camera.h"
#include "animation/AnimatedCharacter.h"
#include "animation/MotionMatchingController.h"
#include "npc/NPCSimulation.h"
#include "npc/NPCData.h"
#include "PlayerState.h"  // For PlayerMovement::CAPSULE_HEIGHT

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

    // Motion Matching section
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("MOTION MATCHING");
    ImGui::PopStyleColor();

    // character already declared above, reuse it

    // Enable/disable motion matching
    bool wasEnabled = character.isUsingMotionMatching();
    if (ImGui::Checkbox("Enable Motion Matching", &settings.motionMatchingEnabled)) {
        if (settings.motionMatchingEnabled && !wasEnabled) {
            // Initialize motion matching if not already done
            if (!character.getMotionMatchingController().isDatabaseBuilt()) {
                character.initializeMotionMatching();
            } else {
                character.setUseMotionMatching(true);
            }
        } else if (!settings.motionMatchingEnabled && wasEnabled) {
            character.setUseMotionMatching(false);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use motion matching for animation selection\n"
                          "instead of state machine");
    }

    // Sync the checkbox with actual state
    settings.motionMatchingEnabled = character.isUsingMotionMatching();

    if (character.isUsingMotionMatching()) {
        ImGui::Indent();

        // Debug visualization options
        ImGui::Checkbox("Show Trajectory", &settings.showMotionMatchingTrajectory);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Visualize predicted (cyan) and matched (green) trajectories");
        }

        ImGui::Checkbox("Show Features", &settings.showMotionMatchingFeatures);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show feature bone positions used for matching");
        }

        ImGui::Checkbox("Show Stats", &settings.showMotionMatchingStats);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Display motion matching cost statistics");
        }

        ImGui::Spacing();

        // Motion matching statistics
        const auto& stats = character.getMotionMatchingStats();
        const auto& playback = character.getMotionMatchingController().getPlaybackState();

        ImGui::Text("Current Clip:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%s", stats.currentClipName.c_str());

        ImGui::Text("Clip Time: %.2fs", stats.currentClipTime);

        // Cost display with color coding
        float costThreshold = 2.0f;
        ImVec4 costColor = stats.lastMatchCost < costThreshold ?
                           ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                           ImVec4(1.0f, 0.5f, 0.2f, 1.0f);

        ImGui::Text("Match Cost:");
        ImGui::SameLine();
        ImGui::TextColored(costColor, "%.3f", stats.lastMatchCost);

        if (ImGui::TreeNode("Cost Breakdown")) {
            ImGui::Text("Trajectory: %.3f", stats.lastTrajectoryCost);
            ImGui::Text("Pose: %.3f", stats.lastPoseCost);
            ImGui::Text("Matches/sec: %zu", stats.matchesThisSecond);
            ImGui::Text("Database poses: %zu", stats.posesSearched);
            ImGui::TreePop();
        }

        ImGui::Unindent();
    } else {
        ImGui::TextDisabled("Enable to see motion matching options");

        // Show database info if available
        const auto& controller = character.getMotionMatchingController();
        if (controller.isDatabaseBuilt()) {
            const auto& db = controller.getDatabase();
            ImGui::Text("Database: %zu poses from %zu clips",
                       db.getPoseCount(), db.getClipCount());
        }
    }
}

// Helper to project world position to screen coordinates
static ImVec2 worldToScreen(const glm::vec3& worldPos,
                             const glm::mat4& viewProj,
                             float width, float height) {
    glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
    if (clipPos.w <= 0.0f) {
        return ImVec2(-1000, -1000);  // Behind camera
    }
    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    float screenX = (ndc.x * 0.5f + 0.5f) * width;
    float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * height;  // Flip Y for screen coords
    return ImVec2(screenX, screenY);
}

void GuiPlayerTab::renderMotionMatchingOverlay(IPlayerControl& playerControl, const Camera& camera,
                                                 const PlayerSettings& settings) {
    if (!settings.motionMatchingEnabled) {
        return;
    }

    auto& sceneBuilder = playerControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();
    if (!character.isUsingMotionMatching()) {
        return;
    }

    const auto& controller = character.getMotionMatchingController();
    if (!controller.isDatabaseBuilt()) {
        return;
    }

    // Get viewport size
    float width = static_cast<float>(playerControl.getWidth());
    float height = static_cast<float>(playerControl.getHeight());

    // Get view-projection matrix
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();

    // Get the character's world position
    const auto& sceneObjects = sceneBuilder.getRenderables();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    if (playerIndex >= sceneObjects.size()) {
        return;
    }

    glm::mat4 worldTransform = sceneObjects[playerIndex].transform;
    glm::vec3 characterPos = glm::vec3(worldTransform[3]);

    // The render transform includes a vertical offset (CAPSULE_HEIGHT * 0.5) for the character model.
    // Motion matching trajectory/features are at ground level, so subtract this offset.
    characterPos.y -= PlayerMovement::CAPSULE_HEIGHT * 0.5f;

    // Get ImGui background draw list for overlay rendering
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Draw trajectory visualization
    if (settings.showMotionMatchingTrajectory) {
        // Query trajectory (predicted from input)
        const auto& queryTrajectory = controller.getQueryTrajectory();

        // Matched trajectory (from database)
        const auto& matchedTrajectory = controller.getLastMatchedTrajectory();

        // Draw query trajectory (cyan - predicted)
        ImU32 queryColor = IM_COL32(0, 200, 255, 200);
        ImU32 queryPointColor = IM_COL32(0, 255, 255, 255);

        ImVec2 prevScreen = worldToScreen(characterPos, viewProj, width, height);
        for (size_t i = 0; i < queryTrajectory.sampleCount; ++i) {
            const auto& sample = queryTrajectory.samples[i];

            // Sample position is relative to character
            glm::vec3 worldPos = characterPos + sample.position;
            ImVec2 screenPos = worldToScreen(worldPos, viewProj, width, height);

            // Draw line from previous point
            if (i > 0 && prevScreen.x > -500 && screenPos.x > -500) {
                drawList->AddLine(prevScreen, screenPos, queryColor, 2.0f);
            }

            // Draw point
            if (screenPos.x > -500) {
                float radius = sample.timeOffset >= 0 ? 5.0f : 3.0f;  // Future points larger
                drawList->AddCircleFilled(screenPos, radius, queryPointColor);

                // Draw facing direction
                glm::vec3 facingEnd = worldPos + sample.facing * 0.3f;
                ImVec2 facingScreenPos = worldToScreen(facingEnd, viewProj, width, height);
                if (facingScreenPos.x > -500) {
                    drawList->AddLine(screenPos, facingScreenPos, IM_COL32(0, 150, 200, 150), 1.0f);
                }
            }

            prevScreen = screenPos;
        }

        // Draw matched trajectory (green - from database)
        ImU32 matchColor = IM_COL32(100, 255, 100, 150);
        ImU32 matchPointColor = IM_COL32(0, 255, 0, 200);

        prevScreen = worldToScreen(characterPos, viewProj, width, height);
        for (size_t i = 0; i < matchedTrajectory.sampleCount; ++i) {
            const auto& sample = matchedTrajectory.samples[i];

            glm::vec3 worldPos = characterPos + sample.position;
            ImVec2 screenPos = worldToScreen(worldPos, viewProj, width, height);

            // Draw line (offset slightly for visibility)
            if (i > 0 && prevScreen.x > -500 && screenPos.x > -500) {
                drawList->AddLine(
                    ImVec2(prevScreen.x + 2, prevScreen.y + 2),
                    ImVec2(screenPos.x + 2, screenPos.y + 2),
                    matchColor, 1.5f);
            }

            // Draw point
            if (screenPos.x > -500) {
                drawList->AddCircle(screenPos, 4.0f, matchPointColor, 8, 2.0f);
            }

            prevScreen = screenPos;
        }
    }

    // Draw feature bone positions
    if (settings.showMotionMatchingFeatures) {
        const auto& db = controller.getDatabase();
        const auto& playback = controller.getPlaybackState();

        // Get the current matched pose features
        if (playback.matchedPoseIndex < db.getPoseCount()) {
            const auto& matchedPose = db.getPose(playback.matchedPoseIndex);
            const auto& features = matchedPose.poseFeatures;

            ImU32 featureColor = IM_COL32(255, 150, 0, 200);

            for (size_t i = 0; i < features.boneCount; ++i) {
                glm::vec3 boneWorldPos = characterPos + features.boneFeatures[i].position;
                ImVec2 screenPos = worldToScreen(boneWorldPos, viewProj, width, height);

                if (screenPos.x > -500) {
                    // Draw diamond shape for feature bones
                    float size = 6.0f;
                    drawList->AddQuadFilled(
                        ImVec2(screenPos.x, screenPos.y - size),
                        ImVec2(screenPos.x + size, screenPos.y),
                        ImVec2(screenPos.x, screenPos.y + size),
                        ImVec2(screenPos.x - size, screenPos.y),
                        featureColor);

                    // Draw velocity vector
                    glm::vec3 velEnd = boneWorldPos + features.boneFeatures[i].velocity * 0.1f;
                    ImVec2 velScreenPos = worldToScreen(velEnd, viewProj, width, height);
                    if (velScreenPos.x > -500) {
                        drawList->AddLine(screenPos, velScreenPos, IM_COL32(255, 200, 0, 150), 1.5f);
                    }
                }
            }

            // Draw root velocity
            glm::vec3 rootVelEnd = characterPos + features.rootVelocity * 0.2f;
            ImVec2 charScreen = worldToScreen(characterPos, viewProj, width, height);
            ImVec2 velScreen = worldToScreen(rootVelEnd, viewProj, width, height);
            if (charScreen.x > -500 && velScreen.x > -500) {
                drawList->AddLine(charScreen, velScreen, IM_COL32(255, 255, 0, 255), 3.0f);
                drawList->AddCircleFilled(velScreen, 4.0f, IM_COL32(255, 255, 0, 255));
            }
        }
    }

    // Draw stats overlay in corner
    if (settings.showMotionMatchingStats) {
        const auto& stats = character.getMotionMatchingStats();

        ImVec2 statsPos(10, height - 120);
        ImU32 bgColor = IM_COL32(0, 0, 0, 180);
        ImU32 textColor = IM_COL32(255, 255, 255, 255);

        // Background
        drawList->AddRectFilled(
            ImVec2(statsPos.x - 5, statsPos.y - 5),
            ImVec2(statsPos.x + 200, statsPos.y + 105),
            bgColor, 5.0f);

        // Title
        drawList->AddText(statsPos, IM_COL32(100, 200, 255, 255), "Motion Matching");
        statsPos.y += 18;

        // Stats
        char buf[128];
        snprintf(buf, sizeof(buf), "Clip: %s", stats.currentClipName.c_str());
        drawList->AddText(statsPos, textColor, buf);
        statsPos.y += 16;

        snprintf(buf, sizeof(buf), "Time: %.2fs", stats.currentClipTime);
        drawList->AddText(statsPos, textColor, buf);
        statsPos.y += 16;

        ImU32 costColor = stats.lastMatchCost < 2.0f ?
                          IM_COL32(100, 255, 100, 255) :
                          IM_COL32(255, 150, 100, 255);
        snprintf(buf, sizeof(buf), "Cost: %.3f", stats.lastMatchCost);
        drawList->AddText(statsPos, costColor, buf);
        statsPos.y += 16;

        snprintf(buf, sizeof(buf), "Matches/s: %zu", stats.matchesThisSecond);
        drawList->AddText(statsPos, textColor, buf);
        statsPos.y += 16;

        snprintf(buf, sizeof(buf), "Poses: %zu", stats.posesSearched);
        drawList->AddText(statsPos, IM_COL32(150, 150, 150, 255), buf);
    }
}
