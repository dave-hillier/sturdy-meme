#pragma once

#include <cstdint>
#include <vector>

struct ElevationGrid {
    std::vector<uint16_t> data;
    int width;
    int height;

    uint16_t at(int x, int y) const {
        return data[y * width + x];
    }

    uint16_t& at(int x, int y) {
        return data[y * width + x];
    }

    bool in_bounds(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }
};

// Downsample an elevation grid to target resolution (uses max elevation in each block)
ElevationGrid downsample_elevation(const ElevationGrid& src, int target_size);
