#include "ErosionDataLoader.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Cache file paths
std::string ErosionDataLoader::getFlowMapPath(const std::string& cacheDir) {
    return cacheDir + "/flow_accumulation.exr";
}

std::string ErosionDataLoader::getRiversPath(const std::string& cacheDir) {
    return cacheDir + "/rivers.geojson";
}

std::string ErosionDataLoader::getLakesPath(const std::string& cacheDir) {
    return cacheDir + "/lakes.geojson";
}

std::string ErosionDataLoader::getMetadataPath(const std::string& cacheDir) {
    return cacheDir + "/erosion_data.meta";
}

bool ErosionDataLoader::isCacheValid(const ErosionLoadConfig& config) const {
    return loadAndValidateMetadata(config);
}

bool ErosionDataLoader::loadAndValidateMetadata(const ErosionLoadConfig& config) const {
    // Check all cache files exist first
    if (!fs::exists(getRiversPath(config.cacheDirectory)) ||
        !fs::exists(getLakesPath(config.cacheDirectory))) {
        SDL_Log("Erosion cache: missing cache files in %s", config.cacheDirectory.c_str());
        return false;
    }

    // Flow map is optional for visualization-only mode
    bool hasFlowMap = fs::exists(getFlowMapPath(config.cacheDirectory));

    // Skip source validation if no source heightmap specified (test/development mode)
    if (config.sourceHeightmapPath.empty()) {
        SDL_Log("Erosion cache: loading without source validation (test mode)");
        return true;
    }

    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        SDL_Log("Erosion cache: metadata file not found at %s", metaPath.c_str());
        return false;
    }

    std::string line;
    uintmax_t cachedSourceSize = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "sourceFileSize") cachedSourceSize = std::stoull(value);
        }
    }

    // Validate source file size matches (path may differ between preprocessing and runtime)
    std::error_code sizeEc;
    uintmax_t currentSourceSize = fs::file_size(config.sourceHeightmapPath, sizeEc);
    if (sizeEc || cachedSourceSize != currentSourceSize) {
        SDL_Log("Erosion cache: source file size mismatch (cached: %ju, current: %ju)",
                static_cast<uintmax_t>(cachedSourceSize),
                sizeEc ? static_cast<uintmax_t>(0) : static_cast<uintmax_t>(currentSourceSize));
        return false;
    }

    if (!hasFlowMap) {
        SDL_Log("Erosion cache: missing flow map (visualization-only mode)");
    }

    SDL_Log("Erosion cache: valid cache found");
    return true;
}

bool ErosionDataLoader::loadFromCache(const ErosionLoadConfig& config) {
    // Load rivers from GeoJSON
    {
        std::ifstream file(getRiversPath(config.cacheDirectory));
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ErosionDataLoader: Failed to open rivers GeoJSON: %s",
                getRiversPath(config.cacheDirectory).c_str());
            return false;
        }

        try {
            json j;
            file >> j;

            waterData.rivers.clear();

            if (j.contains("features")) {
                for (const auto& feature : j["features"]) {
                    if (feature["geometry"]["type"] != "LineString") continue;

                    RiverSpline river;

                    // Read coordinates (each point is [x, z, y] where y is altitude)
                    const auto& coords = feature["geometry"]["coordinates"];
                    for (const auto& coord : coords) {
                        glm::vec3 point;
                        point.x = coord[0].get<float>();  // worldX
                        point.z = coord[1].get<float>();  // worldZ
                        point.y = coord[2].get<float>();  // worldY (altitude)
                        river.controlPoints.push_back(point);
                    }

                    // Read properties
                    const auto& props = feature["properties"];
                    river.totalFlow = props.value("totalFlow", 0.0f);

                    // Read per-point widths if available
                    if (props.contains("widths")) {
                        for (const auto& w : props["widths"]) {
                            river.widths.push_back(w.get<float>());
                        }
                    } else {
                        // Fall back to single width for all points
                        float width = props.value("width", 5.0f);
                        river.widths.resize(river.controlPoints.size(), width);
                    }

                    waterData.rivers.push_back(std::move(river));
                }
            }
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ErosionDataLoader: Failed to parse rivers GeoJSON: %s", e.what());
            return false;
        }
    }

    // Load lakes from GeoJSON
    {
        std::ifstream file(getLakesPath(config.cacheDirectory));
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ErosionDataLoader: Failed to open lakes GeoJSON: %s",
                getLakesPath(config.cacheDirectory).c_str());
            return false;
        }

        try {
            json j;
            file >> j;

            waterData.lakes.clear();

            if (j.contains("features")) {
                for (const auto& feature : j["features"]) {
                    Lake lake;

                    // Read geometry (Point or Polygon centroid)
                    const auto& geom = feature["geometry"];
                    if (geom["type"] == "Point") {
                        const auto& coord = geom["coordinates"];
                        lake.position.x = coord[0].get<float>();
                        lake.position.y = coord[1].get<float>();
                    } else if (geom["type"] == "Polygon") {
                        // Calculate centroid from polygon
                        const auto& coords = geom["coordinates"][0];  // outer ring
                        float sumX = 0, sumY = 0;
                        for (const auto& coord : coords) {
                            sumX += coord[0].get<float>();
                            sumY += coord[1].get<float>();
                        }
                        lake.position.x = sumX / coords.size();
                        lake.position.y = sumY / coords.size();
                    }

                    // Read properties
                    const auto& props = feature["properties"];
                    lake.waterLevel = props.value("waterLevel", 0.0f);
                    lake.radius = props.value("radius", 10.0f);
                    lake.area = props.value("area", 0.0f);
                    lake.depth = props.value("depth", 1.0f);

                    waterData.lakes.push_back(lake);
                }
            }
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ErosionDataLoader: Failed to parse lakes GeoJSON: %s", e.what());
            return false;
        }
    }

    waterData.seaLevel = config.seaLevel;
    SDL_Log("Erosion: loaded from cache - %zu rivers, %zu lakes",
            waterData.rivers.size(), waterData.lakes.size());

    return true;
}
