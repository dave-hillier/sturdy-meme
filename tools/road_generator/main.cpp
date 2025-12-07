// Road network generator tool
// Generates road splines connecting settlements using A* pathfinding

#include "RoadSpline.h"
#include "RoadPathfinder.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <biome_map.png> <settlements.json> <output_dir> [options]\n"
              << "\n"
              << "Generates road network connecting settlements using terrain-aware pathfinding.\n"
              << "\n"
              << "Arguments:\n"
              << "  heightmap.png     16-bit PNG heightmap file\n"
              << "  biome_map.png     RGBA8 biome map from biome_preprocess\n"
              << "  settlements.json  Settlement data from biome_preprocess\n"
              << "  output_dir        Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --terrain-size <value>      World size in meters (default: 16384.0)\n"
              << "  --min-altitude <value>      Min altitude in heightmap (default: 0.0)\n"
              << "  --max-altitude <value>      Max altitude in heightmap (default: 200.0)\n"
              << "  --grid-resolution <value>   Pathfinding grid size (default: 512)\n"
              << "  --simplify-epsilon <value>  Path simplification threshold in meters (default: 10.0)\n"
              << "  --help                      Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  roads.json        Road network data in JSON format\n"
              << "  roads.bin         Binary road network for runtime loading\n"
              << "  roads_debug.png   Debug visualization of road network\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png biome_map.png settlements.json ./generated\n";
}

bool loadSettlements(const std::string& path, std::vector<Settlement>& settlements, float& terrainSize) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open settlements file: %s", path.c_str());
        return false;
    }

    try {
        json j;
        file >> j;

        if (j.contains("terrain_size")) {
            terrainSize = j["terrain_size"].get<float>();
        }

        for (const auto& s : j["settlements"]) {
            Settlement settlement;
            settlement.id = s["id"].get<uint32_t>();

            std::string typeStr = s["type"].get<std::string>();
            if (typeStr == "Town") settlement.type = SettlementType::Town;
            else if (typeStr == "Village") settlement.type = SettlementType::Village;
            else if (typeStr == "FishingVillage") settlement.type = SettlementType::FishingVillage;
            else settlement.type = SettlementType::Hamlet;

            settlement.position.x = s["x"].get<float>();
            settlement.position.y = s["z"].get<float>(); // Note: stored as z in JSON

            if (s.contains("score")) {
                settlement.score = s["score"].get<float>();
            }

            if (s.contains("features")) {
                for (const auto& f : s["features"]) {
                    settlement.features.push_back(f.get<std::string>());
                }
            }

            settlements.push_back(settlement);
        }

        SDL_Log("Loaded %zu settlements from %s", settlements.size(), path.c_str());
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse settlements JSON: %s", e.what());
        return false;
    }
}

bool saveRoadsJson(const std::string& path, const RoadGen::RoadNetwork& network) {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create roads JSON file: %s", path.c_str());
        return false;
    }

    file << "{\n";
    file << "  \"terrain_size\": " << network.terrainSize << ",\n";
    file << "  \"total_length_m\": " << network.getTotalLength() << ",\n";
    file << "  \"roads\": [\n";

    for (size_t i = 0; i < network.roads.size(); i++) {
        const auto& road = network.roads[i];

        file << "    {\n";
        file << "      \"type\": \"" << RoadGen::getRoadTypeName(road.type) << "\",\n";
        file << "      \"from_settlement\": " << road.fromSettlementId << ",\n";
        file << "      \"to_settlement\": " << road.toSettlementId << ",\n";
        file << "      \"length_m\": " << road.getLength() << ",\n";
        file << "      \"control_points\": [\n";

        for (size_t j = 0; j < road.controlPoints.size(); j++) {
            const auto& cp = road.controlPoints[j];
            file << "        {\"x\": " << cp.position.x << ", \"z\": " << cp.position.y;
            if (cp.widthOverride > 0.0f) {
                file << ", \"width\": " << cp.widthOverride;
            }
            file << "}";
            if (j < road.controlPoints.size() - 1) file << ",";
            file << "\n";
        }

        file << "      ]\n";
        file << "    }";
        if (i < network.roads.size() - 1) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    SDL_Log("Saved roads JSON: %s", path.c_str());
    return true;
}

bool saveRoadsBinary(const std::string& path, const RoadGen::RoadNetwork& network) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create roads binary file: %s", path.c_str());
        return false;
    }

    // Header
    const char magic[] = "ROAD";
    file.write(magic, 4);

    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    file.write(reinterpret_cast<const char*>(&network.terrainSize), sizeof(network.terrainSize));

    uint32_t numRoads = static_cast<uint32_t>(network.roads.size());
    file.write(reinterpret_cast<const char*>(&numRoads), sizeof(numRoads));

    // Roads
    for (const auto& road : network.roads) {
        uint8_t type = static_cast<uint8_t>(road.type);
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));

        file.write(reinterpret_cast<const char*>(&road.fromSettlementId), sizeof(road.fromSettlementId));
        file.write(reinterpret_cast<const char*>(&road.toSettlementId), sizeof(road.toSettlementId));

        uint32_t numPoints = static_cast<uint32_t>(road.controlPoints.size());
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));

        for (const auto& cp : road.controlPoints) {
            file.write(reinterpret_cast<const char*>(&cp.position.x), sizeof(cp.position.x));
            file.write(reinterpret_cast<const char*>(&cp.position.y), sizeof(cp.position.y));
            file.write(reinterpret_cast<const char*>(&cp.widthOverride), sizeof(cp.widthOverride));
        }
    }

    SDL_Log("Saved roads binary: %s (%lld bytes)", path.c_str(), static_cast<long long>(file.tellp()));
    return true;
}

bool saveDebugVisualization(const std::string& path, const RoadGen::RoadNetwork& network,
                            uint32_t imageSize = 1024) {
    std::vector<unsigned char> image(imageSize * imageSize * 4, 0);

    // Set background to dark gray
    for (size_t i = 0; i < imageSize * imageSize; i++) {
        image[i * 4 + 0] = 32;
        image[i * 4 + 1] = 32;
        image[i * 4 + 2] = 32;
        image[i * 4 + 3] = 255;
    }

    // Road type colors (BGR order for visualization)
    auto getRoadColor = [](RoadGen::RoadType type) -> glm::ivec3 {
        switch (type) {
            case RoadGen::RoadType::MainRoad:  return {255, 200, 100}; // Orange-yellow
            case RoadGen::RoadType::Road:      return {200, 180, 150}; // Light tan
            case RoadGen::RoadType::Lane:      return {150, 140, 130}; // Gray-tan
            case RoadGen::RoadType::Bridleway: return {120, 100, 80};  // Brown
            case RoadGen::RoadType::Footpath:  return {100, 80, 60};   // Dark brown
            default:                           return {128, 128, 128}; // Gray
        }
    };

    // Draw each road
    for (const auto& road : network.roads) {
        glm::ivec3 color = getRoadColor(road.type);
        float width = RoadGen::getRoadWidth(road.type);
        int pixelWidth = std::max(1, static_cast<int>(width * imageSize / network.terrainSize));

        for (size_t i = 1; i < road.controlPoints.size(); i++) {
            glm::vec2 p0 = road.controlPoints[i-1].position;
            glm::vec2 p1 = road.controlPoints[i].position;

            // Convert to pixel coordinates
            int x0 = static_cast<int>(p0.x / network.terrainSize * (imageSize - 1));
            int y0 = static_cast<int>(p0.y / network.terrainSize * (imageSize - 1));
            int x1 = static_cast<int>(p1.x / network.terrainSize * (imageSize - 1));
            int y1 = static_cast<int>(p1.y / network.terrainSize * (imageSize - 1));

            // Draw line using Bresenham's algorithm
            int dx = std::abs(x1 - x0);
            int dy = std::abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1;
            int sy = y0 < y1 ? 1 : -1;
            int err = dx - dy;

            while (true) {
                // Draw a thick line
                for (int wy = -pixelWidth/2; wy <= pixelWidth/2; wy++) {
                    for (int wx = -pixelWidth/2; wx <= pixelWidth/2; wx++) {
                        int px = x0 + wx;
                        int py = y0 + wy;
                        if (px >= 0 && px < static_cast<int>(imageSize) &&
                            py >= 0 && py < static_cast<int>(imageSize)) {
                            size_t idx = (py * imageSize + px) * 4;
                            image[idx + 0] = static_cast<unsigned char>(color.r);
                            image[idx + 1] = static_cast<unsigned char>(color.g);
                            image[idx + 2] = static_cast<unsigned char>(color.b);
                            image[idx + 3] = 255;
                        }
                    }
                }

                if (x0 == x1 && y0 == y1) break;

                int e2 = 2 * err;
                if (e2 > -dy) {
                    err -= dy;
                    x0 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        }
    }

    unsigned error = lodepng::encode(path, image, imageSize, imageSize);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save debug image: %s", lodepng_error_text(error));
        return false;
    }

    SDL_Log("Saved debug visualization: %s", path.c_str());
    return true;
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 5) {
        printUsage(argv[0]);
        return 1;
    }

    std::string heightmapPath = argv[1];
    std::string biomemapPath = argv[2];
    std::string settlementsPath = argv[3];
    std::string outputDir = argv[4];

    RoadGen::PathfinderConfig config;
    config.terrainSize = 16384.0f;
    config.minAltitude = 0.0f;
    config.maxAltitude = 200.0f;
    config.gridResolution = 512;
    config.simplifyEpsilon = 10.0f;

    // Parse optional arguments
    for (int i = 5; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else if (arg == "--grid-resolution" && i + 1 < argc) {
            config.gridResolution = std::stoul(argv[++i]);
        } else if (arg == "--simplify-epsilon" && i + 1 < argc) {
            config.simplifyEpsilon = std::stof(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if needed
    fs::create_directories(outputDir);

    SDL_Log("Road Network Generator");
    SDL_Log("======================");
    SDL_Log("Heightmap: %s", heightmapPath.c_str());
    SDL_Log("Biome map: %s", biomemapPath.c_str());
    SDL_Log("Settlements: %s", settlementsPath.c_str());
    SDL_Log("Output: %s", outputDir.c_str());
    SDL_Log("Terrain size: %.1f m", config.terrainSize);
    SDL_Log("Grid resolution: %u", config.gridResolution);
    SDL_Log("Simplification epsilon: %.1f m", config.simplifyEpsilon);

    // Load settlements
    std::vector<Settlement> settlements;
    float settlementsTerrainSize = config.terrainSize;

    if (!loadSettlements(settlementsPath, settlements, settlementsTerrainSize)) {
        return 1;
    }

    // Use terrain size from settlements if available
    config.terrainSize = settlementsTerrainSize;

    // Initialize pathfinder
    RoadGen::RoadPathfinder pathfinder;
    pathfinder.init(config);

    // Load terrain data
    SDL_Log("Loading terrain data...");

    if (!pathfinder.loadHeightmap(heightmapPath)) {
        return 1;
    }

    if (!pathfinder.loadBiomeMap(biomemapPath)) {
        return 1;
    }

    // Generate road network
    SDL_Log("Generating road network...");

    RoadGen::RoadNetwork network;

    bool success = pathfinder.generateRoadNetwork(settlements, network,
        [](float progress, const std::string& status) {
            SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
        });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Road generation failed!");
        return 1;
    }

    // Save outputs
    std::string jsonPath = outputDir + "/roads.json";
    std::string binPath = outputDir + "/roads.bin";
    std::string debugPath = outputDir + "/roads_debug.png";

    if (!saveRoadsJson(jsonPath, network)) {
        return 1;
    }

    if (!saveRoadsBinary(binPath, network)) {
        return 1;
    }

    if (!saveDebugVisualization(debugPath, network)) {
        return 1;
    }

    SDL_Log("Road generation complete!");
    SDL_Log("Output files:");
    SDL_Log("  %s", jsonPath.c_str());
    SDL_Log("  %s", binPath.c_str());
    SDL_Log("  %s", debugPath.c_str());

    return 0;
}
