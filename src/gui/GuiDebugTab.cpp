#include "GuiDebugTab.h"
#include "core/interfaces/IDebugControl.h"
#include "DebugLineSystem.h"
#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <imgui.h>

void GuiDebugTab::renderVisualizations(IDebugControl& debugControl) {
    bool cascadeDebug = debugControl.isShowingCascadeDebug();
    if (ImGui::Checkbox("Shadow Cascade Debug", &cascadeDebug)) {
        debugControl.toggleCascadeDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows colored overlay for each shadow cascade");
    }

    bool snowDepthDebug = debugControl.isShowingSnowDepthDebug();
    if (ImGui::Checkbox("Snow Depth Debug", &snowDepthDebug)) {
        debugControl.toggleSnowDepthDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows snow accumulation depth as heat map");
    }

    bool roadRiverVis = debugControl.isRoadRiverVisualizationEnabled();
    if (ImGui::Checkbox("Road/River Visualization", &roadRiverVis)) {
        debugControl.setRoadRiverVisualizationEnabled(roadRiverVis);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows road and river paths with directional cones");
    }

    ImGui::BeginDisabled(!roadRiverVis);
    ImGui::Indent();

    bool showRoads = debugControl.isRoadVisualizationEnabled();
    if (ImGui::Checkbox("Show Roads", &showRoads)) {
        debugControl.setRoadVisualizationEnabled(showRoads);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show road paths as bidirectional orange cones");
    }

    bool showRivers = debugControl.isRiverVisualizationEnabled();
    if (ImGui::Checkbox("Show Rivers", &showRivers)) {
        debugControl.setRiverVisualizationEnabled(showRivers);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show river paths as blue cones pointing downstream");
    }

    ImGui::Unindent();
    ImGui::EndDisabled();
}

void GuiDebugTab::renderPhysicsDebug(IDebugControl& debugControl) {
#ifdef JPH_DEBUG_RENDERER
    bool physicsDebug = debugControl.isPhysicsDebugEnabled();
    if (ImGui::Checkbox("Physics Debug", &physicsDebug)) {
        debugControl.setPhysicsDebugEnabled(physicsDebug);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw Jolt Physics collision shapes and debug info");
    }

    ImGui::BeginDisabled(!physicsDebug);
    auto* debugRenderer = debugControl.getPhysicsDebugRenderer();
    if (debugRenderer) {
        auto& options = debugRenderer->getOptions();

        ImGui::Checkbox("Draw Shapes", &options.drawShapes);
        ImGui::Checkbox("Wireframe", &options.drawShapeWireframe);
        ImGui::Checkbox("Bounding Boxes", &options.drawBoundingBox);
        ImGui::Checkbox("Velocity", &options.drawVelocity);
        ImGui::Checkbox("Center of Mass", &options.drawCenterOfMassTransform);

        ImGui::Spacing();
        ImGui::Text("Body Types:");
        ImGui::Checkbox("Static", &options.drawStaticBodies);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Warning: Static bodies include terrain heightfields which are very slow to render");
        }
        ImGui::Checkbox("Dynamic", &options.drawDynamicBodies);
        ImGui::Checkbox("Kinematic", &options.drawKinematicBodies);
        ImGui::Checkbox("Character", &options.drawCharacter);
    }

    // Show stats
    auto& debugLines = debugControl.getDebugLineSystem();
    ImGui::Spacing();
    ImGui::Text("Lines: %zu", debugLines.getLineCount());
    ImGui::Text("Triangles: %zu", debugLines.getTriangleCount());

    // Ragdoll spawning
    ImGui::Spacing();
    if (ImGui::Button("Spawn Ragdoll")) {
        debugControl.spawnRagdoll();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drop an articulated ragdoll from 5m above player (also: R key)");
    }
    int ragdollCount = debugControl.getActiveRagdollCount();
    if (ragdollCount > 0) {
        ImGui::SameLine();
        ImGui::Text("Active: %d", ragdollCount);
    }
    ImGui::EndDisabled();
#else
    (void)debugControl;
    ImGui::TextDisabled("Physics debug not available (JPH_DEBUG_RENDERER not defined)");
#endif
}

void GuiDebugTab::renderOcclusionCulling(IDebugControl& debugControl) {
    bool hiZEnabled = debugControl.isHiZCullingEnabled();
    if (ImGui::Checkbox("Hi-Z Occlusion Culling", &hiZEnabled)) {
        debugControl.setHiZCullingEnabled(hiZEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable hierarchical Z-buffer occlusion culling (8 key)");
    }

    auto stats = debugControl.getHiZCullingStats();
    ImGui::Text("Total Objects: %u", stats.totalObjects);
    ImGui::Text("Visible: %u", stats.visibleObjects);
    ImGui::Text("Frustum Culled: %u", stats.frustumCulled);
    ImGui::Text("Occlusion Culled: %u", stats.occlusionCulled);
}

void GuiDebugTab::renderSystemInfo() {
    ImGui::Text("Renderer: Vulkan");
    ImGui::Text("Shadow Cascades: 4");
    ImGui::Text("Shadow Map Size: 2048");
    ImGui::Text("Max Frames in Flight: 2");
}

void GuiDebugTab::renderKeyboardShortcuts() {
    ImGui::BulletText("F1 - Toggle GUI");
    ImGui::BulletText("F2 - Tree Editor");
    ImGui::BulletText("P - Place tree at camera");
    ImGui::BulletText("Tab - Toggle camera mode");
    ImGui::BulletText("1-4 - Time presets");
    ImGui::BulletText("+/- - Time scale");
    ImGui::BulletText("C - Cycle weather");
    ImGui::BulletText("Z/X - Weather intensity");
    ImGui::BulletText(",/. - Snow amount");
    ImGui::BulletText("T - Terrain wireframe");
    ImGui::BulletText("6 - Cascade debug");
    ImGui::BulletText("7 - Snow depth debug");
    ImGui::BulletText("8 - Hi-Z culling toggle");
    ImGui::BulletText("[ ] - Fog density");
    ImGui::BulletText("\\ - Toggle fog");
    ImGui::BulletText("F - Spawn confetti");
    ImGui::BulletText("R - Spawn ragdoll");
}
