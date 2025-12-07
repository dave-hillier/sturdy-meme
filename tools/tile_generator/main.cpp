#include "TileCompositor.h"
#include <lodepng.h>
#include <iostream>
#include <string>
#include <cstring>
#include <SDL3/SDL_log.h>

void printUsage(const char* programName) {
    SDL_Log("Virtual Texture Tile Generator");
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log("");
    SDL_Log("Required options:");
    SDL_Log("  --heightmap <path>    Path to 16-bit heightmap PNG");
    SDL_Log("  --biomemap <path>     Path to biome zone map PNG");
    SDL_Log("  --output <dir>        Output directory for tiles");
    SDL_Log("");
    SDL_Log("Optional options:");
    SDL_Log("  --materials <path>    Base path for material textures (default: assets/textures/terrain)");
    SDL_Log("  --roads <path>        Path to roads.json file");
    SDL_Log("  --terrain-size <f>    Terrain size in meters (default: 16384)");
    SDL_Log("  --tile-res <n>        Tile resolution in pixels (default: 128)");
    SDL_Log("  --tiles-per-axis <n>  Number of tiles per axis at mip 0 (default: 512)");
    SDL_Log("  --max-mip <n>         Maximum mip level (default: 9)");
    SDL_Log("  --single-mip <n>      Generate only a single mip level");
    SDL_Log("  --single-tile <x,y,m> Generate a single tile at x,y,mip level");
    SDL_Log("  --help                Show this help message");
}

struct GeneratorOptions {
    std::string heightmapPath;
    std::string biomemapPath;
    std::string outputDir;
    std::string materialsPath = "assets/textures/terrain";
    std::string roadsPath;

    float terrainSize = 16384.0f;
    uint32_t tileResolution = 128;
    uint32_t tilesPerAxis = 512;
    uint32_t maxMipLevels = 9;

    bool singleMip = false;
    uint32_t singleMipLevel = 0;

    bool singleTile = false;
    uint32_t singleTileX = 0;
    uint32_t singleTileY = 0;
    uint32_t singleTileMip = 0;
};

bool parseArguments(int argc, char* argv[], GeneratorOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return false; // Signal to show help
        }
        else if (arg == "--heightmap" && i + 1 < argc) {
            opts.heightmapPath = argv[++i];
        }
        else if (arg == "--biomemap" && i + 1 < argc) {
            opts.biomemapPath = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            opts.outputDir = argv[++i];
        }
        else if (arg == "--materials" && i + 1 < argc) {
            opts.materialsPath = argv[++i];
        }
        else if (arg == "--roads" && i + 1 < argc) {
            opts.roadsPath = argv[++i];
        }
        else if (arg == "--terrain-size" && i + 1 < argc) {
            opts.terrainSize = std::stof(argv[++i]);
        }
        else if (arg == "--tile-res" && i + 1 < argc) {
            opts.tileResolution = std::stoul(argv[++i]);
        }
        else if (arg == "--tiles-per-axis" && i + 1 < argc) {
            opts.tilesPerAxis = std::stoul(argv[++i]);
        }
        else if (arg == "--max-mip" && i + 1 < argc) {
            opts.maxMipLevels = std::stoul(argv[++i]);
        }
        else if (arg == "--single-mip" && i + 1 < argc) {
            opts.singleMip = true;
            opts.singleMipLevel = std::stoul(argv[++i]);
        }
        else if (arg == "--single-tile" && i + 1 < argc) {
            opts.singleTile = true;
            std::string coords = argv[++i];
            // Parse "x,y,mip" format
            size_t pos1 = coords.find(',');
            size_t pos2 = coords.find(',', pos1 + 1);
            if (pos1 != std::string::npos && pos2 != std::string::npos) {
                opts.singleTileX = std::stoul(coords.substr(0, pos1));
                opts.singleTileY = std::stoul(coords.substr(pos1 + 1, pos2 - pos1 - 1));
                opts.singleTileMip = std::stoul(coords.substr(pos2 + 1));
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --single-tile format. Expected x,y,mip");
                return false;
            }
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown argument: %s", arg.c_str());
            return false;
        }
    }

    // Validate required arguments
    if (opts.heightmapPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing required argument: --heightmap");
        return false;
    }
    if (opts.biomemapPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing required argument: --biomemap");
        return false;
    }
    if (opts.outputDir.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing required argument: --output");
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    GeneratorOptions opts;

    if (!parseArguments(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    SDL_Log("=== Virtual Texture Tile Generator ===");
    SDL_Log("Heightmap:      %s", opts.heightmapPath.c_str());
    SDL_Log("Biome map:      %s", opts.biomemapPath.c_str());
    SDL_Log("Output:         %s", opts.outputDir.c_str());
    SDL_Log("Materials:      %s", opts.materialsPath.c_str());
    SDL_Log("Terrain size:   %.1f m", opts.terrainSize);
    SDL_Log("Tile resolution: %u px", opts.tileResolution);
    SDL_Log("Tiles/axis:     %u", opts.tilesPerAxis);
    SDL_Log("Max mip levels: %u", opts.maxMipLevels);

    // Setup compositor config
    VirtualTexture::TileCompositorConfig config;
    config.terrainSize = opts.terrainSize;
    config.tileResolution = opts.tileResolution;
    config.tilesPerAxis = opts.tilesPerAxis;
    config.maxMipLevels = opts.maxMipLevels;

    // Create compositor
    VirtualTexture::TileCompositor compositor;
    compositor.init(config);
    compositor.setMaterialBasePath(opts.materialsPath);

    // Load data
    SDL_Log("");
    SDL_Log("Loading data...");

    if (!compositor.loadHeightmap(opts.heightmapPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap");
        return 1;
    }

    if (!compositor.loadBiomeMap(opts.biomemapPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load biome map");
        return 1;
    }

    if (!opts.roadsPath.empty()) {
        compositor.loadRoads(opts.roadsPath);
    }

    SDL_Log("");
    SDL_Log("Generating tiles...");

    // Progress callback
    auto progressCallback = [](float progress, const std::string& status) {
        static int lastPercent = -1;
        int percent = static_cast<int>(progress * 100);
        if (percent != lastPercent) {
            SDL_Log("[%3d%%] %s", percent, status.c_str());
            lastPercent = percent;
        }
    };

    bool success = false;

    if (opts.singleTile) {
        // Generate a single tile
        VirtualTexture::OutputTile tile;
        compositor.generateTile(opts.singleTileX, opts.singleTileY, opts.singleTileMip, tile);

        std::string filename = opts.outputDir + "/tile_" +
                               std::to_string(opts.singleTileX) + "_" +
                               std::to_string(opts.singleTileY) + "_mip" +
                               std::to_string(opts.singleTileMip) + ".png";

        unsigned error = lodepng::encode(filename, tile.pixels, tile.resolution, tile.resolution);
        if (error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save tile: %s", lodepng_error_text(error));
            return 1;
        }

        SDL_Log("Saved single tile to: %s", filename.c_str());
        success = true;
    }
    else if (opts.singleMip) {
        // Generate a single mip level
        success = compositor.generateMipLevel(opts.singleMipLevel, opts.outputDir, progressCallback);
    }
    else {
        // Generate all mip levels
        success = compositor.generateAllMips(opts.outputDir, progressCallback);
    }

    if (success) {
        SDL_Log("");
        SDL_Log("=== Generation complete ===");
        SDL_Log("Loaded %zu textures", compositor.getLoadedTextureCount());
        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Generation failed");
        return 1;
    }
}
