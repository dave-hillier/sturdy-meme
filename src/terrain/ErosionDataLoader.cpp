#include "ErosionDataLoader.h"
#include <SDL3/SDL_log.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// Cache file paths
std::string ErosionDataLoader::getFlowMapPath(const std::string& cacheDir) {
    return cacheDir + "/flow_accumulation.raw";
}

std::string ErosionDataLoader::getRiversPath(const std::string& cacheDir) {
    return cacheDir + "/rivers.dat";
}

std::string ErosionDataLoader::getLakesPath(const std::string& cacheDir) {
    return cacheDir + "/lakes.dat";
}

std::string ErosionDataLoader::getMetadataPath(const std::string& cacheDir) {
    return cacheDir + "/erosion_data.meta";
}

bool ErosionDataLoader::isCacheValid(const ErosionLoadConfig& config) const {
    return loadAndValidateMetadata(config);
}

bool ErosionDataLoader::loadAndValidateMetadata(const ErosionLoadConfig& config) const {
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

    // Check all cache files exist
    if (!fs::exists(getFlowMapPath(config.cacheDirectory)) ||
        !fs::exists(getRiversPath(config.cacheDirectory)) ||
        !fs::exists(getLakesPath(config.cacheDirectory))) {
        SDL_Log("Erosion cache: missing cache files");
        return false;
    }

    SDL_Log("Erosion cache: valid cache found");
    return true;
}

bool ErosionDataLoader::loadFromCache(const ErosionLoadConfig& config) {
    uint32_t flowWidth = 0;
    uint32_t flowHeight = 0;

    // Load flow map and direction
    {
        std::ifstream file(getFlowMapPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&flowWidth), sizeof(flowWidth));
        file.read(reinterpret_cast<char*>(&flowHeight), sizeof(flowHeight));

        waterData.flowAccumulation.resize(flowWidth * flowHeight);
        file.read(reinterpret_cast<char*>(waterData.flowAccumulation.data()),
                  waterData.flowAccumulation.size() * sizeof(float));

        // Try to load flow direction (may not exist in older caches)
        waterData.flowDirection.resize(flowWidth * flowHeight, -1);
        file.read(reinterpret_cast<char*>(waterData.flowDirection.data()),
                  waterData.flowDirection.size() * sizeof(int8_t));

        waterData.flowMapWidth = flowWidth;
        waterData.flowMapHeight = flowHeight;
    }

    // Load rivers
    {
        std::ifstream file(getRiversPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numRivers;
        file.read(reinterpret_cast<char*>(&numRivers), sizeof(numRivers));

        waterData.rivers.resize(numRivers);
        for (auto& river : waterData.rivers) {
            uint32_t numPoints;
            file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));

            river.controlPoints.resize(numPoints);
            river.widths.resize(numPoints);

            file.read(reinterpret_cast<char*>(river.controlPoints.data()),
                      numPoints * sizeof(glm::vec3));
            file.read(reinterpret_cast<char*>(river.widths.data()),
                      numPoints * sizeof(float));
            file.read(reinterpret_cast<char*>(&river.totalFlow), sizeof(float));
        }
    }

    // Load lakes
    {
        std::ifstream file(getLakesPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numLakes;
        file.read(reinterpret_cast<char*>(&numLakes), sizeof(numLakes));

        waterData.lakes.resize(numLakes);
        for (auto& lake : waterData.lakes) {
            file.read(reinterpret_cast<char*>(&lake.position), sizeof(glm::vec2));
            file.read(reinterpret_cast<char*>(&lake.waterLevel), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.radius), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.area), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.depth), sizeof(float));
        }
    }

    waterData.seaLevel = config.seaLevel;
    SDL_Log("Erosion: loaded from cache - %zu rivers, %zu lakes",
            waterData.rivers.size(), waterData.lakes.size());

    return true;
}
