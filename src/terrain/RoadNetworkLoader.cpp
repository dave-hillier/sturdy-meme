#include "RoadNetworkLoader.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string RoadNetworkLoader::getRoadsPath(const std::string& cacheDir) {
    return cacheDir + "/roads.bin";
}

bool RoadNetworkLoader::loadFromBinary(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: Failed to open %s", path.c_str());
        return false;
    }

    // Read and verify magic
    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 'R' || magic[1] != 'O' || magic[2] != 'A' || magic[3] != 'D') {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: Invalid file format");
        return false;
    }

    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: Unsupported version %u", version);
        return false;
    }

    // Read terrain size
    file.read(reinterpret_cast<char*>(&roadNetwork.terrainSize), sizeof(roadNetwork.terrainSize));

    // Read number of roads
    uint32_t numRoads;
    file.read(reinterpret_cast<char*>(&numRoads), sizeof(numRoads));

    roadNetwork.roads.clear();
    roadNetwork.roads.reserve(numRoads);

    // Read each road
    for (uint32_t i = 0; i < numRoads; i++) {
        RoadSpline road;

        uint8_t type;
        file.read(reinterpret_cast<char*>(&type), sizeof(type));
        road.type = static_cast<RoadType>(type);

        file.read(reinterpret_cast<char*>(&road.fromSettlementId), sizeof(road.fromSettlementId));
        file.read(reinterpret_cast<char*>(&road.toSettlementId), sizeof(road.toSettlementId));

        uint32_t numPoints;
        file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));

        road.controlPoints.reserve(numPoints);
        for (uint32_t j = 0; j < numPoints; j++) {
            RoadControlPoint cp;
            file.read(reinterpret_cast<char*>(&cp.position.x), sizeof(cp.position.x));
            file.read(reinterpret_cast<char*>(&cp.position.y), sizeof(cp.position.y));
            file.read(reinterpret_cast<char*>(&cp.widthOverride), sizeof(cp.widthOverride));
            road.controlPoints.push_back(cp);
        }

        roadNetwork.roads.push_back(std::move(road));
    }

    loaded = true;
    SDL_Log("RoadNetworkLoader: Loaded %zu roads from %s", roadNetwork.roads.size(), path.c_str());
    return true;
}

bool RoadNetworkLoader::loadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: Failed to open %s", path.c_str());
        return false;
    }

    try {
        json j;
        file >> j;

        if (j.contains("terrain_size")) {
            roadNetwork.terrainSize = j["terrain_size"].get<float>();
        }

        roadNetwork.roads.clear();

        for (const auto& r : j["roads"]) {
            RoadSpline road;

            std::string typeStr = r["type"].get<std::string>();
            if (typeStr == "footpath") road.type = RoadType::Footpath;
            else if (typeStr == "bridleway") road.type = RoadType::Bridleway;
            else if (typeStr == "lane") road.type = RoadType::Lane;
            else if (typeStr == "road") road.type = RoadType::Road;
            else if (typeStr == "main_road") road.type = RoadType::MainRoad;
            else road.type = RoadType::Lane;

            road.fromSettlementId = r["from_settlement"].get<uint32_t>();
            road.toSettlementId = r["to_settlement"].get<uint32_t>();

            for (const auto& cp : r["control_points"]) {
                RoadControlPoint point;
                point.position.x = cp["x"].get<float>();
                point.position.y = cp["z"].get<float>();
                point.widthOverride = cp.contains("width") ? cp["width"].get<float>() : 0.0f;
                road.controlPoints.push_back(point);
            }

            roadNetwork.roads.push_back(std::move(road));
        }

        loaded = true;
        SDL_Log("RoadNetworkLoader: Loaded %zu roads from %s", roadNetwork.roads.size(), path.c_str());
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: JSON parse error: %s", e.what());
        return false;
    }
}
