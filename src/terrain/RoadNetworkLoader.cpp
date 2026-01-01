#include "RoadNetworkLoader.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string RoadNetworkLoader::getRoadsPath(const std::string& cacheDir) {
    return cacheDir + "/roads.geojson";
}

bool RoadNetworkLoader::loadFromGeoJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: Failed to open %s", path.c_str());
        return false;
    }

    try {
        json j;
        file >> j;

        // Read FeatureCollection properties
        if (j.contains("properties")) {
            const auto& props = j["properties"];
            if (props.contains("terrain_size")) {
                roadNetwork.terrainSize = props["terrain_size"].get<float>();
            }
        }

        roadNetwork.roads.clear();

        // Read features (roads)
        if (j.contains("features")) {
            for (const auto& feature : j["features"]) {
                if (feature["geometry"]["type"] != "LineString") continue;

                RoadSpline road;

                // Read properties
                const auto& props = feature["properties"];
                std::string typeStr = props.value("type", "lane");
                if (typeStr == "footpath") road.type = RoadType::Footpath;
                else if (typeStr == "bridleway") road.type = RoadType::Bridleway;
                else if (typeStr == "lane") road.type = RoadType::Lane;
                else if (typeStr == "road") road.type = RoadType::Road;
                else if (typeStr == "main_road") road.type = RoadType::MainRoad;
                else road.type = RoadType::Lane;

                road.fromSettlementId = props.value("from_settlement", 0u);
                road.toSettlementId = props.value("to_settlement", 0u);

                float defaultWidth = props.value("width", 0.0f);

                // Read coordinates from geometry
                const auto& coords = feature["geometry"]["coordinates"];
                for (const auto& coord : coords) {
                    RoadControlPoint point;
                    point.position.x = coord[0].get<float>();
                    point.position.y = coord[1].get<float>();  // z in world space
                    point.widthOverride = defaultWidth;
                    road.controlPoints.push_back(point);
                }

                roadNetwork.roads.push_back(std::move(road));
            }
        }

        loaded = true;
        SDL_Log("RoadNetworkLoader: Loaded %zu roads from %s", roadNetwork.roads.size(), path.c_str());
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RoadNetworkLoader: GeoJSON parse error: %s", e.what());
        return false;
    }
}
