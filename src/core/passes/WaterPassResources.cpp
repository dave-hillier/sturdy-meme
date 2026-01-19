// WaterPassResources.cpp - Factory implementation

#include "WaterPassResources.h"
#include "../RendererSystems.h"

WaterPassResources WaterPassResources::collect(RendererSystems& systems) {
    WaterPassResources resources;

    resources.profiler = &systems.profiler();
    resources.water = &systems.water();
    resources.waterGBuffer = &systems.waterGBuffer();
    resources.waterTileCull = systems.hasWaterTileCull() ? &systems.waterTileCull() : nullptr;
    resources.ssr = &systems.ssr();
    resources.postProcess = &systems.postProcess();

    return resources;
}
