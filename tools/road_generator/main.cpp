// Road network generator tool
// Generates road splines connecting settlements using space colonization + A* pathfinding

#include "RoadSpline.h"
#include "RoadPathfinder.h"
#include "SpaceColonization.h"
#include "RoadSVG.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <cmath>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Build stamp - tracks inputs and config to skip reprocessing when unchanged
struct RoadBuildConfig {
    std::string heightmapPath;
    std::string biomemapPath;
    std::string settlementsPath;
    std::string outputDir;
    float terrainSize;
    float minAltitude;
    float maxAltitude;
    uint32_t gridResolution;
    float simplifyEpsilon;
    bool useColonization;
};

bool isRoadOutputUpToDate(const RoadBuildConfig& config) {
    std::string metaPath = config.outputDir + "/roads.meta";
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    uintmax_t cachedHeightmapSize = 0;
    uintmax_t cachedBiomemapSize = 0;
    uintmax_t cachedSettlementsSize = 0;
    float cachedTerrainSize = 0;
    float cachedMinAltitude = 0;
    float cachedMaxAltitude = 0;
    uint32_t cachedGridResolution = 0;
    float cachedSimplifyEpsilon = 0;
    bool cachedUseColonization = false;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "heightmapSize") cachedHeightmapSize = std::stoull(value);
            else if (key == "biomemapSize") cachedBiomemapSize = std::stoull(value);
            else if (key == "settlementsSize") cachedSettlementsSize = std::stoull(value);
            else if (key == "terrainSize") cachedTerrainSize = std::stof(value);
            else if (key == "minAltitude") cachedMinAltitude = std::stof(value);
            else if (key == "maxAltitude") cachedMaxAltitude = std::stof(value);
            else if (key == "gridResolution") cachedGridResolution = std::stoul(value);
            else if (key == "simplifyEpsilon") cachedSimplifyEpsilon = std::stof(value);
            else if (key == "useColonization") cachedUseColonization = (value == "1");
        }
    }

    // Check input files exist and sizes match
    std::error_code ec;
    if (!fs::exists(config.heightmapPath, ec)) return false;
    if (!fs::exists(config.biomemapPath, ec)) return false;
    if (!fs::exists(config.settlementsPath, ec)) return false;

    uintmax_t heightmapSize = fs::file_size(config.heightmapPath, ec);
    if (ec || heightmapSize != cachedHeightmapSize) {
        SDL_Log("Roads: heightmap file size changed, reprocessing");
        return false;
    }

    uintmax_t biomemapSize = fs::file_size(config.biomemapPath, ec);
    if (ec || biomemapSize != cachedBiomemapSize) {
        SDL_Log("Roads: biome map file size changed, reprocessing");
        return false;
    }

    uintmax_t settlementsSize = fs::file_size(config.settlementsPath, ec);
    if (ec || settlementsSize != cachedSettlementsSize) {
        SDL_Log("Roads: settlements file size changed, reprocessing");
        return false;
    }

    // Check config matches
    if (std::abs(cachedTerrainSize - config.terrainSize) > 0.1f ||
        std::abs(cachedMinAltitude - config.minAltitude) > 0.01f ||
        std::abs(cachedMaxAltitude - config.maxAltitude) > 0.01f ||
        cachedGridResolution != config.gridResolution ||
        std::abs(cachedSimplifyEpsilon - config.simplifyEpsilon) > 0.01f ||
        cachedUseColonization != config.useColonization) {
        SDL_Log("Roads: configuration changed, reprocessing");
        return false;
    }

    // Check output files exist
    std::vector<std::string> outputs = {
        "roads.geojson", "roads_debug.png", "roads.svg"
    };
    for (const auto& output : outputs) {
        std::string path = config.outputDir + "/" + output;
        if (!fs::exists(path, ec)) {
            SDL_Log("Roads: missing output %s, reprocessing", output.c_str());
            return false;
        }
    }

    return true;
}

bool saveRoadBuildStamp(const RoadBuildConfig& config) {
    std::string metaPath = config.outputDir + "/roads.meta";
    std::ofstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    std::error_code ec;
    uintmax_t heightmapSize = fs::file_size(config.heightmapPath, ec);
    if (ec) return false;
    uintmax_t biomemapSize = fs::file_size(config.biomemapPath, ec);
    if (ec) return false;
    uintmax_t settlementsSize = fs::file_size(config.settlementsPath, ec);
    if (ec) return false;

    file << "heightmapSize=" << heightmapSize << "\n";
    file << "biomemapSize=" << biomemapSize << "\n";
    file << "settlementsSize=" << settlementsSize << "\n";
    file << "terrainSize=" << config.terrainSize << "\n";
    file << "minAltitude=" << config.minAltitude << "\n";
    file << "maxAltitude=" << config.maxAltitude << "\n";
    file << "gridResolution=" << config.gridResolution << "\n";
    file << "simplifyEpsilon=" << config.simplifyEpsilon << "\n";
    file << "useColonization=" << (config.useColonization ? "1" : "0") << "\n";

    return true;
}

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
              << "  --use-colonization          Use space colonization for network topology\n"
              << "  --help                      Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  roads.geojson       Road network data in GeoJSON format\n"
              << "  roads_debug.png     Debug visualization of road network\n"
              << "  roads.svg           SVG visualization of roads\n"
              << "  network.svg         SVG of network topology (if --use-colonization)\n"
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
            Settlement settlement{};  // Zero-initialize all fields
            settlement.id = s["id"].get<uint32_t>();

            std::string typeStr = s["type"].get<std::string>();
            if (typeStr == "Town") settlement.type = SettlementType::Town;
            else if (typeStr == "Village") settlement.type = SettlementType::Village;
            else if (typeStr == "FishingVillage") settlement.type = SettlementType::FishingVillage;
            else settlement.type = SettlementType::Hamlet;

            settlement.position.x = s["x"].get<float>();
            settlement.position.y = s["z"].get<float>(); // Note: stored as z in JSON

            if (s.contains("radius")) {
                settlement.radius = s["radius"].get<float>();
            } else {
                // Default radii based on settlement type
                switch (settlement.type) {
                    case SettlementType::Town: settlement.radius = 200.0f; break;
                    case SettlementType::Village: settlement.radius = 100.0f; break;
                    case SettlementType::FishingVillage: settlement.radius = 80.0f; break;
                    default: settlement.radius = 50.0f; break;
                }
            }

            if (s.contains("score")) {
                settlement.score = s["score"].get<float>();
            } else {
                settlement.score = 0.0f;
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

bool saveRoadsGeoJson(const std::string& path, const RoadGen::RoadNetwork& network) {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create roads GeoJSON file: %s", path.c_str());
        return false;
    }

    json featureCollection;
    featureCollection["type"] = "FeatureCollection";
    featureCollection["properties"] = {
        {"terrain_size", network.terrainSize},
        {"total_length_m", network.getTotalLength()}
    };

    json features = json::array();

    for (const auto& road : network.roads) {
        json coordinates = json::array();
        for (const auto& cp : road.controlPoints) {
            // GeoJSON format: [x, z] (2D) or [x, z, y] for 3D
            coordinates.push_back({cp.position.x, cp.position.y});
        }

        json feature;
        feature["type"] = "Feature";
        feature["geometry"] = {
            {"type", "LineString"},
            {"coordinates", coordinates}
        };
        feature["properties"] = {
            {"type", RoadGen::getRoadTypeName(road.type)},
            {"from_settlement", road.fromSettlementId},
            {"to_settlement", road.toSettlementId},
            {"length_m", road.getLength()},
            {"width", RoadGen::getRoadWidth(road.type)}
        };

        features.push_back(feature);
    }

    featureCollection["features"] = features;

    file << featureCollection.dump(2);

    SDL_Log("Saved roads GeoJSON: %s", path.c_str());
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

    bool useColonization = false;

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
        } else if (arg == "--use-colonization") {
            useColonization = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Check if outputs are up to date - skip processing if unchanged
    RoadBuildConfig buildConfig;
    buildConfig.heightmapPath = heightmapPath;
    buildConfig.biomemapPath = biomemapPath;
    buildConfig.settlementsPath = settlementsPath;
    buildConfig.outputDir = outputDir;
    buildConfig.terrainSize = config.terrainSize;
    buildConfig.minAltitude = config.minAltitude;
    buildConfig.maxAltitude = config.maxAltitude;
    buildConfig.gridResolution = config.gridResolution;
    buildConfig.simplifyEpsilon = config.simplifyEpsilon;
    buildConfig.useColonization = useColonization;

    if (isRoadOutputUpToDate(buildConfig)) {
        SDL_Log("Roads outputs up to date - skipping");
        return 0;
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

    // Space colonization for network topology (optional)
    RoadGen::ColonizationResult colonizationResult;

    if (useColonization) {
        SDL_Log("Building network topology with space colonization...");

        // Separate towns (roots) from other settlements (attractions)
        std::vector<glm::vec2> attractionPoints;
        std::vector<glm::vec2> rootPoints;
        std::vector<uint32_t> attractionIds;
        std::vector<uint32_t> rootIds;

        for (const auto& s : settlements) {
            if (s.type == SettlementType::Town) {
                rootPoints.push_back(s.position);
                rootIds.push_back(s.id);
            }
            // All settlements are also attraction points
            attractionPoints.push_back(s.position);
            attractionIds.push_back(s.id);
        }

        // If no towns, use villages as roots
        if (rootPoints.empty()) {
            for (const auto& s : settlements) {
                if (s.type == SettlementType::Village) {
                    rootPoints.push_back(s.position);
                    rootIds.push_back(s.id);
                }
            }
        }

        // If still no roots, use the highest scoring settlement
        if (rootPoints.empty() && !settlements.empty()) {
            const Settlement* best = &settlements[0];
            for (const auto& s : settlements) {
                if (s.score > best->score) best = &s;
            }
            rootPoints.push_back(best->position);
            rootIds.push_back(best->id);
        }

        RoadGen::ColonizationConfig colonConfig;
        colonConfig.attractionRadius = config.terrainSize * 0.5f;
        colonConfig.killRadius = 150.0f;  // Close enough to settlement
        colonConfig.branchLength = 300.0f;

        RoadGen::SpaceColonization colonizer;
        colonizer.buildNetwork(
            attractionPoints, rootPoints,
            attractionIds, rootIds,
            colonConfig, colonizationResult,
            [](float progress, const std::string& status) {
                SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
            }
        );

        // Save network topology SVG
        std::string networkSvgPath = outputDir + "/network.svg";
        RoadGen::writeNetworkSVG(networkSvgPath, colonizationResult, settlements, config.terrainSize);
    }

    // Generate road network with A* pathfinding
    SDL_Log("Generating road paths with A* pathfinding...");

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
    std::string geojsonPath = outputDir + "/roads.geojson";
    std::string debugPath = outputDir + "/roads_debug.png";
    std::string svgPath = outputDir + "/roads.svg";

    if (!saveRoadsGeoJson(geojsonPath, network)) {
        return 1;
    }

    if (!saveDebugVisualization(debugPath, network)) {
        return 1;
    }

    // Save roads SVG
    RoadGen::writeRoadsSVG(svgPath, network, settlements);

    // Save build stamp for future runs
    buildConfig.terrainSize = config.terrainSize;  // May have been updated from settlements
    saveRoadBuildStamp(buildConfig);

    SDL_Log("Road generation complete!");
    SDL_Log("Output files:");
    SDL_Log("  %s", geojsonPath.c_str());
    SDL_Log("  %s", debugPath.c_str());
    SDL_Log("  %s", svgPath.c_str());
    if (useColonization) {
        SDL_Log("  %s/network.svg", outputDir.c_str());
    }

    return 0;
}
