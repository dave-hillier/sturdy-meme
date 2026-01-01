// Standalone biome map preprocessing tool
// Generates biome classification from heightmap and erosion data

#include "BiomeGenerator.h"
#include "SettlementSVG.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>

namespace fs = std::filesystem;

// Check if outputs are up to date based on inputs and config
bool isBiomeOutputUpToDate(const BiomeConfig& config) {
    std::string metaPath = config.outputDir + "/biome.meta";
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::string cachedHeightmapPath;
    uintmax_t cachedHeightmapSize = 0;
    std::string cachedErosionDir;
    float cachedSeaLevel = 0;
    float cachedTerrainSize = 0;
    float cachedMinAltitude = 0;
    float cachedMaxAltitude = 0;
    uint32_t cachedOutputResolution = 0;
    uint32_t cachedNumSettlements = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "heightmap") cachedHeightmapPath = value;
            else if (key == "heightmapSize") cachedHeightmapSize = std::stoull(value);
            else if (key == "erosionDir") cachedErosionDir = value;
            else if (key == "seaLevel") cachedSeaLevel = std::stof(value);
            else if (key == "terrainSize") cachedTerrainSize = std::stof(value);
            else if (key == "minAltitude") cachedMinAltitude = std::stof(value);
            else if (key == "maxAltitude") cachedMaxAltitude = std::stof(value);
            else if (key == "outputResolution") cachedOutputResolution = std::stoul(value);
            else if (key == "numSettlements") cachedNumSettlements = std::stoul(value);
        }
    }

    // Check heightmap file exists and size matches
    std::error_code ec;
    if (!fs::exists(config.heightmapPath, ec)) {
        return false;
    }

    uintmax_t currentHeightmapSize = fs::file_size(config.heightmapPath, ec);
    if (ec || currentHeightmapSize != cachedHeightmapSize) {
        SDL_Log("Biome: heightmap file size changed, reprocessing");
        return false;
    }

    // Check erosion input files exist (flow_accumulation.bin, flow_direction.bin)
    std::string flowAccPath = config.erosionCacheDir + "/flow_accumulation.bin";
    std::string flowDirPath = config.erosionCacheDir + "/flow_direction.bin";
    if (!fs::exists(flowAccPath, ec) || !fs::exists(flowDirPath, ec)) {
        SDL_Log("Biome: erosion input files missing, reprocessing");
        return false;
    }

    // Check config matches
    if (std::abs(cachedSeaLevel - config.seaLevel) > 0.01f ||
        std::abs(cachedTerrainSize - config.terrainSize) > 0.1f ||
        std::abs(cachedMinAltitude - config.minAltitude) > 0.01f ||
        std::abs(cachedMaxAltitude - config.maxAltitude) > 0.01f ||
        cachedOutputResolution != config.outputResolution ||
        cachedNumSettlements != config.numSettlements) {
        SDL_Log("Biome: configuration changed, reprocessing");
        return false;
    }

    // Check all output files exist
    std::vector<std::string> outputs = {
        "biome_map.png", "biome_debug.png", "settlements.json", "settlements.svg"
    };
    for (const auto& output : outputs) {
        std::string path = config.outputDir + "/" + output;
        if (!fs::exists(path, ec)) {
            SDL_Log("Biome: missing output %s, reprocessing", output.c_str());
            return false;
        }
    }

    return true;
}

bool saveBiomeBuildStamp(const BiomeConfig& config) {
    std::string metaPath = config.outputDir + "/biome.meta";
    std::ofstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    std::error_code ec;
    uintmax_t heightmapSize = fs::file_size(config.heightmapPath, ec);
    if (ec) {
        return false;
    }

    file << "heightmap=" << config.heightmapPath << "\n";
    file << "heightmapSize=" << heightmapSize << "\n";
    file << "erosionDir=" << config.erosionCacheDir << "\n";
    file << "seaLevel=" << config.seaLevel << "\n";
    file << "terrainSize=" << config.terrainSize << "\n";
    file << "minAltitude=" << config.minAltitude << "\n";
    file << "maxAltitude=" << config.maxAltitude << "\n";
    file << "outputResolution=" << config.outputResolution << "\n";
    file << "numSettlements=" << config.numSettlements << "\n";

    return true;
}

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
              << "  settlements.svg    SVG visualization of settlement data\n"
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

    // Check if outputs are up to date - skip processing if unchanged
    if (isBiomeOutputUpToDate(config)) {
        SDL_Log("Biome outputs up to date - skipping");
        return 0;
    }

    // Create output directory if it doesn't exist
    fs::create_directories(config.outputDir);

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
    std::string settlementsSvgPath = config.outputDir + "/settlements.svg";

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

    // Save settlement SVG visualization
    writeSettlementsSVG(
        settlementsSvgPath,
        generator.getResult().settlements,
        config.terrainSize
    );

    // Save build stamp for future runs
    saveBiomeBuildStamp(config);

    SDL_Log("Biome generation complete!");
    SDL_Log("Output files:");
    SDL_Log("  %s", biomeMapPath.c_str());
    SDL_Log("  %s", debugPath.c_str());
    SDL_Log("  %s", settlementsPath.c_str());
    SDL_Log("  %s", settlementsSvgPath.c_str());

    return 0;
}
