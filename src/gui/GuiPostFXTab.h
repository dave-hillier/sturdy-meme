#pragma once

class IPostProcessState;
class ICloudShadowControl;

namespace GuiPostFXTab {
    void render(IPostProcessState& postProcess, ICloudShadowControl& cloudShadow);
}
