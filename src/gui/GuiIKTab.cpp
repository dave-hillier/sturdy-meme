#include "GuiIKTab.h"
#include "core/interfaces/ISceneControl.h"
#include "Camera.h"
#include "AnimatedCharacter.h"
#include "PlayerCape.h"
#include "SceneBuilder.h"

#include <imgui.h>
#include <glm/glm.hpp>

void GuiIKTab::render(ISceneControl& sceneControl, const Camera& camera, IKDebugSettings& settings) {
    ImGui::Spacing();

    // Check if character is loaded
    auto& sceneBuilder = sceneControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        ImGui::TextDisabled("No animated character loaded");
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();
    auto& ikSystem = character.getIKSystem();

    // Debug Visualization section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("DEBUG VISUALIZATION");
    ImGui::PopStyleColor();

    ImGui::Checkbox("Show Skeleton", &settings.showSkeleton);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw wireframe skeleton bones");
    }

    ImGui::Checkbox("Show IK Targets", &settings.showIKTargets);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw IK target positions and pole vectors");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Look-At IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("LOOK-AT IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Look-At", &settings.lookAtEnabled)) {
        ikSystem.setLookAtEnabled(settings.lookAtEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Character head/neck tracks a target");
    }

    if (settings.lookAtEnabled) {
        ImGui::Indent();

        const char* modeNames[] = { "Fixed Point", "Camera Position", "Mouse (Screen)" };
        int currentMode = static_cast<int>(settings.lookAtMode);
        if (ImGui::Combo("Target Mode", &currentMode, modeNames, 3)) {
            settings.lookAtMode = static_cast<IKDebugSettings::LookAtMode>(currentMode);
        }

        if (settings.lookAtMode == IKDebugSettings::LookAtMode::Fixed) {
            ImGui::DragFloat3("Target Position", &settings.fixedLookAtTarget.x, 0.1f);
        }

        // Update look-at target based on mode
        glm::vec3 lookTarget;
        switch (settings.lookAtMode) {
            case IKDebugSettings::LookAtMode::Fixed:
                lookTarget = settings.fixedLookAtTarget;
                break;
            case IKDebugSettings::LookAtMode::Camera:
                lookTarget = camera.getPosition();
                break;
            case IKDebugSettings::LookAtMode::Mouse:
                // Project mouse into world (simplified - uses camera front direction)
                lookTarget = camera.getPosition() + camera.getForward() * 5.0f;
                break;
        }
        ikSystem.setLookAtTarget(lookTarget);

        float lookAtWeight = 1.0f;
        if (ImGui::SliderFloat("Weight", &lookAtWeight, 0.0f, 1.0f)) {
            ikSystem.setLookAtWeight(lookAtWeight);
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Foot Placement IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("FOOT PLACEMENT IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Foot Placement", &settings.footPlacementEnabled)) {
        // Enable/disable both feet
        ikSystem.setFootPlacementEnabled("LeftFoot", settings.footPlacementEnabled);
        ikSystem.setFootPlacementEnabled("RightFoot", settings.footPlacementEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Feet adapt to terrain height");
    }

    if (settings.footPlacementEnabled) {
        ImGui::Indent();

        if (ImGui::SliderFloat("Ground Offset", &settings.groundOffset, -0.2f, 0.2f)) {
            // Ground offset adjustment
        }

        // Show foot placement debug info
        ImGui::TextDisabled("Left Foot: Active");
        ImGui::TextDisabled("Right Foot: Active");

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Straddle IK section
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.8f, 1.0f));
    ImGui::Text("STRADDLE IK");
    ImGui::PopStyleColor();

    if (ImGui::Checkbox("Enable Straddling", &settings.straddleEnabled)) {
        ikSystem.setStraddleEnabled(settings.straddleEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hip tilt when feet at different heights");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // IK Chain Info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("IK CHAINS");
    ImGui::PopStyleColor();

    // Show configured chains
    const auto& skeleton = character.getSkeleton();
    ImGui::Text("Skeleton Bones: %zu", skeleton.joints.size());

    // Two-bone chains
    if (auto* chain = ikSystem.getChain("LeftArm")) {
        ImGui::BulletText("Left Arm: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("RightArm")) {
        ImGui::BulletText("Right Arm: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("LeftLeg")) {
        ImGui::BulletText("Left Leg: %s", chain->enabled ? "Enabled" : "Disabled");
    }
    if (auto* chain = ikSystem.getChain("RightLeg")) {
        ImGui::BulletText("Right Leg: %s", chain->enabled ? "Enabled" : "Disabled");
    }
}

void GuiIKTab::renderSkeletonOverlay(ISceneControl& sceneControl, const Camera& camera, const IKDebugSettings& settings, bool showCapeColliders) {
    auto& sceneBuilder = sceneControl.getSceneBuilder();
    if (!sceneBuilder.hasCharacter()) {
        return;
    }

    auto& character = sceneBuilder.getAnimatedCharacter();

    // Get the character's world transform from the scene object
    const auto& sceneObjects = sceneBuilder.getRenderables();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    if (playerIndex >= sceneObjects.size()) {
        return;
    }

    glm::mat4 worldTransform = sceneObjects[playerIndex].transform;

    // Get viewport size
    float width = static_cast<float>(sceneControl.getWidth());
    float height = static_cast<float>(sceneControl.getHeight());

    // Get view-projection matrix
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();

    // Helper to project world position to screen
    auto worldToScreen = [&](const glm::vec3& worldPos) -> ImVec2 {
        glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
        if (clipPos.w <= 0.0f) {
            return ImVec2(-1000, -1000);  // Behind camera
        }
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        float screenX = (ndc.x * 0.5f + 0.5f) * width;
        // Vulkan projection already flips Y (proj[1][1] *= -1), so NDC Y is already
        // in screen orientation (negative = up, positive = down). Just map to pixels.
        float screenY = (ndc.y * 0.5f + 0.5f) * height;
        return ImVec2(screenX, screenY);
    };

    // Get the background draw list (renders behind everything)
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Draw skeleton if enabled
    if (settings.showSkeleton) {
        SkeletonDebugData skelData = character.getSkeletonDebugData(worldTransform);

        // Draw bones as lines
        for (const auto& bone : skelData.bones) {
            if (bone.parentIndex < 0) continue;  // Skip root

            ImVec2 startScreen = worldToScreen(bone.startPos);
            ImVec2 endScreen = worldToScreen(bone.endPos);

            // Skip if behind camera
            if (startScreen.x < -500 || endScreen.x < -500) continue;

            // Bone color - cyan for normal, yellow for end effectors
            ImU32 boneColor = bone.isEndEffector ?
                IM_COL32(255, 255, 100, 200) :
                IM_COL32(100, 255, 255, 200);

            drawList->AddLine(startScreen, endScreen, boneColor, 2.0f);
        }

        // Draw joint positions as circles
        for (const auto& pos : skelData.jointPositions) {
            ImVec2 screenPos = worldToScreen(pos);
            if (screenPos.x < -500) continue;

            drawList->AddCircleFilled(screenPos, 4.0f, IM_COL32(255, 100, 100, 255));
        }
    }

    // Draw IK targets if enabled
    if (settings.showIKTargets) {
        IKDebugData ikData = character.getIKDebugData();

        // Transform IK positions to world space and draw
        for (const auto& chain : ikData.chains) {
            if (!chain.active) continue;

            // Chain positions are already in skeleton local space
            glm::vec4 targetWorld = worldTransform * glm::vec4(chain.targetPos, 1.0f);
            glm::vec4 poleWorld = worldTransform * glm::vec4(chain.polePos, 1.0f);
            glm::vec4 endWorld = worldTransform * glm::vec4(chain.endPos, 1.0f);

            ImVec2 targetScreen = worldToScreen(glm::vec3(targetWorld));
            ImVec2 poleScreen = worldToScreen(glm::vec3(poleWorld));
            ImVec2 endScreen = worldToScreen(glm::vec3(endWorld));

            // Draw target as green cross
            if (targetScreen.x > -500) {
                drawList->AddLine(
                    ImVec2(targetScreen.x - 8, targetScreen.y),
                    ImVec2(targetScreen.x + 8, targetScreen.y),
                    IM_COL32(100, 255, 100, 255), 2.0f);
                drawList->AddLine(
                    ImVec2(targetScreen.x, targetScreen.y - 8),
                    ImVec2(targetScreen.x, targetScreen.y + 8),
                    IM_COL32(100, 255, 100, 255), 2.0f);
            }

            // Draw pole vector as blue diamond
            if (poleScreen.x > -500) {
                drawList->AddQuadFilled(
                    ImVec2(poleScreen.x, poleScreen.y - 6),
                    ImVec2(poleScreen.x + 6, poleScreen.y),
                    ImVec2(poleScreen.x, poleScreen.y + 6),
                    ImVec2(poleScreen.x - 6, poleScreen.y),
                    IM_COL32(100, 100, 255, 200));
            }

            // Draw line from end effector to target
            if (targetScreen.x > -500 && endScreen.x > -500) {
                drawList->AddLine(endScreen, targetScreen, IM_COL32(255, 255, 100, 150), 1.0f);
            }
        }

        // Draw look-at targets
        for (const auto& lookAt : ikData.lookAtTargets) {
            if (!lookAt.active) continue;

            glm::vec4 targetWorld = worldTransform * glm::vec4(lookAt.targetPos, 1.0f);
            glm::vec4 headWorld = worldTransform * glm::vec4(lookAt.headPos, 1.0f);

            ImVec2 targetScreen = worldToScreen(glm::vec3(targetWorld));
            ImVec2 headScreen = worldToScreen(glm::vec3(headWorld));

            // Draw target as magenta circle
            if (targetScreen.x > -500) {
                drawList->AddCircle(targetScreen, 10.0f, IM_COL32(255, 100, 255, 255), 12, 2.0f);
            }

            // Draw line from head to target
            if (targetScreen.x > -500 && headScreen.x > -500) {
                drawList->AddLine(headScreen, targetScreen, IM_COL32(255, 100, 255, 150), 1.0f);
            }
        }

        // Draw foot placement targets
        for (const auto& foot : ikData.footPlacements) {
            if (!foot.active) continue;

            glm::vec4 groundWorld = worldTransform * glm::vec4(foot.groundPos, 1.0f);
            glm::vec4 footWorld = worldTransform * glm::vec4(foot.footPos, 1.0f);

            ImVec2 groundScreen = worldToScreen(glm::vec3(groundWorld));
            ImVec2 footScreen = worldToScreen(glm::vec3(footWorld));

            // Draw ground target as orange square
            if (groundScreen.x > -500) {
                drawList->AddRectFilled(
                    ImVec2(groundScreen.x - 5, groundScreen.y - 5),
                    ImVec2(groundScreen.x + 5, groundScreen.y + 5),
                    IM_COL32(255, 150, 50, 200));
            }

            // Draw line from foot to ground target
            if (groundScreen.x > -500 && footScreen.x > -500) {
                drawList->AddLine(footScreen, groundScreen, IM_COL32(255, 150, 50, 150), 1.0f);
            }
        }
    }

    // Draw cape colliders if enabled
    if (showCapeColliders) {
        auto& cape = sceneBuilder.getPlayerCape();
        if (cape.isInitialized()) {
            CapeDebugData capeData = cape.getDebugData();

            // Draw sphere colliders (orange circles)
            for (const auto& sphere : capeData.spheres) {
                ImVec2 centerScreen = worldToScreen(sphere.center);
                if (centerScreen.x < -500) continue;

                // Approximate screen radius based on depth
                glm::vec4 clipCenter = viewProj * glm::vec4(sphere.center, 1.0f);
                glm::vec4 clipEdge = viewProj * glm::vec4(sphere.center + glm::vec3(sphere.radius, 0, 0), 1.0f);
                float screenRadius = 20.0f;  // Default size
                if (clipCenter.w > 0.1f && clipEdge.w > 0.1f) {
                    float ndcRadius = std::abs((clipEdge.x / clipEdge.w) - (clipCenter.x / clipCenter.w));
                    screenRadius = ndcRadius * 0.5f * width;
                    screenRadius = glm::clamp(screenRadius, 5.0f, 100.0f);
                }

                drawList->AddCircle(centerScreen, screenRadius, IM_COL32(255, 150, 50, 200), 16, 2.0f);
            }

            // Draw capsule colliders (green lines with circles at ends)
            for (const auto& capsule : capeData.capsules) {
                ImVec2 p1Screen = worldToScreen(capsule.point1);
                ImVec2 p2Screen = worldToScreen(capsule.point2);

                if (p1Screen.x < -500 && p2Screen.x < -500) continue;

                // Approximate screen radius for capsule ends
                glm::vec4 clipP1 = viewProj * glm::vec4(capsule.point1, 1.0f);
                float screenRadius = 15.0f;
                if (clipP1.w > 0.1f) {
                    glm::vec4 clipEdge = viewProj * glm::vec4(capsule.point1 + glm::vec3(capsule.radius, 0, 0), 1.0f);
                    if (clipEdge.w > 0.1f) {
                        float ndcRadius = std::abs((clipEdge.x / clipEdge.w) - (clipP1.x / clipP1.w));
                        screenRadius = ndcRadius * 0.5f * width;
                        screenRadius = glm::clamp(screenRadius, 4.0f, 60.0f);
                    }
                }

                // Draw capsule axis line
                if (p1Screen.x > -500 && p2Screen.x > -500) {
                    drawList->AddLine(p1Screen, p2Screen, IM_COL32(100, 255, 100, 200), 3.0f);
                }

                // Draw circles at capsule ends
                if (p1Screen.x > -500) {
                    drawList->AddCircle(p1Screen, screenRadius, IM_COL32(100, 255, 100, 200), 12, 2.0f);
                }
                if (p2Screen.x > -500) {
                    drawList->AddCircle(p2Screen, screenRadius, IM_COL32(100, 255, 100, 200), 12, 2.0f);
                }
            }

            // Draw attachment points (cyan diamonds)
            for (const auto& attachPos : capeData.attachmentPoints) {
                ImVec2 screenPos = worldToScreen(attachPos);
                if (screenPos.x < -500) continue;

                drawList->AddQuadFilled(
                    ImVec2(screenPos.x, screenPos.y - 8),
                    ImVec2(screenPos.x + 8, screenPos.y),
                    ImVec2(screenPos.x, screenPos.y + 8),
                    ImVec2(screenPos.x - 8, screenPos.y),
                    IM_COL32(100, 255, 255, 220));
            }
        }
    }
}
