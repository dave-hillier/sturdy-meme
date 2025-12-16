#include "GuiWeatherTab.h"
#include "Renderer.h"
#include "WindSystem.h"
#include "LeafSystem.h"

#include <imgui.h>
#include <glm/glm.hpp>

void GuiWeatherTab::render(Renderer& renderer) {
    ImGui::Spacing();

    // Weather type
    const char* weatherTypes[] = { "Rain", "Snow" };
    int weatherType = static_cast<int>(renderer.getWeatherType());
    if (ImGui::Combo("Weather Type", &weatherType, weatherTypes, 2)) {
        renderer.setWeatherType(static_cast<uint32_t>(weatherType));
    }

    // Intensity
    float intensity = renderer.getIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
        renderer.setWeatherIntensity(intensity);
    }

    // Quick intensity buttons
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) renderer.setWeatherIntensity(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Light")) renderer.setWeatherIntensity(0.3f);
    ImGui::SameLine();
    if (ImGui::Button("Medium")) renderer.setWeatherIntensity(0.6f);
    ImGui::SameLine();
    if (ImGui::Button("Heavy")) renderer.setWeatherIntensity(1.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Snow coverage
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("SNOW COVERAGE");
    ImGui::PopStyleColor();

    float snowAmount = renderer.getSnowAmount();
    if (ImGui::SliderFloat("Snow Amount", &snowAmount, 0.0f, 1.0f)) {
        renderer.setSnowAmount(snowAmount);
    }

    glm::vec3 snowColor = renderer.getSnowColor();
    float sc[3] = {snowColor.r, snowColor.g, snowColor.b};
    if (ImGui::ColorEdit3("Snow Color", sc)) {
        renderer.setSnowColor(glm::vec3(sc[0], sc[1], sc[2]));
    }

    // Environment settings for snow
    auto& env = renderer.getEnvironmentSettings();

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
