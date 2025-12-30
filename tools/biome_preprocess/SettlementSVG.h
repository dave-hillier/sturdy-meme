#pragma once

#include "BiomeGenerator.h"
#include <string>
#include <vector>

// Write settlement data to an SVG file for visualization
// Similar to river_svg for watershed output and RoadSVG for road network output
void writeSettlementsSVG(
    const std::string& filename,
    const std::vector<Settlement>& settlements,
    float terrainSize,
    int outputWidth = 1024,
    int outputHeight = 1024
);
