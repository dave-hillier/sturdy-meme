#pragma once

#include "d8.h"
#include <vector>
#include <string>
#include <cstdint>

struct Point {
    double x;
    double y;
};

struct River {
    std::vector<Point> points;              // Path from headwater to outlet
    std::vector<uint32_t> accumulation;     // Flow accumulation at each point
    uint32_t max_accumulation;              // Maximum flow accumulation along river
};

// Extract individual river paths from river_map
// Each river is traced from headwaters downstream to outlet
std::vector<River> extract_river_paths(
    const std::vector<uint32_t>& river_map,
    const D8Result& d8,
    int width,
    int height
);

// Write all rivers to a single SVG file with fitted curves
// output_width/height can differ from processing dimensions to scale coordinates
void write_rivers_svg(
    const std::string& filename,
    const std::vector<River>& rivers,
    int width,
    int height,
    int output_width = 0,
    int output_height = 0
);
