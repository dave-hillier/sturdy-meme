#pragma once

#include "elevation_grid.h"
#include <vector>
#include <cstdint>

// D8 flow direction encoding:
// 7 0 1
// 6 X 2
// 5 4 3
// Value 8 = no flow (pit or flat)

struct D8Result {
    std::vector<uint8_t> flow_direction;  // Direction 0-7, or 8 for no flow
    std::vector<uint32_t> flow_accumulation;
    int width;
    int height;

    uint8_t direction_at(int x, int y) const {
        return flow_direction[y * width + x];
    }

    uint32_t accumulation_at(int x, int y) const {
        return flow_accumulation[y * width + x];
    }
};

// Compute D8 flow directions from elevation grid
D8Result compute_d8(const ElevationGrid& elevation);

// Resolve DAFA (Depression and Flat Areas) using watershed merging
// This updates flow directions so all cells can drain to edge/sea
// Based on the watershed merging algorithm that preserves original DEM
D8Result resolve_dafa_by_merging(const ElevationGrid& elevation, D8Result d8, uint16_t sea_level);

// Get the dx, dy offset for a given direction
void get_d8_offset(uint8_t direction, int& dx, int& dy);

// Trace rivers from sea outlets upstream
// Returns a vector where each cell contains the "river order" (0 = not a river)
// Higher values indicate larger rivers (more upstream area)
std::vector<uint32_t> trace_rivers_from_sea(
    const ElevationGrid& elevation,
    const D8Result& d8,
    uint32_t min_accumulation,
    uint16_t sea_level
);
