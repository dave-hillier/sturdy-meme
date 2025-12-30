#pragma once

#include "SettlementGenerator.h"
#include <string>
#include <vector>

// Write settlements to an SVG file with organic perimeter shapes
void writeSettlementsSVG(
    const std::string& filename,
    const std::vector<Settlement>& settlements,
    float terrainSize,
    int outputWidth = 2048,
    int outputHeight = 2048
);
