#include "GuiDebugTab.h"
#include "Renderer.h"
#include "DebugLineSystem.h"
#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <imgui.h>

void GuiDebugTab::render(Renderer& renderer) {
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("DEBUG VISUALIZATIONS");
    ImGui::PopStyleColor();

    bool cascadeDebug = renderer.isShowingCascadeDebug();
    if (ImGui::Checkbox("Shadow Cascade Debug", &cascadeDebug)) {
        renderer.toggleCascadeDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows colored overlay for each shadow cascade");
    }

    bool snowDepthDebug = renderer.isShowingSnowDepthDebug();
    if (ImGui::Checkbox("Snow Depth Debug", &snowDepthDebug)) {
        renderer.toggleSnowDepthDebug();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows snow accumulation depth as heat map");
    }

#ifdef JPH_DEBUG_RENDERER
    ImGui::Spacing();

    bool physicsDebug = renderer.isPhysicsDebugEnabled();
    if (ImGui::Checkbox("Physics Debug", &physicsDebug)) {
        renderer.setPhysicsDebugEnabled(physicsDebug);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw Jolt Physics collision shapes and debug info");
    }

    if (physicsDebug) {
        ImGui::Indent();

        auto* debugRenderer = renderer.getPhysicsDebugRenderer();
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
        } else {
            ImGui::TextDisabled("Enable to see options");
        }

        ImGui::Unindent();
    }

    // Show stats
    auto& debugLines = renderer.getDebugLineSystem();
    if (physicsDebug) {
        ImGui::Spacing();
        ImGui::Text("Lines: %zu", debugLines.getLineCount());
        ImGui::Text("Triangles: %zu", debugLines.getTriangleCount());
    }
#endif

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("OCCLUSION CULLING");
    ImGui::PopStyleColor();

    bool hiZEnabled = renderer.isHiZCullingEnabled();
    if (ImGui::Checkbox("Hi-Z Occlusion Culling", &hiZEnabled)) {
        renderer.setHiZCullingEnabled(hiZEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable hierarchical Z-buffer occlusion culling (8 key)");
    }

    // Display culling statistics
    auto stats = renderer.getHiZCullingStats();
    ImGui::Text("Total Objects: %u", stats.totalObjects);
    ImGui::Text("Visible: %u", stats.visibleObjects);
    ImGui::Text("Frustum Culled: %u", stats.frustumCulled);
    ImGui::Text("Occlusion Culled: %u", stats.occlusionCulled);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("SYSTEM INFO");
    ImGui::PopStyleColor();

    ImGui::Text("Renderer: Vulkan");
    ImGui::Text("Shadow Cascades: 4");
    ImGui::Text("Shadow Map Size: 2048");
    ImGui::Text("Max Frames in Flight: 2");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Keyboard shortcuts reference
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("KEYBOARD SHORTCUTS");
    ImGui::PopStyleColor();

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
}
