// ShadowPassResources.cpp - Factory implementation

#include "ShadowPassResources.h"
#include "../RendererSystems.h"

ShadowPassResources ShadowPassResources::collect(RendererSystems& systems) {
    ShadowPassResources resources;

    resources.profiler = &systems.profiler();
    resources.shadow = &systems.shadow();
    resources.terrain = &systems.terrain();
    resources.vegetation = systems.vegetation();
    resources.scene = &systems.scene();
    resources.globalBuffers = &systems.globalBuffers();
    resources.skinnedMesh = &systems.skinnedMesh();

    return resources;
}
