#include "river_binary.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

bool write_rivers_geojson(
    const std::string& filename,
    const std::vector<River>& rivers,
    const ElevationGrid& elevation,
    int processing_width,
    int processing_height,
    const RiverGeoJsonConfig& config
) {
    // Find max accumulation for width scaling
    uint32_t global_max_acc = 0;
    for (const auto& river : rivers) {
        global_max_acc = std::max(global_max_acc, river.max_accumulation);
    }
    double log_max = std::log(static_cast<double>(global_max_acc) + 1.0);

    // Scale factors for coordinate conversion
    // Processing coords are in [0, processing_width/height]
    // World coords are centered: [-terrainSize/2, terrainSize/2]
    double scale_x = config.terrainSize / processing_width;
    double scale_z = config.terrainSize / processing_height;
    double offset = config.terrainSize / 2.0;

    // Height conversion: elevation is 0-65535, maps to minAltitude-maxAltitude
    double height_range = config.maxAltitude - config.minAltitude;

    // Build GeoJSON FeatureCollection
    json featureCollection;
    featureCollection["type"] = "FeatureCollection";
    featureCollection["properties"] = {
        {"terrainSize", config.terrainSize},
        {"minAltitude", config.minAltitude},
        {"maxAltitude", config.maxAltitude}
    };

    json features = json::array();
    size_t totalPoints = 0;

    for (const auto& river : rivers) {
        // Calculate river width based on log-scaled accumulation
        double log_acc = std::log(static_cast<double>(river.max_accumulation) + 1.0);
        double width_factor = log_acc / log_max;
        float riverWidth = static_cast<float>(
            config.minRiverWidth + (config.maxRiverWidth - config.minRiverWidth) * width_factor
        );

        // Build coordinate array with 3D points [x, z, y] (GeoJSON uses [lon, lat, alt])
        json coordinates = json::array();
        json widths = json::array();

        for (const auto& pt : river.points) {
            // Convert from processing pixel coords to world coords
            float worldX = static_cast<float>(pt.x * scale_x - offset);
            float worldZ = static_cast<float>(pt.y * scale_z - offset);

            // Sample height from elevation grid
            int elev_x = static_cast<int>(pt.x * elevation.width / processing_width);
            int elev_y = static_cast<int>(pt.y * elevation.height / processing_height);
            elev_x = std::clamp(elev_x, 0, elevation.width - 1);
            elev_y = std::clamp(elev_y, 0, elevation.height - 1);

            uint16_t elev_val = elevation.at(elev_x, elev_y);
            float normalizedHeight = static_cast<float>(elev_val) / 65535.0f;
            float worldY = static_cast<float>(config.minAltitude + normalizedHeight * height_range);

            // GeoJSON coordinates: [x, z, y] where y is altitude
            coordinates.push_back({worldX, worldZ, worldY});
            widths.push_back(riverWidth);
        }

        json feature;
        feature["type"] = "Feature";
        feature["geometry"] = {
            {"type", "LineString"},
            {"coordinates", coordinates}
        };
        feature["properties"] = {
            {"totalFlow", static_cast<float>(river.max_accumulation)},
            {"width", riverWidth},
            {"widths", widths}
        };

        features.push_back(feature);
        totalPoints += river.points.size();
    }

    featureCollection["features"] = features;

    // Write to file with pretty formatting
    std::ofstream file(filename);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rivers GeoJSON file: %s", filename.c_str());
        return false;
    }

    file << featureCollection.dump(2);

    if (!file.good()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error writing to file: %s", filename.c_str());
        return false;
    }

    SDL_Log("Saved rivers GeoJSON: %s (%zu rivers, %zu total points)",
            filename.c_str(), rivers.size(), totalPoints);

    return true;
}

bool write_lakes_geojson(const std::string& filename) {
    // Create empty FeatureCollection for lakes
    json featureCollection;
    featureCollection["type"] = "FeatureCollection";
    featureCollection["features"] = json::array();

    std::ofstream file(filename);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create lakes GeoJSON file: %s", filename.c_str());
        return false;
    }

    file << featureCollection.dump(2);

    if (!file.good()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error writing to file: %s", filename.c_str());
        return false;
    }

    SDL_Log("Saved lakes GeoJSON: %s (0 lakes, placeholder)", filename.c_str());
    return true;
}
