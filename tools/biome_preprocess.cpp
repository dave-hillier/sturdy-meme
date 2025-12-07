// Standalone biome map preprocessing tool
// Generates biome classification from heightmap and erosion data

#include "BiomeGenerator.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <erosion_cache> <output_dir> [options]\n"
              << "\n"
              << "Generates biome classification map for south coast of England terrain.\n"
              << "\n"
              << "Arguments:\n"
              << "  heightmap.png    16-bit PNG heightmap file\n"
              << "  erosion_cache    Directory containing erosion data (from erosion_preprocess)\n"
              << "  output_dir       Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --sea-level <value>         Height below which is sea (default: 0.0)\n"
              << "  --terrain-size <value>      World size in meters (default: 16384.0)\n"
              << "  --min-altitude <value>      Min altitude in heightmap (default: 0.0)\n"
              << "  --max-altitude <value>      Max altitude in heightmap (default: 200.0)\n"
              << "  --output-resolution <value> Biome map resolution (default: 1024)\n"
              << "  --num-settlements <value>   Target number of settlements (default: 20)\n"
              << "  --help                      Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  biome_map.png      RGBA8 biome data (R=zone, G=subzone, B=settlement_dist)\n"
              << "  biome_debug.png    Colored visualization of biome zones\n"
              << "  settlements.json   Settlement locations and metadata\n"
              << "\n"
              << "Biome zones (south coast of England):\n"
              << "  0: Sea            - Below sea level\n"
              << "  1: Beach          - Low coastal, gentle slope\n"
              << "  2: Chalk Cliff    - Steep coastal slopes\n"
              << "  3: Salt Marsh     - Low-lying coastal wetland\n"
              << "  4: River          - River channels\n"
              << "  5: Wetland        - Inland wet areas near rivers\n"
              << "  6: Grassland      - Chalk downs, higher elevation\n"
              << "  7: Agricultural   - Flat lowland fields\n"
              << "  8: Woodland       - Valleys and sheltered slopes\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./erosion_cache ./biome_cache --sea-level 23\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    BiomeConfig config{};
    config.heightmapPath = argv[1];
    config.erosionCacheDir = argv[2];
    config.outputDir = argv[3];
    config.seaLevel = 0.0f;
    config.terrainSize = 16384.0f;
    config.minAltitude = 0.0f;
    config.maxAltitude = 200.0f;
    config.outputResolution = 1024;
    config.numSettlements = 20;

    // Parse optional arguments
    for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--sea-level" && i + 1 < argc) {
            config.seaLevel = std::stof(argv[++i]);
        } else if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else if (arg == "--output-resolution" && i + 1 < argc) {
            config.outputResolution = std::stoul(argv[++i]);
        } else if (arg == "--num-settlements" && i + 1 < argc) {
            config.numSettlements = std::stoul(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(config.outputDir);

    SDL_Log("Biome Map Preprocessor");
    SDL_Log("======================");
    SDL_Log("Heightmap: %s", config.heightmapPath.c_str());
    SDL_Log("Erosion cache: %s", config.erosionCacheDir.c_str());
    SDL_Log("Output: %s", config.outputDir.c_str());
    SDL_Log("Sea level: %.1f m", config.seaLevel);
    SDL_Log("Terrain size: %.1f m", config.terrainSize);
    SDL_Log("Altitude range: %.1f to %.1f m", config.minAltitude, config.maxAltitude);
    SDL_Log("Output resolution: %u x %u", config.outputResolution, config.outputResolution);
    SDL_Log("Target settlements: %u", config.numSettlements);

    BiomeGenerator generator;

    SDL_Log("Generating biome map...");

    bool success = generator.generate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Biome generation failed!");
        return 1;
    }

    // Save outputs
    std::string biomeMapPath = config.outputDir + "/biome_map.png";
    std::string debugPath = config.outputDir + "/biome_debug.png";
    std::string settlementsPath = config.outputDir + "/settlements.json";

    if (!generator.saveBiomeMap(biomeMapPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save biome map!");
        return 1;
    }

    if (!generator.saveDebugVisualization(debugPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save debug visualization!");
        return 1;
    }

    if (!generator.saveSettlements(settlementsPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save settlements!");
        return 1;
    }

    SDL_Log("Biome generation complete!");
    SDL_Log("Output files:");
    SDL_Log("  %s", biomeMapPath.c_str());
    SDL_Log("  %s", debugPath.c_str());
    SDL_Log("  %s", settlementsPath.c_str());

    return 0;
}
