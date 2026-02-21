#pragma once

class ITerrainControl;
class ITimeSystem;
class Camera;

namespace GuiDashboard {
    struct State {
        float frameTimeHistory[120] = {0};
        int frameTimeIndex = 0;
        float avgFrameTime = 0.0f;
    };

    void render(ITerrainControl& terrain, ITimeSystem& time, const Camera& camera,
                float deltaTime, float fps, State& state);
}
