#pragma once

#include <cstdint>

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

    // LOD debug
    bool showLODOverlay = false;  // Show LOD level as screen text
    bool forceLODLevel = false;   // Override automatic LOD selection
    uint32_t forcedLOD = 0;       // Forced LOD level when override is enabled
};

namespace GuiPlayerTab {
    void render(IPlayerControl& playerControl, PlayerSettings& settings);
}
