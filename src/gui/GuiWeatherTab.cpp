#include "GuiWeatherTab.h"
#include "core/interfaces/IWeatherControl.h"
#include "WindSystem.h"
#include "LeafSystem.h"
#include "EnvironmentSettings.h"

#include <imgui.h>
#include <glm/glm.hpp>

void GuiWeatherTab::render(IWeatherControl& weatherControl) {
    ImGui::Spacing();

    // Weather type
    const char* weatherTypes[] = { "Rain", "Snow" };
    int weatherType = static_cast<int>(weatherControl.getWeatherType());
    if (ImGui::Combo("Weather Type", &weatherType, weatherTypes, 2)) {
        weatherControl.setWeatherType(static_cast<uint32_t>(weatherType));
    }

    // Intensity
    float intensity = weatherControl.getIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
        weatherControl.setWeatherIntensity(intensity);
    }

    // Quick intensity buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) weatherControl.setWeatherIntensity(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Light")) weatherControl.setWeatherIntensity(0.3f);
    ImGui::SameLine();
    if (ImGui::Button("Medium")) weatherControl.setWeatherIntensity(0.6f);
    ImGui::SameLine();
    if (ImGui::Button("Heavy")) weatherControl.setWeatherIntensity(1.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Snow coverage
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("SNOW COVERAGE");
    ImGui::PopStyleColor();

    float snowAmount = weatherControl.getSnowAmount();
    if (ImGui::SliderFloat("Snow Amount", &snowAmount, 0.0f, 1.0f)) {
        weatherControl.setSnowAmount(snowAmount);
    }

    glm::vec3 snowColor = weatherControl.getSnowColor();
    float sc[3] = {snowColor.r, snowColor.g, snowColor.b};
    if (ImGui::ColorEdit3("Snow Color", sc)) {
        weatherControl.setSnowColor(glm::vec3(sc[0], sc[1], sc[2]));
    }

    // Environment settings for snow
    auto& env = weatherControl.getEnvironmentSettings();

    if (ImGui::SliderFloat("Snow Roughness", &env.snowRoughness, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat("Accumulation Rate", &env.snowAccumulationRate, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat("Melt Rate", &env.snowMeltRate, 0.0f, 1.0f)) {}

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Wind settings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.7f, 1.0f));
    ImGui::Text("WIND");
    ImGui::PopStyleColor();

    float windDir[2] = {env.windDirection.x, env.windDirection.y};
    if (ImGui::SliderFloat2("Direction", windDir, -1.0f, 1.0f)) {
        env.windDirection = glm::vec2(windDir[0], windDir[1]);
    }

    if (ImGui::SliderFloat("Strength", &env.windStrength, 0.0f, 3.0f)) {}
    if (ImGui::SliderFloat("Speed", &env.windSpeed, 0.0f, 5.0f)) {}
    if (ImGui::SliderFloat("Gust Frequency", &env.gustFrequency, 0.0f, 2.0f)) {}
    if (ImGui::SliderFloat("Gust Amplitude", &env.gustAmplitude, 0.0f, 2.0f)) {}
}
