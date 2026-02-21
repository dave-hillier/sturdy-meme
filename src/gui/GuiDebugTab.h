#pragma once

class IDebugControl;

namespace GuiDebugTab {
    void renderVisualizations(IDebugControl& debugControl);
    void renderPhysicsDebugOptions(IDebugControl& debugControl);
    void renderOcclusionCulling(IDebugControl& debugControl);
    void renderSystemInfo();
    void renderKeyboardShortcuts();
}
