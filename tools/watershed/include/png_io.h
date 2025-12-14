#pragma once

#include "elevation_grid.h"
#include "watershed.h"
#include <string>

// Read 16-bit grayscale PNG as elevation grid
ElevationGrid read_elevation_png(const std::string& filename);

// Write watershed labels as colored PNG (24-bit RGB)
void write_watershed_png(const std::string& filename, const WatershedResult& watersheds);

// Write flow accumulation as 16-bit grayscale PNG (log-scaled)
void write_flow_accumulation_png(const std::string& filename, const D8Result& d8);

// Write river network as PNG (cells with flow accumulation >= threshold)
void write_rivers_png(const std::string& filename, const D8Result& d8, uint32_t threshold);

// Write traced river network as PNG (rivers connected to sea/edge outlets)
void write_traced_rivers_png(
    const std::string& filename,
    const std::vector<uint32_t>& river_map,
    int width, int height
);

// Write combined terrain + rivers PNG (R=rivers, G=elevation, B=sea)
void write_terrain_rivers_png(
    const std::string& filename,
    const ElevationGrid& elevation,
    const D8Result& d8,
    uint32_t river_threshold,
    uint16_t sea_level = 0
);

// Write combined terrain + traced rivers PNG
void write_terrain_traced_rivers_png(
    const std::string& filename,
    const ElevationGrid& elevation,
    const std::vector<uint32_t>& river_map,
    uint16_t sea_level = 0
);
