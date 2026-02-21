#pragma once

class IPostProcessState;
class ICloudShadowControl;

namespace GuiPostFXTab {
    void renderHDRPipeline(IPostProcessState& postProcess);
    void renderCloudShadows(ICloudShadowControl& cloudShadow);
    void renderBloom(IPostProcessState& postProcess);
    void renderGodRays(IPostProcessState& postProcess);
    void renderVolumetricFogSettings(IPostProcessState& postProcess);
    void renderLocalToneMapping(IPostProcessState& postProcess);
    void renderExposure(IPostProcessState& postProcess);
}
