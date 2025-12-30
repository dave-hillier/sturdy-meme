#pragma once

#include "RoadSpline.h"
#include "SpaceColonization.h"
#include <string>
#include <vector>

namespace RoadGen {

// Write the space colonization network topology to SVG
void writeNetworkSVG(
    const std::string& filename,
    const ColonizationResult& network,
    const std::vector<Settlement>& settlements,
    float terrainSize,
    int outputWidth = 1024,
    int outputHeight = 1024
);

// Write the final road splines to SVG
void writeRoadsSVG(
    const std::string& filename,
    const RoadNetwork& roads,
    const std::vector<Settlement>& settlements,
    int outputWidth = 1024,
    int outputHeight = 1024
);

} // namespace RoadGen
