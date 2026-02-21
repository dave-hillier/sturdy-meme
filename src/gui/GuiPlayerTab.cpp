#include "GuiPlayerTab.h"
#include "core/interfaces/IPlayerControl.h"
#include "SceneBuilder.h"
#include "Camera.h"
#include "animation/AnimatedCharacter.h"
#include "animation/MotionMatchingController.h"
#include "npc/NPCSimulation.h"
#include "npc/NPCData.h"
#include "GLTFLoader.h"  // For Skeleton

#include <imgui.h>
#include <cmath>

void GuiPlayerTab::renderCape(PlayerSettings& settings) {
    ImGui::Checkbox("Enable Cape", &settings.capeEnabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle cape visibility and simulation");
    }

    ImGui::Checkbox("Show Cape Colliders", &settings.showCapeColliders);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visualize body colliders used for cape collision");
    }
}

void GuiPlayerTab::renderWeapons(PlayerSettings& settings) {
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
}

void GuiPlayerTab::renderCharacterLOD(IPlayerControl& playerControl, PlayerSettings& settings) {
    auto& sceneBuilder = playerControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        ImGui::TextDisabled("No animated character loaded");
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();
    uint32_t currentLOD = character.getLODLevel();

    const char* lodNames[] = {"LOD0 (High)", "LOD1 (Medium)", "LOD2 (Low)", "LOD3 (Distant)"};
    ImVec4 lodColors[] = {
        ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
        ImVec4(0.8f, 0.8f, 0.2f, 1.0f),
        ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
        ImVec4(1.0f, 0.2f, 0.2f, 1.0f)
    };

    ImGui::Text("Current LOD:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, lodColors[currentLOD]);
    ImGui::Text("%s", lodNames[currentLOD]);
    ImGui::PopStyleColor();

    uint32_t activeBones = character.getActiveBoneCount();
    uint32_t totalBones = character.getTotalBoneCount();
    ImGui::Text("Active Bones: %u / %u", activeBones, totalBones);
    if (activeBones < totalBones) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(-%u%%)",
            100 - (activeBones * 100 / totalBones));
    }

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

    ImGui::Checkbox("Force LOD Level", &settings.forceLODLevel);
    int forcedLOD = static_cast<int>(settings.forcedLOD);
    if (ImGui::SliderInt("Forced LOD", &forcedLOD, 0, 3, lodNames[forcedLOD])) {
        settings.forcedLOD = static_cast<uint32_t>(forcedLOD);
    }

    if (settings.forceLODLevel) {
        character.setLODLevel(settings.forcedLOD);
        bool shouldSkip = (settings.forcedLOD >= 2);
        character.setSkipAnimationUpdate(shouldSkip);
        if (shouldSkip) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(anim frozen)");
        }
    } else {
        character.setSkipAnimationUpdate(false);
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Test: Move character then force LOD2/3 to see animation freeze");
}

void GuiPlayerTab::renderCapeInfo() {
    ImGui::BulletText("Cloth simulation: Verlet integration");
    ImGui::BulletText("Body colliders: Spheres + Capsules");
    ImGui::BulletText("Attachments: Shoulders + Upper back");
}

void GuiPlayerTab::renderNPCLOD(IPlayerControl& playerControl) {
    auto& sceneBuilder = playerControl.getSceneBuilder();
    auto* npcSim = sceneBuilder.getNPCSimulation();
    if (!npcSim) {
        ImGui::TextDisabled("No NPC simulation active");
        return;
    }

    const auto& npcData = npcSim->getData();
    size_t npcCount = npcData.count();

    if (npcCount == 0) {
        ImGui::TextDisabled("No NPCs in scene");
        return;
    }

    uint32_t virtualCount = 0, bulkCount = 0, realCount = 0;
    for (size_t i = 0; i < npcCount; ++i) {
        switch (npcData.lodLevels[i]) {
            case NPCLODLevel::Virtual: virtualCount++; break;
            case NPCLODLevel::Bulk: bulkCount++; break;
            case NPCLODLevel::Real: realCount++; break;
        }
    }

    ImVec4 colorReal(0.2f, 1.0f, 0.2f, 1.0f);
    ImVec4 colorBulk(1.0f, 0.8f, 0.2f, 1.0f);
    ImVec4 colorVirtual(1.0f, 0.3f, 0.3f, 1.0f);

    ImGui::Text("Total NPCs: %zu", npcCount);

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

void GuiPlayerTab::renderMotionMatching(IPlayerControl& playerControl, PlayerSettings& settings) {
    auto& sceneBuilder = playerControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        ImGui::TextDisabled("No animated character loaded");
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();

    bool wasEnabled = character.isUsingMotionMatching();
    if (ImGui::Checkbox("Enable Motion Matching", &settings.motionMatchingEnabled)) {
        if (settings.motionMatchingEnabled && !wasEnabled) {
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

    // Auto-initialize motion matching if enabled by default but not yet active
    if (settings.motionMatchingEnabled && !character.isUsingMotionMatching()) {
        if (!character.getMotionMatchingController().isDatabaseBuilt()) {
            character.initializeMotionMatching();
        } else {
            character.setUseMotionMatching(true);
        }
    }

    // Sync the checkbox with actual state
    settings.motionMatchingEnabled = character.isUsingMotionMatching();

    bool mmActive = character.isUsingMotionMatching();

    // Facing mode
    const char* facingModeItems[] = { "Follow Movement", "Follow Camera", "Follow Target" };
    int currentFacingMode = static_cast<int>(settings.facingMode);
    FacingMode prevMode = settings.facingMode;
    if (ImGui::Combo("Facing Mode", &currentFacingMode, facingModeItems, IM_ARRAYSIZE(facingModeItems))) {
        settings.facingMode = static_cast<FacingMode>(currentFacingMode);
        if (mmActive) {
            auto& controller = const_cast<MotionMatching::MotionMatchingController&>(
                character.getMotionMatchingController());
            bool isStrafeMode = (settings.facingMode != FacingMode::FollowMovement);
            controller.setStrafeMode(isStrafeMode);
        }
        if (prevMode == FacingMode::FollowTarget && settings.facingMode != FacingMode::FollowTarget) {
            settings.hasTarget = false;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Follow Movement: Character turns to face movement direction\n"
                          "Follow Camera: Character faces camera (strafe mode)\n"
                          "Follow Target: Character faces a target position (lock-on)\n\n"
                          "Quick toggle: CapsLock or B button (gamepad)\n"
                          "Hold: Middle mouse or Left Trigger");
    }

    if (settings.facingMode == FacingMode::FollowTarget) {
        if (settings.hasTarget) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "Target: (%.1f, %.1f, %.1f)",
                settings.targetPosition.x, settings.targetPosition.y, settings.targetPosition.z);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Target will be placed 5m ahead");
        }
    }

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tab: Toggle 3rd Person Camera");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "P: Toggle Orbit Camera");

    if (settings.facingMode == FacingMode::FollowCamera) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "FOLLOW CAMERA ACTIVE");
    } else if (settings.facingMode == FacingMode::FollowTarget && settings.hasTarget) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "FOLLOW TARGET ACTIVE");
    }

    ImGui::Separator();
    ImGui::Spacing();

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

    const auto& stats = character.getMotionMatchingStats();

    ImGui::Text("Current Clip:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%s", stats.currentClipName.c_str());

    ImGui::Text("Clip Time: %.2fs", stats.currentClipTime);

    float costThreshold = 2.0f;
    ImVec4 costColor = stats.lastMatchCost < costThreshold ?
                       ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                       ImVec4(1.0f, 0.5f, 0.2f, 1.0f);

    ImGui::Text("Match Cost:");
    ImGui::SameLine();
    ImGui::TextColored(costColor, "%.3f", stats.lastMatchCost);

    ImGui::Text("Trajectory: %.3f", stats.lastTrajectoryCost);
    ImGui::Text("Pose: %.3f", stats.lastPoseCost);
    ImGui::Text("Heading: %.3f", stats.lastHeadingCost);
    ImGui::Text("Bias: %.3f", stats.lastBiasCost);
    ImGui::Text("Matches/sec: %zu", stats.matchesThisSecond);
    ImGui::Text("Database poses: %zu", stats.posesSearched);

    const auto& controller = character.getMotionMatchingController();
    if (controller.isDatabaseBuilt()) {
        const auto& db = controller.getDatabase();
        ImGui::Text("Database: %zu poses from %zu clips",
                   db.getPoseCount(), db.getClipCount());
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
    // Vulkan projection already flips Y (proj[1][1] *= -1), so NDC Y is already
    // in screen orientation. Just map to pixels.
    float screenY = (ndc.y * 0.5f + 0.5f) * height;
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
    const Renderable* playerRenderable = sceneBuilder.getRenderableForEntity(sceneBuilder.getPlayerEntity());
    if (!playerRenderable) {
        return;
    }

    glm::mat4 worldTransform = playerRenderable->transform;

    // Get skeleton for computing actual bone positions
    const auto& skeleton = character.getSkeleton();
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Find a foot bone to anchor the visualization at actual ground level
    int32_t footIdx = skeleton.findJointIndex("LeftFoot");
    if (footIdx < 0) footIdx = skeleton.findJointIndex("mixamorig:LeftFoot");
    if (footIdx < 0) footIdx = skeleton.findJointIndex("RightFoot");
    if (footIdx < 0) footIdx = skeleton.findJointIndex("mixamorig:RightFoot");

    // Compute ground position: foot world position with Y as ground level
    glm::vec3 groundPos = glm::vec3(worldTransform[3]);  // Default to transform origin
    if (footIdx >= 0 && static_cast<size_t>(footIdx) < globalTransforms.size()) {
        // Get foot world position
        glm::vec4 footWorld = worldTransform * globalTransforms[footIdx] * glm::vec4(0, 0, 0, 1);
        // Use transform XZ but foot Y (which is at ground level)
        groundPos = glm::vec3(worldTransform[3].x, footWorld.y, worldTransform[3].z);
    }

    // Get ImGui background draw list for overlay rendering
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Draw trajectory visualization
    if (settings.showMotionMatchingTrajectory) {
        // Query trajectory (predicted from input)
        const auto& queryTrajectory = controller.getQueryTrajectory();

        // Matched trajectory (from database)
        const auto& matchedTrajectory = controller.getLastMatchedTrajectory();

        // Use ground position as anchor (where the feet actually are)
        glm::vec3 charOrigin = groundPos;

        // Draw query trajectory (cyan - predicted)
        ImU32 queryColor = IM_COL32(0, 200, 255, 200);
        ImU32 queryPointColor = IM_COL32(0, 255, 255, 255);

        ImVec2 prevScreen = worldToScreen(charOrigin, viewProj, width, height);
        for (size_t i = 0; i < queryTrajectory.sampleCount; ++i) {
            const auto& sample = queryTrajectory.samples[i];

            // Trajectory positions are world-space offsets from current position
            // (predictor works in world space), so just add directly
            glm::vec3 worldPos = groundPos + sample.position;
            ImVec2 screenPos = worldToScreen(worldPos, viewProj, width, height);

            // Draw line from previous point
            if (i > 0 && prevScreen.x > -500 && screenPos.x > -500) {
                drawList->AddLine(prevScreen, screenPos, queryColor, 2.0f);
            }

            // Draw point
            if (screenPos.x > -500) {
                float radius = sample.timeOffset >= 0 ? 5.0f : 3.0f;  // Future points larger
                drawList->AddCircleFilled(screenPos, radius, queryPointColor);

                // Draw facing direction (already in world space)
                glm::vec3 facingEnd = worldPos + sample.facing * 0.3f;
                ImVec2 facingScreenPos = worldToScreen(facingEnd, viewProj, width, height);
                if (facingScreenPos.x > -500) {
                    drawList->AddLine(screenPos, facingScreenPos, IM_COL32(0, 150, 200, 150), 1.0f);
                }
            }

            prevScreen = screenPos;
        }

        // Draw matched trajectory (green - from database)
        // Database trajectory is in character-local space (forward = Z+)
        // Transform to world space using character's facing direction
        ImU32 matchColor = IM_COL32(100, 255, 100, 150);
        ImU32 matchPointColor = IM_COL32(0, 255, 0, 200);

        // Get character facing from world transform (Z axis)
        glm::vec3 charFacing = glm::normalize(glm::vec3(worldTransform[2]));
        charFacing.y = 0.0f;
        if (glm::length(charFacing) > 0.01f) {
            charFacing = glm::normalize(charFacing);
        } else {
            charFacing = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // Rotation angle: local Z+ -> world facing direction
        float matchAngle = std::atan2(charFacing.x, charFacing.z);
        float matchCosA = std::cos(matchAngle);
        float matchSinA = std::sin(matchAngle);

        prevScreen = worldToScreen(charOrigin, viewProj, width, height);
        for (size_t i = 0; i < matchedTrajectory.sampleCount; ++i) {
            const auto& sample = matchedTrajectory.samples[i];

            // Transform from local space to world space (Y-axis rotation)
            glm::vec3 localPos = sample.position;
            glm::vec3 worldOffset;
            worldOffset.x = localPos.x * matchCosA + localPos.z * matchSinA;
            worldOffset.y = localPos.y;
            worldOffset.z = -localPos.x * matchSinA + localPos.z * matchCosA;

            glm::vec3 worldPos = groundPos + worldOffset;
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

        // Use ground position as anchor
        glm::vec3 charOrigin = groundPos;

        // Get the current matched pose features
        if (playback.matchedPoseIndex < db.getPoseCount()) {
            const auto& matchedPose = db.getPose(playback.matchedPoseIndex);
            const auto& features = matchedPose.poseFeatures;

            ImU32 featureColor = IM_COL32(255, 150, 0, 200);

            for (size_t i = 0; i < features.boneCount; ++i) {
                // Transform bone position from character space to world space
                glm::vec3 rotatedPos = glm::mat3(worldTransform) * features.boneFeatures[i].position;
                glm::vec3 boneWorldPos = groundPos + rotatedPos;
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

                    // Draw velocity vector (transform as direction)
                    glm::vec3 velWorld = glm::mat3(worldTransform) * features.boneFeatures[i].velocity;
                    glm::vec3 velEnd = boneWorldPos + velWorld * 0.1f;
                    ImVec2 velScreenPos = worldToScreen(velEnd, viewProj, width, height);
                    if (velScreenPos.x > -500) {
                        drawList->AddLine(screenPos, velScreenPos, IM_COL32(255, 200, 0, 150), 1.5f);
                    }
                }
            }

            // Draw root velocity (transform as direction)
            glm::vec3 rootVelWorld = glm::mat3(worldTransform) * features.rootVelocity;
            glm::vec3 rootVelEnd = charOrigin + rootVelWorld * 0.2f;
            ImVec2 charScreen = worldToScreen(charOrigin, viewProj, width, height);
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
