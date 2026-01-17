#include "FrameUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

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
