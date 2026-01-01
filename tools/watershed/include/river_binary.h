#pragma once

#include "river_svg.h"
#include "elevation_grid.h"
#include <string>
#include <cstdint>

// Configuration for world-space coordinate conversion
struct RiverGeoJsonConfig {
    float terrainSize = 16384.0f;    // World size in meters
    float minAltitude = 0.0f;        // Minimum altitude for heightmap 0
    float maxAltitude = 200.0f;      // Maximum altitude for heightmap 65535
    float minRiverWidth = 2.0f;      // Minimum river width in meters
    float maxRiverWidth = 30.0f;     // Maximum river width in meters
};

// Write rivers in GeoJSON format compatible with ErosionDataLoader
// Uses LineString features with properties for width and flow
bool write_rivers_geojson(
    const std::string& filename,
    const std::vector<River>& rivers,
    const ElevationGrid& elevation,
    int processing_width,
    int processing_height,
    const RiverGeoJsonConfig& config
);

// Write empty lakes.geojson file (required by ErosionDataLoader)
bool write_lakes_geojson(const std::string& filename);
