// HDRPassResources.cpp - Factory implementation

#include "HDRPassResources.h"
#include "../RendererSystems.h"

HDRPassResources HDRPassResources::collect(RendererSystems& systems) {
    HDRPassResources resources;

    resources.profiler = &systems.profiler();
    resources.postProcess = &systems.postProcess();
    resources.sky = &systems.sky();
    resources.terrain = &systems.terrain();
    resources.geometry = systems.geometry();
    resources.scene = &systems.scene();
    resources.skinnedMesh = &systems.skinnedMesh();
    resources.globalBuffers = &systems.globalBuffers();
    resources.shadow = &systems.shadow();
    resources.vegetation = systems.vegetation();
    resources.water = &systems.water();
    resources.waterTileCull = systems.hasWaterTileCull() ? &systems.waterTileCull() : nullptr;
    resources.snow = systems.snowGroup();
    resources.wind = &systems.wind();
    resources.debugLine = &systems.debugLine();
    resources.npcRenderer = systems.npcRenderer();  // May be null if no NPCs
    resources.ecsWorld = systems.ecsWorld();  // Phase 6: ECS world for direct entity queries

    return resources;
}
