#include "GuiWaterTab.h"
#include "Renderer.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"

#include <imgui.h>
#include <glm/glm.hpp>

void GuiWaterTab::render(Renderer& renderer) {
    ImGui::Spacing();

    auto& water = renderer.getWaterSystem();

    // Water info header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.9f, 1.0f));
    ImGui::Text("WATER SYSTEM");
    ImGui::PopStyleColor();

    ImGui::Text("Current Level: %.2f m", water.getWaterLevel());
    ImGui::Text("Base Level: %.2f m", water.getBaseWaterLevel());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Water level controls
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("LEVEL & TIDES");
    ImGui::PopStyleColor();

    float baseLevel = water.getBaseWaterLevel();
    if (ImGui::SliderFloat("Base Water Level", &baseLevel, -50.0f, 50.0f, "%.1f m")) {
        water.setWaterLevel(baseLevel);
    }

    float tidalRange = water.getTidalRange();
    if (ImGui::SliderFloat("Tidal Range", &tidalRange, 0.0f, 10.0f, "%.1f m")) {
        water.setTidalRange(tidalRange);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum tide variation from base level");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Wave parameters
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("WAVES");
    ImGui::PopStyleColor();

    // FFT Ocean toggle
    bool useFFT = water.getUseFFTOcean();
    if (ImGui::Checkbox("FFT Ocean (Tessendorf)", &useFFT)) {
        water.setUseFFTOcean(useFFT);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use FFT-based ocean simulation instead of Gerstner waves");
    }

    float amplitude = water.getWaveAmplitude();
    if (ImGui::SliderFloat("Amplitude", &amplitude, 0.0f, 5.0f, "%.2f m")) {
        water.setWaveAmplitude(amplitude);
    }

    float wavelength = water.getWaveLength();
    if (ImGui::SliderFloat("Wavelength", &wavelength, 1.0f, 100.0f, "%.1f m")) {
        water.setWaveLength(wavelength);
    }

    float steepness = water.getWaveSteepness();
    if (ImGui::SliderFloat("Steepness", &steepness, 0.0f, 1.0f, "%.2f")) {
        water.setWaveSteepness(steepness);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Wave sharpness (0=sine, 1=peaked)");
    }

    float speed = water.getWaveSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.0f, 3.0f, "%.2f")) {
        water.setWaveSpeed(speed);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Appearance
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("APPEARANCE");
    ImGui::PopStyleColor();

    glm::vec4 waterColor = water.getWaterColor();
    float col[4] = {waterColor.r, waterColor.g, waterColor.b, waterColor.a};
    if (ImGui::ColorEdit4("Water Color", col)) {
        water.setWaterColor(glm::vec4(col[0], col[1], col[2], col[3]));
    }

    float foam = water.getFoamThreshold();
    if (ImGui::SliderFloat("Foam Threshold", &foam, 0.0f, 2.0f, "%.2f")) {
        water.setFoamThreshold(foam);
    }

    float fresnel = water.getFresnelPower();
    if (ImGui::SliderFloat("Fresnel Power", &fresnel, 1.0f, 10.0f, "%.1f")) {
        water.setFresnelPower(fresnel);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Controls reflection intensity at grazing angles");
    }

    // Shore effects
    ImGui::Spacing();
    ImGui::Text("Shore Effects:");

    float shoreBlend = water.getShoreBlendDistance();
    if (ImGui::SliderFloat("Shore Blend", &shoreBlend, 0.5f, 10.0f, "%.1f m")) {
        water.setShoreBlendDistance(shoreBlend);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Distance over which water fades near shore");
    }

    float shoreFoam = water.getShoreFoamWidth();
    if (ImGui::SliderFloat("Shore Foam Width", &shoreFoam, 1.0f, 20.0f, "%.1f m")) {
        water.setShoreFoamWidth(shoreFoam);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Width of foam bands along the shoreline");
    }

    // Presets
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Presets:");
    if (ImGui::Button("Ocean")) {
        water.setWaterColor(glm::vec4(0.02f, 0.08f, 0.15f, 0.95f));
        water.setWaveAmplitude(1.5f);
        water.setWaveLength(30.0f);
        water.setWaveSteepness(0.4f);
        water.setWaveSpeed(0.8f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Lake")) {
        water.setWaterColor(glm::vec4(0.05f, 0.12f, 0.18f, 0.9f));
        water.setWaveAmplitude(0.3f);
        water.setWaveLength(8.0f);
        water.setWaveSteepness(0.2f);
        water.setWaveSpeed(0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Calm")) {
        water.setWaterColor(glm::vec4(0.03f, 0.1f, 0.2f, 0.85f));
        water.setWaveAmplitude(0.1f);
        water.setWaveLength(5.0f);
        water.setWaveSteepness(0.1f);
        water.setWaveSpeed(0.3f);
    }
    if (ImGui::Button("Storm")) {
        water.setWaterColor(glm::vec4(0.04f, 0.06f, 0.1f, 0.98f));
        water.setWaveAmplitude(3.0f);
        water.setWaveLength(20.0f);
        water.setWaveSteepness(0.6f);
        water.setWaveSpeed(1.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Tropical")) {
        water.setWaterColor(glm::vec4(0.0f, 0.15f, 0.2f, 0.8f));
        water.setWaveAmplitude(0.5f);
        water.setWaveLength(12.0f);
        water.setWaveSteepness(0.3f);
        water.setWaveSpeed(0.6f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Performance optimization controls
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("PERFORMANCE");
    ImGui::PopStyleColor();

    auto& tileCull = renderer.getWaterTileCull();
    bool tileCullEnabled = tileCull.isEnabled();
    if (ImGui::Checkbox("Tile Culling", &tileCullEnabled)) {
        tileCull.setEnabled(tileCullEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Skip water rendering when not visible (temporal)");
    }

    // Show tile cull stats when enabled
    if (tileCullEnabled) {
        glm::uvec2 tileCount = tileCull.getTileCount();
        ImGui::Text("Tiles: %ux%u (%u px)", tileCount.x, tileCount.y, tileCull.getTileSize());
    }
}
