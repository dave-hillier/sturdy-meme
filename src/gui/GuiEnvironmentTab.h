#pragma once

class IEnvironmentControl;

// State for environment tab toggles that need to persist
struct EnvironmentTabState {
    bool heightFogEnabled = true;
    float cachedLayerDensity = 0.02f;
    bool atmosphereEnabled = true;
    float cachedRayleighScale = 13.558f;
    float cachedMieScale = 3.996f;
};

namespace GuiEnvironmentTab {
    void renderFroxelFog(IEnvironmentControl& envControl);
    void renderHeightFog(IEnvironmentControl& envControl, EnvironmentTabState& state);
    void renderAtmosphere(IEnvironmentControl& envControl, EnvironmentTabState& state);
    void renderLeaves(IEnvironmentControl& envControl);
    void renderClouds(IEnvironmentControl& envControl);
}
