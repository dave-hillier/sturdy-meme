// Vegetation placement generator tool
// Uses Poisson disk sampling to generate natural-looking vegetation distributions
// Outputs tile-based JSON files for streaming/paging

#include "VegetationPlacer.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <filesystem>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <output_dir> [options]\n"
              << "\n"
              << "Generates vegetation placement data using Poisson disk sampling.\n"
              << "Results are saved as tile-based JSON files for efficient streaming.\n"
              << "\n"
              << "Arguments:\n"
              << "  output_dir           Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --biome-map <path>   PNG biome map for zone-aware placement\n"
              << "  --heightmap <path>   16-bit PNG heightmap for slope filtering\n"
              << "  --terrain-size <m>   World size in meters (default: 16384.0)\n"
              << "  --tile-size <m>      Tile size in meters (default: 256.0)\n"
              << "  --density <factor>   Global density multiplier (default: 1.0)\n"
              << "  --seed <value>       Random seed for reproducibility (default: 12345)\n"
              << "  --min-altitude <m>   Minimum altitude (default: 0.0)\n"
              << "  --max-altitude <m>   Maximum altitude (default: 200.0)\n"
              << "  --tree-spacing <m>   Minimum spacing between trees (default: 4.0)\n"
              << "  --bush-spacing <m>   Minimum spacing between bushes (default: 2.0)\n"
              << "  --rock-spacing <m>   Minimum spacing between rocks (default: 3.0)\n"
              << "  --no-svg             Disable SVG visualization output\n"
              << "  --svg-size <px>      SVG output size (default: 2048)\n"
              << "  --help               Show this help message\n"
              << "\n"
              << "Biome Densities (trees per hectare approx):\n"
              << "  Woodland:     ~100 trees/ha (dense forest)\n"
              << "  Grassland:    ~5 trees/ha (sparse, scattered)\n"
              << "  Wetland:      ~20 trees/ha (willows, alders)\n"
              << "  Agricultural: ~1 trees/ha (field margins only)\n"
              << "\n"
              << "Output files:\n"
              << "  vegetation_manifest.json    Tile listing and statistics\n"
              << "  tile_X_Z.json               Per-tile vegetation instances\n"
              << "  vegetation.svg              Optional visualization\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " ./vegetation --biome-map biome.png --density 1.5\n"
              << "\n"
              << "Instance JSON format:\n"
              << "  {\n"
              << "    \"position\": [x, z],\n"
              << "    \"rotation\": radians,\n"
              << "    \"scale\": float,\n"
              << "    \"type\": \"oak_large\",\n"
              << "    \"preset\": \"oak_large\",  // for trees only\n"
              << "    \"seed\": 12345\n"
              << "  }\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    VegetationGeneratorConfig config{};
    config.outputDir = argv[1];

    // Parse optional arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--biome-map" && i + 1 < argc) {
            config.biomemapPath = argv[++i];
        } else if (arg == "--heightmap" && i + 1 < argc) {
            config.heightmapPath = argv[++i];
        } else if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--tile-size" && i + 1 < argc) {
            config.tileSize = std::stof(argv[++i]);
        } else if (arg == "--density" && i + 1 < argc) {
            config.densityMultiplier = std::stof(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else if (arg == "--tree-spacing" && i + 1 < argc) {
            config.minTreeSpacing = std::stof(argv[++i]);
        } else if (arg == "--bush-spacing" && i + 1 < argc) {
            config.minBushSpacing = std::stof(argv[++i]);
        } else if (arg == "--rock-spacing" && i + 1 < argc) {
            config.minRockSpacing = std::stof(argv[++i]);
        } else if (arg == "--no-svg") {
            config.generateSVG = false;
        } else if (arg == "--svg-size" && i + 1 < argc) {
            config.svgSize = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(config.outputDir);

    SDL_Log("Vegetation Generator");
    SDL_Log("====================");
    SDL_Log("Output: %s", config.outputDir.c_str());
    SDL_Log("Terrain size: %.1f m", config.terrainSize);
    SDL_Log("Tile size: %.1f m", config.tileSize);
    SDL_Log("Density multiplier: %.2f", config.densityMultiplier);
    SDL_Log("Seed: %u", config.seed);
    if (!config.biomemapPath.empty()) {
        SDL_Log("Biome map: %s", config.biomemapPath.c_str());
    }
    if (!config.heightmapPath.empty()) {
        SDL_Log("Heightmap: %s", config.heightmapPath.c_str());
    }
    SDL_Log("Tree spacing: %.1f m", config.minTreeSpacing);

    VegetationPlacer placer;

    SDL_Log("Generating vegetation...");

    bool success = placer.generate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vegetation generation failed!");
        return 1;
    }

    // Save outputs
    std::string tilesDir = config.outputDir + "/tiles";
    std::string manifestPath = config.outputDir + "/vegetation_manifest.json";
    std::string svgPath = config.outputDir + "/vegetation.svg";

    if (!placer.saveTiles(tilesDir)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save tiles!");
        return 1;
    }

    if (!placer.saveManifest(manifestPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save manifest!");
        return 1;
    }

    if (config.generateSVG) {
        if (!placer.saveSVG(svgPath, config.svgSize)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save SVG!");
            return 1;
        }
    }

    const auto& stats = placer.getStatistics();
    SDL_Log("Vegetation generation complete!");
    SDL_Log("Generated %zu tiles", stats.tilesGenerated);
    SDL_Log("Total instances: %zu", placer.getTotalInstanceCount());
    SDL_Log("  Trees: %zu", stats.totalTrees);
    SDL_Log("  Bushes: %zu", stats.totalBushes);
    SDL_Log("  Rocks: %zu", stats.totalRocks);
    SDL_Log("  Detritus: %zu", stats.totalDetritus);
    SDL_Log("Output files:");
    SDL_Log("  %s", manifestPath.c_str());
    SDL_Log("  %s/*.json", tilesDir.c_str());
    if (config.generateSVG) {
        SDL_Log("  %s", svgPath.c_str());
    }

    return 0;
}
