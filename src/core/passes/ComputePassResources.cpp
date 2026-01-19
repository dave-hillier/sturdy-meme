// ComputePassResources.cpp - Factory implementation

#include "ComputePassResources.h"
#include "../RendererSystems.h"

ComputePassResources ComputePassResources::collect(RendererSystems& systems) {
    ComputePassResources resources;

    resources.profiler = &systems.profiler();
    resources.postProcess = &systems.postProcess();
    resources.globalBuffers = &systems.globalBuffers();
    resources.shadow = &systems.shadow();

    resources.terrain = &systems.terrain();
    resources.catmullClark = &systems.catmullClark();
    resources.displacement = &systems.displacement();
    resources.grass = &systems.grass();

    resources.weather = &systems.weather();
    resources.leaf = &systems.leaf();
    resources.snowMask = &systems.snowMask();
    resources.volumetricSnow = &systems.volumetricSnow();

    // Optional tree systems
    resources.tree = systems.tree();
    resources.treeRenderer = systems.treeRenderer();
    resources.treeLOD = systems.treeLOD();
    resources.impostorCull = systems.impostorCull();

    resources.hiZ = &systems.hiZ();
    resources.flowMap = &systems.flowMap();
    resources.foam = &systems.foam();
    resources.cloudShadow = &systems.cloudShadow();
    resources.wind = &systems.wind();

    resources.froxel = &systems.froxel();
    resources.atmosphereLUT = &systems.atmosphereLUT();

    return resources;
}
