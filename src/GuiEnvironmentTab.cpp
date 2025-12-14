#include "GuiEnvironmentTab.h"
#include "Renderer.h"
#include "AtmosphereLUTSystem.h"

#include <imgui.h>
#include <glm/glm.hpp>

void GuiEnvironmentTab::render(Renderer& renderer, EnvironmentTabState& state) {
    ImGui::Spacing();

    // ========== FROXEL VOLUMETRIC FOG ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("FROXEL VOLUMETRIC FOG");
    ImGui::PopStyleColor();

    bool fogEnabled = renderer.isFogEnabled();
    if (ImGui::Checkbox("Enable Froxel Fog", &fogEnabled)) {
        renderer.setFogEnabled(fogEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Frustum-aligned voxel grid volumetric fog with temporal reprojection");
    }

    if (fogEnabled) {
        // Main fog parameters - wide ranges for extreme testing
        float fogDensity = renderer.getFogDensity();
        if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogDensity(fogDensity);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no fog, 1 = extremely dense (logarithmic scale)");
        }

        float fogAbsorption = renderer.getFogAbsorption();
        if (ImGui::SliderFloat("Absorption", &fogAbsorption, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogAbsorption(fogAbsorption);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Light absorption coefficient (0 = transparent, 1 = opaque fog)");
        }

        float fogBaseHeight = renderer.getFogBaseHeight();
        if (ImGui::SliderFloat("Base Height", &fogBaseHeight, -500.0f, 500.0f, "%.1f")) {
            renderer.setFogBaseHeight(fogBaseHeight);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Height where fog density is maximum");
        }

        float fogScaleHeight = renderer.getFogScaleHeight();
        if (ImGui::SliderFloat("Scale Height", &fogScaleHeight, 0.1f, 2000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setFogScaleHeight(fogScaleHeight);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Exponential falloff (0.1 = thin layer, 2000 = fog everywhere)");
        }

        float volumetricFar = renderer.getVolumetricFarPlane();
        if (ImGui::SliderFloat("Far Plane", &volumetricFar, 10.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
            renderer.setVolumetricFarPlane(volumetricFar);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volumetric range (10 = close only, 5000 = entire scene)");
        }

        float temporalBlend = renderer.getTemporalBlend();
        if (ImGui::SliderFloat("Temporal Blend", &temporalBlend, 0.0f, 0.999f, "%.3f")) {
            renderer.setTemporalBlend(temporalBlend);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no temporal filtering (noisy), 0.999 = extreme smoothing (ghosting)");
        }

        // Quick presets for common scenarios
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("Clear##froxel")) {
            renderer.setFogDensity(0.0f);
            renderer.setLayerDensity(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Light##froxel")) {
            renderer.setFogDensity(0.005f);
            renderer.setFogAbsorption(0.005f);
            renderer.setFogScaleHeight(100.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dense##froxel")) {
            renderer.setFogDensity(0.03f);
            renderer.setFogAbsorption(0.02f);
            renderer.setFogScaleHeight(50.0f);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== HEIGHT FOG LAYER ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 0.9f, 1.0f));
    ImGui::Text("HEIGHT FOG LAYER");
    ImGui::PopStyleColor();

    if (fogEnabled) {
        // Enable toggle for height fog layer
        if (ImGui::Checkbox("Enable Height Fog", &state.heightFogEnabled)) {
            if (state.heightFogEnabled) {
                // Restore cached density
                renderer.setLayerDensity(state.cachedLayerDensity);
            } else {
                // Cache current density and zero it out
                state.cachedLayerDensity = renderer.getLayerDensity();
                if (state.cachedLayerDensity < 0.001f) state.cachedLayerDensity = 0.02f;  // Ensure valid restore value
                renderer.setLayerDensity(0.0f);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle ground-hugging fog layer");
        }

        if (state.heightFogEnabled) {
            float layerHeight = renderer.getLayerHeight();
            if (ImGui::SliderFloat("Layer Height", &layerHeight, -200.0f, 500.0f, "%.1f")) {
                renderer.setLayerHeight(layerHeight);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Top of ground fog layer (-200 = below ground, 500 = high altitude cloud)");
            }

            float layerThickness = renderer.getLayerThickness();
            if (ImGui::SliderFloat("Layer Thickness", &layerThickness, 0.1f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                renderer.setLayerThickness(layerThickness);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Vertical extent (0.1 = paper thin, 500 = massive fog bank)");
            }

            float layerDensity = renderer.getLayerDensity();
            if (ImGui::SliderFloat("Layer Density", &layerDensity, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
                renderer.setLayerDensity(layerDensity);
                state.cachedLayerDensity = layerDensity;  // Update cache when manually changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 = invisible, 1 = completely opaque (logarithmic)");
            }

            // Quick presets
            ImGui::Text("Presets:");
            ImGui::SameLine();
            if (ImGui::Button("Valley##layer")) {
                renderer.setLayerHeight(20.0f);
                renderer.setLayerThickness(30.0f);
                renderer.setLayerDensity(0.03f);
                state.cachedLayerDensity = 0.03f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Thick Mist##layer")) {
                renderer.setLayerHeight(10.0f);
                renderer.setLayerThickness(15.0f);
                renderer.setLayerDensity(0.1f);
                state.cachedLayerDensity = 0.1f;
            }
        }
    } else {
        ImGui::TextDisabled("Enable Froxel Fog to access height fog settings");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ========== ATMOSPHERIC SCATTERING ==========
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
    ImGui::Text("ATMOSPHERIC SCATTERING");
    ImGui::PopStyleColor();

    AtmosphereParams atmosParams = renderer.getAtmosphereParams();
    bool atmosChanged = false;

    // Enable toggle for atmospheric scattering
    if (ImGui::Checkbox("Enable Atmosphere", &state.atmosphereEnabled)) {
        if (state.atmosphereEnabled) {
            // Restore cached values
            atmosParams.rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f) * (state.cachedRayleighScale / 13.558f);
            atmosParams.mieScatteringBase = state.cachedMieScale / 1000.0f;
            atmosChanged = true;
        } else {
            // Cache current values and zero out scattering
            state.cachedRayleighScale = atmosParams.rayleighScatteringBase.y * 1000.0f;
            state.cachedMieScale = atmosParams.mieScatteringBase * 1000.0f;
            if (state.cachedRayleighScale < 0.001f) state.cachedRayleighScale = 13.558f;
            if (state.cachedMieScale < 0.001f) state.cachedMieScale = 3.996f;
            atmosParams.rayleighScatteringBase = glm::vec3(0.0f);
            atmosParams.mieScatteringBase = 0.0f;
            atmosParams.mieAbsorptionBase = 0.0f;
            atmosParams.ozoneAbsorption = glm::vec3(0.0f);
            atmosChanged = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle sky scattering (Rayleigh blue sky, Mie haze)");
    }

    if (state.atmosphereEnabled) {
        // Rayleigh scattering (blue sky) - wide ranges for extreme testing
        ImGui::Text("Rayleigh Scattering (Air):");
        float rayleighScale = atmosParams.rayleighScatteringBase.y * 1000.0f;  // Scale for UI
        if (ImGui::SliderFloat("Rayleigh Strength", &rayleighScale, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            float oldVal = atmosParams.rayleighScatteringBase.y * 1000.0f;
            if (oldVal > 0.0001f) {
                float ratio = rayleighScale / oldVal;
                atmosParams.rayleighScatteringBase *= ratio;
            } else {
                // If starting from near zero, set to Earth-like ratio
                atmosParams.rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f) * (rayleighScale / 13.558f);
            }
            state.cachedRayleighScale = rayleighScale;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no blue sky, 13.5 = Earth, 200 = extremely blue (logarithmic)");
        }

        if (ImGui::SliderFloat("Rayleigh Scale Height", &atmosParams.rayleighScaleHeight, 0.1f, 100.0f, "%.1f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.1 = thin atmosphere, 8 = Earth, 100 = very thick");
        }

        // Mie scattering (haze/sun halo) - wide ranges
        ImGui::Spacing();
        ImGui::Text("Mie Scattering (Haze):");
        float mieScale = atmosParams.mieScatteringBase * 1000.0f;
        if (ImGui::SliderFloat("Mie Strength", &mieScale, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            atmosParams.mieScatteringBase = mieScale / 1000.0f;
            state.cachedMieScale = mieScale;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no haze, 4 = Earth, 200 = dense smog (logarithmic)");
        }

        if (ImGui::SliderFloat("Mie Scale Height", &atmosParams.mieScaleHeight, 0.01f, 50.0f, "%.2f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.01 = ground-level only, 1.2 = Earth, 50 = everywhere");
        }

        if (ImGui::SliderFloat("Mie Anisotropy", &atmosParams.mieAnisotropy, -0.99f, 0.99f, "%.2f")) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("-1 = backward scatter, 0 = uniform, 0.8 = Earth (forward), 0.99 = laser-like sun");
        }

        float mieAbsScale = atmosParams.mieAbsorptionBase * 1000.0f;
        if (ImGui::SliderFloat("Mie Absorption", &mieAbsScale, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            atmosParams.mieAbsorptionBase = mieAbsScale / 1000.0f;
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no absorption, 4.4 = Earth, 100 = heavy smog");
        }

        // Ozone (affects horizon color) - wide ranges
        ImGui::Spacing();
        ImGui::Text("Ozone Layer:");
        float ozoneScale = atmosParams.ozoneAbsorption.y * 1000.0f;
        if (ImGui::SliderFloat("Ozone Strength", &ozoneScale, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            float oldVal = atmosParams.ozoneAbsorption.y * 1000.0f;
            if (oldVal > 0.0001f) {
                float ratio = ozoneScale / oldVal;
                atmosParams.ozoneAbsorption *= ratio;
            } else {
                // If starting from near zero, set to Earth-like ratio
                atmosParams.ozoneAbsorption = glm::vec3(0.65e-3f, 1.881e-3f, 0.085e-3f) * (ozoneScale / 1.881f);
            }
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no ozone, 1.9 = Earth, 50 = extreme orange sunsets");
        }

        if (ImGui::SliderFloat("Ozone Center", &atmosParams.ozoneLayerCenter, 0.0f, 100.0f, "%.0f km")) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = at surface, 25 = Earth, 100 = very high");
        }

        if (ImGui::SliderFloat("Ozone Width", &atmosParams.ozoneLayerWidth, 0.1f, 100.0f, "%.1f km", ImGuiSliderFlags_Logarithmic)) {
            atmosChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.1 = thin band, 15 = Earth, 100 = everywhere");
        }

        // Quick presets
        ImGui::Spacing();
        ImGui::Text("Presets:");
        if (ImGui::Button("Earth##atmos")) {
            AtmosphereParams earth;
            renderer.setAtmosphereParams(earth);
            state.cachedRayleighScale = 13.558f;
            state.cachedMieScale = 3.996f;
            atmosChanged = false;  // Already set
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##atmos")) {
            AtmosphereParams clear;
            clear.mieScatteringBase = 1.0e-3f;
            clear.mieAbsorptionBase = 1.0e-3f;
            renderer.setAtmosphereParams(clear);
            state.cachedMieScale = 1.0f;
            atmosChanged = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hazy##atmos")) {
            AtmosphereParams hazy;
            hazy.mieScatteringBase = 15.0e-3f;
            hazy.mieAbsorptionBase = 10.0e-3f;
            hazy.mieAnisotropy = 0.7f;
            renderer.setAtmosphereParams(hazy);
            state.cachedMieScale = 15.0f;
            atmosChanged = false;
        }
    }

    if (atmosChanged) {
        renderer.setAtmosphereParams(atmosParams);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Leaf system
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("FALLING LEAVES");
    ImGui::PopStyleColor();

    float leafIntensity = renderer.getLeafIntensity();
    if (ImGui::SliderFloat("Leaf Intensity", &leafIntensity, 0.0f, 1.0f)) {
        renderer.setLeafIntensity(leafIntensity);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Cloud style
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.7f, 1.0f));
    ImGui::Text("CLOUDS");
    ImGui::PopStyleColor();

    bool paraboloid = renderer.isUsingParaboloidClouds();
    if (ImGui::Checkbox("Paraboloid LUT Clouds", &paraboloid)) {
        renderer.toggleCloudStyle();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle between procedural and paraboloid LUT hybrid cloud rendering");
    }

    // Cloud coverage and density controls
    float cloudCoverage = renderer.getCloudCoverage();
    if (ImGui::SliderFloat("Cloud Coverage", &cloudCoverage, 0.0f, 1.0f, "%.2f")) {
        renderer.setCloudCoverage(cloudCoverage);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = clear sky, 0.5 = partly cloudy, 1 = overcast");
    }

    float cloudDensity = renderer.getCloudDensity();
    if (ImGui::SliderFloat("Cloud Density", &cloudDensity, 0.0f, 1.0f, "%.2f")) {
        renderer.setCloudDensity(cloudDensity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = thin/wispy, 0.3 = normal, 1 = thick/opaque");
    }

    // Cloud presets
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Clear##clouds")) {
        renderer.setCloudCoverage(0.0f);
        renderer.setCloudDensity(0.3f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Partly##clouds")) {
        renderer.setCloudCoverage(0.4f);
        renderer.setCloudDensity(0.3f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cloudy##clouds")) {
        renderer.setCloudCoverage(0.7f);
        renderer.setCloudDensity(0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Overcast##clouds")) {
        renderer.setCloudCoverage(0.95f);
        renderer.setCloudDensity(0.7f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Grass interaction
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("GRASS INTERACTION");
    ImGui::PopStyleColor();

    auto& env = renderer.getEnvironmentSettings();
    if (ImGui::SliderFloat("Displacement Decay", &env.grassDisplacementDecay, 0.1f, 5.0f)) {}
    if (ImGui::SliderFloat("Max Displacement", &env.grassMaxDisplacement, 0.0f, 2.0f)) {}
}
