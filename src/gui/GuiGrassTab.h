#pragma once

class IGrassControl;
class IEnvironmentControl;

/**
 * GUI tab for grass system controls.
 * Provides LOD strategy selection, debug visualization toggles,
 * and real-time statistics.
 */
class GuiGrassTab {
public:
    static void render(IGrassControl& grass, IEnvironmentControl& envControl);
};
