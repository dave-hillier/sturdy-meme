#pragma once

class Renderer;

// Player settings for GUI control
struct PlayerSettings {
    // Cape
    bool capeEnabled = false;
    bool showCapeColliders = false;

    // Future player settings can go here
};

namespace GuiPlayerTab {
    void render(Renderer& renderer, PlayerSettings& settings);
}
