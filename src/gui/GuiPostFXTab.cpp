#include "GuiPostFXTab.h"
#include "core/interfaces/IPostProcessState.h"
#include "core/interfaces/ICloudShadowControl.h"
#include "HiZSystem.h"
#include "PostProcessSystem.h"

#include <imgui.h>

void GuiPostFXTab::renderHDRPipeline(IPostProcessState& postProcess) {
    bool hdrPassEnabled = postProcess.isHDRPassEnabled();
    if (ImGui::Checkbox("HDR Pass (Scene Rendering)", &hdrPassEnabled)) {
        postProcess.setHDRPassEnabled(hdrPassEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable entire HDR scene rendering pass (for performance debugging)");
    }

    bool hdrEnabled = postProcess.isHDREnabled();
    if (ImGui::Checkbox("HDR Tonemapping", &hdrEnabled)) {
        postProcess.setHDREnabled(hdrEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable ACES tonemapping and exposure control");
    }
}

void GuiPostFXTab::renderCloudShadows(ICloudShadowControl& cloudShadow) {
    bool cloudShadowEnabled = cloudShadow.isEnabled();
    if (ImGui::Checkbox("Cloud Shadows", &cloudShadowEnabled)) {
        cloudShadow.setEnabled(cloudShadowEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable cloud shadow projection on terrain");
    }

    float cloudShadowIntensity = cloudShadow.getShadowIntensity();
    if (ImGui::SliderFloat("Shadow Intensity", &cloudShadowIntensity, 0.0f, 1.0f)) {
        cloudShadow.setShadowIntensity(cloudShadowIntensity);
    }
}

void GuiPostFXTab::renderBloom(IPostProcessState& postProcess) {
    bool bloomEnabled = postProcess.isBloomEnabled();
    if (ImGui::Checkbox("Enable Bloom", &bloomEnabled)) {
        postProcess.setBloomEnabled(bloomEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable bloom glow effect");
    }
}

void GuiPostFXTab::renderGodRays(IPostProcessState& postProcess) {
    bool godRaysEnabled = postProcess.isGodRaysEnabled();
    if (ImGui::Checkbox("Enable God Rays", &godRaysEnabled)) {
        postProcess.setGodRaysEnabled(godRaysEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle god ray light shafts effect");
    }

    const char* qualityNames[] = {"Low (16 samples)", "Medium (32 samples)", "High (64 samples)"};
    int currentQuality = static_cast<int>(postProcess.getGodRayQuality());
    if (ImGui::Combo("God Ray Quality", &currentQuality, qualityNames, 3)) {
        postProcess.setGodRayQuality(currentQuality);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher quality = more samples = better rays but slower");
    }
}

void GuiPostFXTab::renderVolumetricFogSettings(IPostProcessState& postProcess) {
    bool froxelHighQuality = postProcess.isFroxelFilterHighQuality();
    if (ImGui::Checkbox("High Quality Fog Filter", &froxelHighQuality)) {
        postProcess.setFroxelFilterQuality(froxelHighQuality);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Tricubic filtering (8 samples) vs Trilinear (1 sample)");
    }

    const char* debugModeNames[] = {
        "Normal",
        "Depth Slices",
        "Density",
        "Transmittance",
        "Grid Cells",
        "Volume Raymarch",
        "Cross-Section"
    };
    int currentDebugMode = postProcess.getFroxelDebugMode();
    if (ImGui::Combo("Debug View", &currentDebugMode, debugModeNames, 7)) {
        postProcess.setFroxelDebugMode(currentDebugMode);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Debug visualization modes:\n"
            "- Normal: Standard fog rendering\n"
            "- Depth Slices: Rainbow gradient showing Z distribution\n"
            "- Density: Grayscale fog density (high = red)\n"
            "- Transmittance: Light penetration (dark = blocked)\n"
            "- Grid Cells: Show froxel cell boundaries\n"
            "- Volume Raymarch: 3D accumulation through entire volume\n"
            "- Cross-Section: XY density at current depth"
        );
    }
}

void GuiPostFXTab::renderLocalToneMapping(IPostProcessState& postProcess) {
    bool localToneMapEnabled = postProcess.isLocalToneMapEnabled();
    if (ImGui::Checkbox("Enable Local Tone Mapping", &localToneMapEnabled)) {
        postProcess.setLocalToneMapEnabled(localToneMapEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Ghost of Tsushima bilateral grid technique for detail-preserving contrast");
    }

    float contrast = postProcess.getLocalToneMapContrast();
    if (ImGui::SliderFloat("Contrast Reduction", &contrast, 0.0f, 1.0f)) {
        postProcess.setLocalToneMapContrast(contrast);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = no contrast reduction, 0.5 = typical, 1.0 = very flat");
    }

    float detail = postProcess.getLocalToneMapDetail();
    if (ImGui::SliderFloat("Detail Boost", &detail, 0.5f, 2.0f)) {
        postProcess.setLocalToneMapDetail(detail);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("1.0 = neutral, 1.5 = punchy, 2.0 = maximum detail");
    }

    float bilateralBlend = postProcess.getBilateralBlend();
    if (ImGui::SliderFloat("Bilateral Blend", &bilateralBlend, 0.0f, 1.0f)) {
        postProcess.setBilateralBlend(bilateralBlend);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("GOT used 40%% bilateral, 60%% gaussian for smooth gradients");
    }
}

void GuiPostFXTab::renderExposure(IPostProcessState& postProcess) {
    bool autoExposure = postProcess.isAutoExposureEnabled();
    if (ImGui::Checkbox("Auto Exposure", &autoExposure)) {
        postProcess.setAutoExposureEnabled(autoExposure);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable histogram-based auto-exposure");
    }

    ImGui::Text("Current: %.2f EV", postProcess.getCurrentExposure());

    float manualExposure = postProcess.getManualExposure();
    if (ImGui::SliderFloat("Manual Exposure", &manualExposure, -4.0f, 4.0f, "%.2f EV")) {
        postProcess.setManualExposure(manualExposure);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Manual exposure value in EV (-4 to +4)");
    }
}
