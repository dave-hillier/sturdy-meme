// PostPassResources.cpp - Factory implementation

#include "PostPassResources.h"
#include "../RendererSystems.h"

PostPassResources PostPassResources::collect(RendererSystems& systems) {
    PostPassResources resources;

    resources.profiler = &systems.profiler();
    resources.postProcess = &systems.postProcess();
    resources.bloom = &systems.bloom();
    resources.bilateralGrid = &systems.bilateralGrid();
    resources.hiZ = &systems.hiZ();

    return resources;
}
