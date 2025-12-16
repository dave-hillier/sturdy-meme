#include "GuiTimeTab.h"
#include "Renderer.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"

#include <imgui.h>
#include <algorithm>

void GuiTimeTab::render(Renderer& renderer) {
    ImGui::Spacing();

    // Time of day slider
    float timeOfDay = renderer.getTimeOfDay();
    if (ImGui::SliderFloat("Time of Day", &timeOfDay, 0.0f, 1.0f, "%.3f")) {
        renderer.setTimeOfDay(timeOfDay);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0.0 = Midnight, 0.25 = Sunrise, 0.5 = Noon, 0.75 = Sunset");
    }

    // Quick time buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Dawn")) renderer.setTimeOfDay(0.25f);
    ImGui::SameLine();
    if (ImGui::Button("Noon")) renderer.setTimeOfDay(0.5f);
    ImGui::SameLine();
    if (ImGui::Button("Dusk")) renderer.setTimeOfDay(0.75f);
    ImGui::SameLine();
    if (ImGui::Button("Night")) renderer.setTimeOfDay(0.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Time scale
    float timeScale = renderer.getTimeScale();
    if (ImGui::SliderFloat("Time Scale", &timeScale, 0.0f, 100.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
        renderer.setTimeScale(timeScale);
    }

    if (ImGui::Button("Resume Real-Time")) {
        renderer.resumeAutoTime();
        renderer.setTimeScale(1.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Date controls
    ImGui::Text("Date (affects sun position):");
    int year = renderer.getCurrentYear();
    int month = renderer.getCurrentMonth();
    int day = renderer.getCurrentDay();

    bool dateChanged = false;
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Year", &year, 1, 10)) dateChanged = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Month", &month, 1, 1)) dateChanged = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Day", &day, 1, 1)) dateChanged = true;

    if (dateChanged) {
        month = std::clamp(month, 1, 12);
        day = std::clamp(day, 1, 31);
        renderer.setDate(year, month, day);
    }

    // Season presets
    ImGui::Text("Season:");
    ImGui::SameLine();
    if (ImGui::Button("Spring")) renderer.setDate(renderer.getCurrentYear(), 3, 20);
    ImGui::SameLine();
    if (ImGui::Button("Summer")) renderer.setDate(renderer.getCurrentYear(), 6, 21);
    ImGui::SameLine();
    if (ImGui::Button("Autumn")) renderer.setDate(renderer.getCurrentYear(), 9, 22);
    ImGui::SameLine();
    if (ImGui::Button("Winter")) renderer.setDate(renderer.getCurrentYear(), 12, 21);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Location
    GeographicLocation loc = renderer.getLocation();
    float lat = static_cast<float>(loc.latitude);
    float lon = static_cast<float>(loc.longitude);
    bool locChanged = false;

    if (ImGui::SliderFloat("Latitude", &lat, -90.0f, 90.0f, "%.1f")) locChanged = true;
    if (ImGui::SliderFloat("Longitude", &lon, -180.0f, 180.0f, "%.1f")) locChanged = true;

    if (locChanged) {
        renderer.setLocation({static_cast<double>(lat), static_cast<double>(lon)});
    }

    // Location presets
    ImGui::Text("Location:");
    if (ImGui::Button("London")) {
        renderer.setLocation({51.5f, -0.1f});
    }
    ImGui::SameLine();
    if (ImGui::Button("New York")) {
        renderer.setLocation({40.7f, -74.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Tokyo")) {
        renderer.setLocation({35.7f, 139.7f});
    }
    if (ImGui::Button("Sydney")) {
        renderer.setLocation({-33.9f, 151.2f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Arctic")) {
        renderer.setLocation({71.0f, 25.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Equator")) {
        renderer.setLocation({0.0f, 0.0f});
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Moon Phase Controls
    ImGui::Text("Moon Phase:");

    // Display current moon phase
    float currentPhase = renderer.getCurrentMoonPhase();
    const char* phaseNames[] = { "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                 "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent" };
    int phaseIndex = static_cast<int>(currentPhase * 8.0f) % 8;
    ImGui::Text("Current: %s (%.2f)", phaseNames[phaseIndex], currentPhase);

    // Override checkbox
    bool overrideEnabled = renderer.isMoonPhaseOverrideEnabled();
    if (ImGui::Checkbox("Override Moon Phase", &overrideEnabled)) {
        renderer.setMoonPhaseOverride(overrideEnabled);
    }

    // Manual phase slider (only active when override is enabled)
    if (overrideEnabled) {
        float manualPhase = renderer.getMoonPhase();
        if (ImGui::SliderFloat("Moon Phase", &manualPhase, 0.0f, 1.0f, "%.3f")) {
            renderer.setMoonPhase(manualPhase);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = New Moon, 0.25 = First Quarter, 0.5 = Full Moon, 0.75 = Last Quarter");
        }

        // Quick phase buttons
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("New")) renderer.setMoonPhase(0.0f);
        ImGui::SameLine();
        if (ImGui::Button("1st Q")) renderer.setMoonPhase(0.25f);
        ImGui::SameLine();
        if (ImGui::Button("Full")) renderer.setMoonPhase(0.5f);
        ImGui::SameLine();
        if (ImGui::Button("3rd Q")) renderer.setMoonPhase(0.75f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Moon Brightness Controls
    ImGui::Text("Moon Brightness:");

    float moonBrightness = renderer.getMoonBrightness();
    if (ImGui::SliderFloat("Light Intensity", &moonBrightness, 0.0f, 5.0f, "%.2f")) {
        renderer.setMoonBrightness(moonBrightness);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Multiplier for moonlight intensity on terrain (0-5, default 1.0)");
    }

    float moonDiscIntensity = renderer.getMoonDiscIntensity();
    if (ImGui::SliderFloat("Disc Intensity", &moonDiscIntensity, 0.0f, 50.0f, "%.1f")) {
        renderer.setMoonDiscIntensity(moonDiscIntensity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visual brightness of moon disc in sky (0-50, default 20)");
    }

    float moonEarthshine = renderer.getMoonEarthshine();
    if (ImGui::SliderFloat("Earthshine", &moonEarthshine, 0.0f, 0.2f, "%.3f")) {
        renderer.setMoonEarthshine(moonEarthshine);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visibility of dark side during crescent phases (0-0.2, default 0.02)");
    }

    // Brightness presets
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Dim")) {
        renderer.setMoonBrightness(0.5f);
        renderer.setMoonDiscIntensity(10.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Normal")) {
        renderer.setMoonBrightness(1.0f);
        renderer.setMoonDiscIntensity(20.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bright")) {
        renderer.setMoonBrightness(2.0f);
        renderer.setMoonDiscIntensity(35.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Eclipse Controls
    ImGui::Text("Solar Eclipse:");

    bool eclipseEnabled = renderer.isEclipseEnabled();
    if (ImGui::Checkbox("Enable Eclipse", &eclipseEnabled)) {
        renderer.setEclipseEnabled(eclipseEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Simulates a solar eclipse with the moon passing in front of the sun");
    }

    if (eclipseEnabled) {
        float eclipseAmount = renderer.getEclipseAmount();
        if (ImGui::SliderFloat("Eclipse Amount", &eclipseAmount, 0.0f, 1.0f, "%.3f")) {
            renderer.setEclipseAmount(eclipseAmount);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = No eclipse, 1.0 = Total eclipse");
        }

        // Eclipse presets
        ImGui::Text("Presets:");
        ImGui::SameLine();
        if (ImGui::Button("Partial")) renderer.setEclipseAmount(0.5f);
        ImGui::SameLine();
        if (ImGui::Button("Annular")) renderer.setEclipseAmount(0.85f);
        ImGui::SameLine();
        if (ImGui::Button("Total")) renderer.setEclipseAmount(1.0f);
    }
}
