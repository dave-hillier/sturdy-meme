#include "GuiPostFXTab.h"
#include "Renderer.h"
#include "HiZSystem.h"
#include "PostProcessSystem.h"

#include <imgui.h>

void GuiPostFXTab::render(Renderer& renderer) {
    ImGui::Spacing();

    // HDR Tonemapping toggle
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
    ImGui::Text("HDR PIPELINE");
    ImGui::PopStyleColor();

    bool hdrEnabled = renderer.isHDREnabled();
    if (ImGui::Checkbox("HDR Tonemapping", &hdrEnabled)) {
        renderer.setHDREnabled(hdrEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable ACES tonemapping and exposure control");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cloud shadows toggle
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("CLOUD SHADOWS");
    ImGui::PopStyleColor();

    bool cloudShadowEnabled = renderer.isCloudShadowEnabled();
    if (ImGui::Checkbox("Cloud Shadows", &cloudShadowEnabled)) {
        renderer.setCloudShadowEnabled(cloudShadowEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable cloud shadow projection on terrain");
    }

    if (cloudShadowEnabled) {
        float cloudShadowIntensity = renderer.getCloudShadowIntensity();
        if (ImGui::SliderFloat("Shadow Intensity", &cloudShadowIntensity, 0.0f, 1.0f)) {
            renderer.setCloudShadowIntensity(cloudShadowIntensity);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
    ImGui::Text("BLOOM");
    ImGui::PopStyleColor();

    ImGui::TextDisabled("Bloom is enabled by default");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.6f, 1.0f));
    ImGui::Text("GOD RAYS");
    ImGui::PopStyleColor();

    bool godRaysEnabled = renderer.isGodRaysEnabled();
    if (ImGui::Checkbox("Enable God Rays", &godRaysEnabled)) {
        renderer.setGodRaysEnabled(godRaysEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle god ray light shafts effect");
    }

    if (godRaysEnabled) {
        // God ray quality dropdown
        const char* qualityNames[] = {"Low (16 samples)", "Medium (32 samples)", "High (64 samples)"};
        int currentQuality = static_cast<int>(renderer.getGodRayQuality());
        if (ImGui::Combo("God Ray Quality", &currentQuality, qualityNames, 3)) {
            renderer.setGodRayQuality(currentQuality);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Higher quality = more samples = better rays but slower");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
    ImGui::Text("VOLUMETRIC FOG");
    ImGui::PopStyleColor();

    bool froxelHighQuality = renderer.isFroxelFilterHighQuality();
    if (ImGui::Checkbox("High Quality Fog Filter", &froxelHighQuality)) {
        renderer.setFroxelFilterQuality(froxelHighQuality);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Tricubic filtering (8 samples) vs Trilinear (1 sample)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("EXPOSURE");
    ImGui::PopStyleColor();

    ImGui::TextDisabled("Auto-exposure is active");
    ImGui::TextDisabled("Histogram-based adaptation");
}
