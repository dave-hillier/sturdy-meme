#pragma once

class IPlayerControl;

// Player settings for GUI control
struct PlayerSettings {
    // Cape
    bool capeEnabled = false;
    bool showCapeColliders = false;

    // Weapons
    bool showSword = true;        // Show sword in right hand
    bool showShield = true;       // Show shield on left arm

    // Weapons debug
    bool showWeaponAxes = false;  // Show RGB axis indicators on hand bones
};

namespace GuiPlayerTab {
    void render(IPlayerControl& playerControl, PlayerSettings& settings);
}
