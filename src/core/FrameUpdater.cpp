#include "FrameUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "GPUSceneBuffer.h"
#include "DebugLineSystem.h"
#include "controls/DebugControlSubsystem.h"
// advanceBufferSets
#include "GrassSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "WaterTileCull.h"

#include "updaters/VegetationUpdater.h"
#include "updaters/AtmosphereUpdater.h"
#include "updaters/EnvironmentUpdater.h"

void FrameUpdater::updateAllSystems(
    RendererSystems& systems,
    const FrameData& frame,
    VkExtent2D extent,
    const SnowConfig& snowConfig)
{
    systems.profiler().beginCpuZone("SystemUpdates");

    // Atmosphere updates first (wind affects vegetation)
    AtmosphereUpdater::SnowConfig atmosSnowConfig;
    atmosSnowConfig.maxSnowHeight = snowConfig.maxSnowHeight;
    atmosSnowConfig.useVolumetricSnow = snowConfig.useVolumetricSnow;
    AtmosphereUpdater::update(systems, frame, atmosSnowConfig);

    // Environment updates (terrain/water)
    EnvironmentUpdater::Config envConfig;
    envConfig.maxSnowHeight = snowConfig.maxSnowHeight;
    envConfig.useVolumetricSnow = snowConfig.useVolumetricSnow;
    EnvironmentUpdater::update(systems, frame, envConfig);

    // Vegetation updates last (depends on wind)
    VegetationUpdater::update(systems, frame, extent);

    systems.profiler().endCpuZone("SystemUpdates");
}

void FrameUpdater::populateGPUSceneBuffer(RendererSystems& systems, const FrameData& frame) {
    if (!systems.hasGPUSceneBuffer()) return;

    systems.profiler().beginCpuZone("GPUSceneBuffer");
    GPUSceneBuffer& sceneBuffer = systems.gpuSceneBuffer();
    sceneBuffer.beginFrame(frame.frameIndex);

    const auto& sceneObjects = systems.scene().getRenderables();

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip GPU-skinned characters (player + NPCs, rendered via separate pipeline)
        if (sceneObjects[i].gpuSkinned) continue;

        sceneBuffer.addObject(sceneObjects[i]);
    }

    sceneBuffer.finalize();
    systems.profiler().endCpuZone("GPUSceneBuffer");
}

void FrameUpdater::advanceBufferSets(RendererSystems& systems, uint32_t frameIndex) {
    systems.grass().advanceBufferSet();
    systems.weather().advanceBufferSet();
    systems.leaf().advanceBufferSet();
    if (systems.hasWaterTileCull()) {
        systems.waterTileCull().endFrame(frameIndex);
    }
}

void FrameUpdater::updateDebugLines(RendererSystems& systems, uint32_t frameIndex) {
    // Begin debug line frame if not already started by physics debug
    if (!systems.debugLine().hasLines()) {
        systems.debugLine().beginFrame(frameIndex);
    }

    // Add road/river visualization to debug lines
    systems.debugControlSubsystem().updateRoadRiverVisualization();

    // Upload debug lines if any are present
    if (systems.debugLine().hasLines()) {
        systems.debugLine().uploadLines();
    }
}
