#pragma once

class IPlayerControl;

// Player settings for GUI control
struct PlayerSettings {
    // Cape
    bool capeEnabled = false;
    bool showCapeColliders = false;

    // Weapons debug
    bool showWeaponAxes = false;  // Show RGB axis indicators on hand bones
};

namespace GuiPlayerTab {
    void render(IPlayerControl& playerControl, PlayerSettings& settings);
}
