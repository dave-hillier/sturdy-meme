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

    bool hdrPassEnabled = renderer.isHDRPassEnabled();
    if (ImGui::Checkbox("HDR Pass (Scene Rendering)", &hdrPassEnabled)) {
        renderer.setHDRPassEnabled(hdrPassEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable entire HDR scene rendering pass (for performance debugging)");
    }

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

    bool bloomEnabled = renderer.isBloomEnabled();
    if (ImGui::Checkbox("Enable Bloom", &bloomEnabled)) {
        renderer.setBloomEnabled(bloomEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable bloom glow effect");
    }

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

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.7f, 1.0f));
    ImGui::Text("LOCAL TONE MAPPING");
    ImGui::PopStyleColor();

    bool localToneMapEnabled = renderer.isLocalToneMapEnabled();
    if (ImGui::Checkbox("Enable Local Tone Mapping", &localToneMapEnabled)) {
        renderer.setLocalToneMapEnabled(localToneMapEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Ghost of Tsushima bilateral grid technique for detail-preserving contrast");
    }

    if (localToneMapEnabled) {
        float contrast = renderer.getLocalToneMapContrast();
        if (ImGui::SliderFloat("Contrast Reduction", &contrast, 0.0f, 1.0f)) {
            renderer.setLocalToneMapContrast(contrast);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no contrast reduction, 0.5 = typical, 1.0 = very flat");
        }

        float detail = renderer.getLocalToneMapDetail();
        if (ImGui::SliderFloat("Detail Boost", &detail, 0.5f, 2.0f)) {
            renderer.setLocalToneMapDetail(detail);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1.0 = neutral, 1.5 = punchy, 2.0 = maximum detail");
        }

        float bilateralBlend = renderer.getBilateralBlend();
        if (ImGui::SliderFloat("Bilateral Blend", &bilateralBlend, 0.0f, 1.0f)) {
            renderer.setBilateralBlend(bilateralBlend);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("GOT used 40%% bilateral, 60%% gaussian for smooth gradients");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("EXPOSURE");
    ImGui::PopStyleColor();

    bool autoExposure = renderer.isAutoExposureEnabled();
    if (ImGui::Checkbox("Auto Exposure", &autoExposure)) {
        renderer.setAutoExposureEnabled(autoExposure);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable histogram-based auto-exposure");
    }

    if (autoExposure) {
        ImGui::TextDisabled("Current: %.2f EV", renderer.getCurrentExposure());
    } else {
        float manualExposure = renderer.getManualExposure();
        if (ImGui::SliderFloat("Manual Exposure", &manualExposure, -4.0f, 4.0f, "%.2f EV")) {
            renderer.setManualExposure(manualExposure);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Manual exposure value in EV (-4 to +4)");
        }
    }
}
