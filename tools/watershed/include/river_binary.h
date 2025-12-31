#pragma once

#include "river_svg.h"
#include "elevation_grid.h"
#include <string>
#include <cstdint>

// Configuration for world-space coordinate conversion
struct RiverBinaryConfig {
    float terrainSize = 16384.0f;    // World size in meters
    float minAltitude = 0.0f;        // Minimum altitude for heightmap 0
    float maxAltitude = 200.0f;      // Maximum altitude for heightmap 65535
    float minRiverWidth = 2.0f;      // Minimum river width in meters
    float maxRiverWidth = 30.0f;     // Maximum river width in meters
};

// Write rivers in binary format compatible with ErosionDataLoader
// Format:
//   uint32_t numRivers
//   For each river:
//     uint32_t numPoints
//     glm::vec3[numPoints] controlPoints (x, height, z) world-space
//     float[numPoints] widths
//     float totalFlow
bool write_rivers_binary(
    const std::string& filename,
    const std::vector<River>& rivers,
    const ElevationGrid& elevation,
    int processing_width,
    int processing_height,
    const RiverBinaryConfig& config
);

// Write empty lakes.dat file (required by ErosionDataLoader)
bool write_lakes_binary(const std::string& filename);
