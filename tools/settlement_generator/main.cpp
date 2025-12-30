// Standalone settlement generation tool
// Generates settlement locations from heightmap and erosion data

#include "SettlementGenerator.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <filesystem>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <erosion_cache> <output_dir> [options]\n"
              << "\n"
              << "Generates settlement locations for terrain based on geography.\n"
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
              << "  --num-settlements <value>   Target number of settlements (default: 20)\n"
              << "  --biome-map <path>          Optional biome map for zone-aware placement\n"
              << "  --svg-width <value>         SVG output width (default: 2048)\n"
              << "  --svg-height <value>        SVG output height (default: 2048)\n"
              << "  --help                      Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  settlements.json   Settlement locations and metadata\n"
              << "  settlements.svg    SVG visualization with perimeter shapes\n"
              << "\n"
              << "Settlement types:\n"
              << "  Town            - Large settlements with markets (score > 60)\n"
              << "  Village         - Medium settlements (score > 40)\n"
              << "  Fishing Village - Coastal settlements with harbours\n"
              << "  Hamlet          - Small rural settlements\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./erosion_cache ./settlements --num-settlements 30\n";
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

    SettlementConfig config{};
    config.heightmapPath = argv[1];
    config.erosionCacheDir = argv[2];
    config.outputDir = argv[3];

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
        } else if (arg == "--num-settlements" && i + 1 < argc) {
            config.numSettlements = std::stoul(argv[++i]);
        } else if (arg == "--biome-map" && i + 1 < argc) {
            config.biomeMapPath = argv[++i];
        } else if (arg == "--svg-width" && i + 1 < argc) {
            config.svgWidth = std::stoi(argv[++i]);
        } else if (arg == "--svg-height" && i + 1 < argc) {
            config.svgHeight = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(config.outputDir);

    SDL_Log("Settlement Generator");
    SDL_Log("====================");
    SDL_Log("Heightmap: %s", config.heightmapPath.c_str());
    SDL_Log("Erosion cache: %s", config.erosionCacheDir.c_str());
    SDL_Log("Output: %s", config.outputDir.c_str());
    SDL_Log("Sea level: %.1f m", config.seaLevel);
    SDL_Log("Terrain size: %.1f m", config.terrainSize);
    SDL_Log("Altitude range: %.1f to %.1f m", config.minAltitude, config.maxAltitude);
    SDL_Log("Target settlements: %u", config.numSettlements);
    if (!config.biomeMapPath.empty()) {
        SDL_Log("Biome map: %s", config.biomeMapPath.c_str());
    }

    SettlementGenerator generator;

    SDL_Log("Generating settlements...");

    bool success = generator.generate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settlement generation failed!");
        return 1;
    }

    // Save outputs
    std::string settlementsPath = config.outputDir + "/settlements.json";
    std::string svgPath = config.outputDir + "/settlements.svg";

    if (!generator.saveSettlements(settlementsPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save settlements!");
        return 1;
    }

    if (!generator.saveSettlementsSVG(svgPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save SVG!");
        return 1;
    }

    const auto& result = generator.getResult();
    SDL_Log("Settlement generation complete!");
    SDL_Log("Generated %zu settlements", result.settlements.size());
    SDL_Log("Output files:");
    SDL_Log("  %s", settlementsPath.c_str());
    SDL_Log("  %s", svgPath.c_str());

    return 0;
}
