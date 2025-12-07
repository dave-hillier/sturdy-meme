// Standalone terrain tile preprocessing tool
// Generates tile cache from a 16-bit PNG heightmap

#include "TerrainImporter.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <cstdlib>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <cache_directory> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --min-altitude <value>     Altitude in meters for height value 0 (default: 0.0)\n"
              << "  --max-altitude <value>     Altitude in meters for height value 65535 (default: 200.0)\n"
              << "  --meters-per-pixel <value> World scale in meters per pixel (default: 1.0)\n"
              << "  --tile-resolution <value>  Output tile resolution in pixels (default: 512)\n"
              << "  --lod-levels <value>       Number of LOD levels to generate (default: 4)\n"
              << "  --help                     Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./terrain_cache --min-altitude -15 --max-altitude 220\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    TerrainImportConfig config{};
    config.sourceHeightmapPath = argv[1];
    config.cacheDirectory = argv[2];
    config.minAltitude = 0.0f;
    config.maxAltitude = 200.0f;
    config.metersPerPixel = 1.0f;
    config.tileResolution = 512;
    config.numLODLevels = 4;

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else if (arg == "--meters-per-pixel" && i + 1 < argc) {
            config.metersPerPixel = std::stof(argv[++i]);
        } else if (arg == "--tile-resolution" && i + 1 < argc) {
            config.tileResolution = std::stoul(argv[++i]);
        } else if (arg == "--lod-levels" && i + 1 < argc) {
            config.numLODLevels = std::stoul(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Terrain Tile Preprocessor");
    SDL_Log("=========================");
    SDL_Log("Source: %s", config.sourceHeightmapPath.c_str());
    SDL_Log("Cache: %s", config.cacheDirectory.c_str());
    SDL_Log("Altitude range: %.1f to %.1f meters", config.minAltitude, config.maxAltitude);
    SDL_Log("Meters per pixel: %.2f", config.metersPerPixel);
    SDL_Log("Tile resolution: %u", config.tileResolution);
    SDL_Log("LOD levels: %u", config.numLODLevels);

    TerrainImporter importer;

    SDL_Log("Importing terrain heightmap...");

    bool success = importer.import(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (success) {
        SDL_Log("Import complete!");
        SDL_Log("Tiles: %u x %u", importer.getTilesX(), importer.getTilesZ());
        SDL_Log("World size: %.1f x %.1f meters", importer.getWorldWidth(), importer.getWorldHeight());
        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Import failed!");
        return 1;
    }
}
