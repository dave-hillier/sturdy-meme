#pragma once

class IWeatherState;
struct EnvironmentSettings;

namespace GuiWeatherTab {
    void render(IWeatherState& weatherState, EnvironmentSettings& envSettings);
}
