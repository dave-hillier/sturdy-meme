#pragma once

#include "d8.h"
#include "elevation_grid.h"
#include <vector>
#include <cstdint>

struct WatershedResult {
    std::vector<uint32_t> labels;  // Watershed basin labels
    uint32_t basin_count;
    int width;
    int height;

    uint32_t label_at(int x, int y) const {
        return labels[y * width + x];
    }
};

// Delineate watersheds from D8 flow directions
WatershedResult delineate_watersheds(const D8Result& d8);

// Merge small watersheds based on minimum area threshold
WatershedResult merge_watersheds(
    const WatershedResult& watersheds,
    const ElevationGrid& elevation,
    const D8Result& d8,
    uint32_t min_area
);
