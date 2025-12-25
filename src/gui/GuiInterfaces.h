#pragma once

// Forward declarations for all GUI control interfaces
class ITimeSystem;
class ILocationControl;
class IWeatherState;
class IEnvironmentControl;
class IPostProcessState;
class ICloudShadowControl;
class ITerrainControl;
class IWaterControl;
class ITreeControl;
class IDebugControl;
class IProfilerControl;
class IPerformanceControl;
class ISceneControl;
class IPlayerControl;
struct EnvironmentSettings;

/**
 * GuiInterfaces - Aggregates all control interfaces needed by the GUI system.
 *
 * This struct allows GuiSystem to receive all its dependencies directly,
 * breaking the dependency on Renderer. The Application is responsible for
 * building this struct from RendererSystems.
 */
struct GuiInterfaces {
    ITimeSystem& time;
    ILocationControl& location;
    IWeatherState& weather;
    IEnvironmentControl& environment;
    IPostProcessState& postProcess;
    ICloudShadowControl& cloudShadow;
    ITerrainControl& terrain;
    IWaterControl& water;
    ITreeControl& tree;
    IDebugControl& debug;
    IProfilerControl& profiler;
    IPerformanceControl& performance;
    ISceneControl& scene;
    IPlayerControl& player;
    EnvironmentSettings& environmentSettings;
};
