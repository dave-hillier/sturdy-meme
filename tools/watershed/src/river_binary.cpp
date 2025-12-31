#include "river_binary.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <cmath>
#include <algorithm>

bool write_rivers_binary(
    const std::string& filename,
    const std::vector<River>& rivers,
    const ElevationGrid& elevation,
    int processing_width,
    int processing_height,
    const RiverBinaryConfig& config
) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rivers binary file: %s", filename.c_str());
        return false;
    }

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

    // Write number of rivers
    uint32_t numRivers = static_cast<uint32_t>(rivers.size());
    file.write(reinterpret_cast<const char*>(&numRivers), sizeof(numRivers));

    size_t totalPoints = 0;

    for (const auto& river : rivers) {
        uint32_t numPoints = static_cast<uint32_t>(river.points.size());
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));

        // Write control points (x, height, z) as glm::vec3
        for (const auto& pt : river.points) {
            // Convert from processing pixel coords to world coords
            // pt.x and pt.y are in [0, processing_width/height] (pixel coords with 0.5 offset)
            float worldX = static_cast<float>(pt.x * scale_x - offset);
            float worldZ = static_cast<float>(pt.y * scale_z - offset);

            // Sample height from elevation grid
            // Need to map processing coords back to elevation grid coords
            int elev_x = static_cast<int>(pt.x * elevation.width / processing_width);
            int elev_y = static_cast<int>(pt.y * elevation.height / processing_height);
            elev_x = std::clamp(elev_x, 0, elevation.width - 1);
            elev_y = std::clamp(elev_y, 0, elevation.height - 1);

            uint16_t elev_val = elevation.at(elev_x, elev_y);
            float normalizedHeight = static_cast<float>(elev_val) / 65535.0f;
            float worldY = static_cast<float>(config.minAltitude + normalizedHeight * height_range);

            file.write(reinterpret_cast<const char*>(&worldX), sizeof(float));
            file.write(reinterpret_cast<const char*>(&worldY), sizeof(float));
            file.write(reinterpret_cast<const char*>(&worldZ), sizeof(float));
        }

        // Write widths for each point
        // Width based on log-scaled accumulation, like SVG stroke width
        double log_acc = std::log(static_cast<double>(river.max_accumulation) + 1.0);
        double width_factor = log_acc / log_max;
        float riverWidth = static_cast<float>(
            config.minRiverWidth + (config.maxRiverWidth - config.minRiverWidth) * width_factor
        );

        for (size_t i = 0; i < river.points.size(); i++) {
            file.write(reinterpret_cast<const char*>(&riverWidth), sizeof(float));
        }

        // Write total flow (use max_accumulation as proxy)
        float totalFlow = static_cast<float>(river.max_accumulation);
        file.write(reinterpret_cast<const char*>(&totalFlow), sizeof(float));

        totalPoints += river.points.size();
    }

    SDL_Log("Saved rivers binary: %s (%zu rivers, %zu total points, %lld bytes)",
            filename.c_str(), rivers.size(), totalPoints, static_cast<long long>(file.tellp()));

    return true;
}

bool write_lakes_binary(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create lakes binary file: %s", filename.c_str());
        return false;
    }

    // Write 0 lakes (empty placeholder)
    uint32_t numLakes = 0;
    file.write(reinterpret_cast<const char*>(&numLakes), sizeof(numLakes));

    SDL_Log("Saved lakes binary: %s (0 lakes, placeholder)", filename.c_str());
    return true;
}
