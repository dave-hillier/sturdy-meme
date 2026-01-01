#include <stb_image.h>
#include <lodepng.h>
#include "TerrainImporter.h"
#include <SDL3/SDL_log.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include "../common/ParallelProgress.h"

namespace fs = std::filesystem;

TerrainImporter::~TerrainImporter() {
    sourceData.clear();
    lodData.clear();
}

std::string TerrainImporter::getTilePath(const std::string& cacheDir, int32_t x, int32_t z, uint32_t lod) {
    std::ostringstream oss;
    oss << cacheDir << "/tile_" << x << "_" << z << "_lod" << lod << ".png";
    return oss.str();
}

std::string TerrainImporter::getMetadataPath(const std::string& cacheDir) {
    return cacheDir + "/terrain_tiles.meta";
}

void TerrainImporter::getTileCountForLOD(uint32_t sourceWidth, uint32_t sourceHeight,
                                          uint32_t tileResolution, uint32_t lod,
                                          uint32_t& outTilesX, uint32_t& outTilesZ) {
    // Each LOD level has half the pixels of the previous
    uint32_t lodWidth = sourceWidth >> lod;
    uint32_t lodHeight = sourceHeight >> lod;

    // Minimum 1 pixel
    if (lodWidth < 1) lodWidth = 1;
    if (lodHeight < 1) lodHeight = 1;

    // Ceiling division to get tile count
    outTilesX = (lodWidth + tileResolution - 1) / tileResolution;
    outTilesZ = (lodHeight + tileResolution - 1) / tileResolution;
}

bool TerrainImporter::isCacheValid(const TerrainImportConfig& config) const {
    return loadAndValidateMetadata(config);
}

bool TerrainImporter::loadAndValidateMetadata(const TerrainImportConfig& config) const {
    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        SDL_Log("Terrain cache: metadata file not found at %s", metaPath.c_str());
        return false;
    }

    std::string line;
    std::string cachedSourcePath;
    float cachedMinAlt = 0, cachedMaxAlt = 0, cachedMpp = 0;
    uint32_t cachedTileRes = 0, cachedLODLevels = 0;
    uintmax_t cachedSourceSize = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "source") cachedSourcePath = value;
            else if (key == "minAltitude") cachedMinAlt = std::stof(value);
            else if (key == "maxAltitude") cachedMaxAlt = std::stof(value);
            else if (key == "metersPerPixel") cachedMpp = std::stof(value);
            else if (key == "tileResolution") cachedTileRes = std::stoul(value);
            else if (key == "numLODLevels") cachedLODLevels = std::stoul(value);
            else if (key == "sourceFileSize") cachedSourceSize = std::stoull(value);
        }
    }

    // Validate config matches - use canonical paths for comparison
    std::error_code ec;
    fs::path cachedCanonical = fs::canonical(cachedSourcePath, ec);
    if (ec) {
        SDL_Log("Terrain cache: cached source path invalid: %s", cachedSourcePath.c_str());
        return false;
    }
    fs::path configCanonical = fs::canonical(config.sourceHeightmapPath, ec);
    if (ec) {
        SDL_Log("Terrain cache: config source path invalid: %s", config.sourceHeightmapPath.c_str());
        return false;
    }
    if (cachedCanonical != configCanonical) {
        SDL_Log("Terrain cache: source path mismatch");
        SDL_Log("  Cached: %s", cachedCanonical.c_str());
        SDL_Log("  Config: %s", configCanonical.c_str());
        return false;
    }
    if (std::abs(cachedMinAlt - config.minAltitude) > 0.01f) {
        SDL_Log("Terrain cache: minAltitude mismatch (cached=%f, config=%f)", cachedMinAlt, config.minAltitude);
        return false;
    }
    if (std::abs(cachedMaxAlt - config.maxAltitude) > 0.01f) {
        SDL_Log("Terrain cache: maxAltitude mismatch (cached=%f, config=%f)", cachedMaxAlt, config.maxAltitude);
        return false;
    }
    if (std::abs(cachedMpp - config.metersPerPixel) > 0.001f) {
        SDL_Log("Terrain cache: metersPerPixel mismatch (cached=%f, config=%f)", cachedMpp, config.metersPerPixel);
        return false;
    }
    if (cachedTileRes != config.tileResolution) {
        SDL_Log("Terrain cache: tileResolution mismatch (cached=%u, config=%u)", cachedTileRes, config.tileResolution);
        return false;
    }
    if (cachedLODLevels != config.numLODLevels) {
        SDL_Log("Terrain cache: numLODLevels mismatch (cached=%u, config=%u)", cachedLODLevels, config.numLODLevels);
        return false;
    }

    // Check source file size to detect content changes (timestamps are unreliable with file copies)
    std::error_code sizeEc;
    uintmax_t currentSourceSize = fs::file_size(config.sourceHeightmapPath, sizeEc);
    if (sizeEc) {
        SDL_Log("Terrain cache: cannot read source file size");
        return false;
    }
    if (cachedSourceSize != currentSourceSize) {
        SDL_Log("Terrain cache: source file size changed (cached=%llu, current=%llu)",
                static_cast<unsigned long long>(cachedSourceSize),
                static_cast<unsigned long long>(currentSourceSize));
        return false;
    }

    SDL_Log("Terrain cache: valid cache found at %s", fs::canonical(config.cacheDirectory).c_str());
    return true;
}

bool TerrainImporter::saveMetadata(const TerrainImportConfig& config) const {
    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ofstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    // Get source file size for cache validation
    std::error_code ec;
    uintmax_t sourceFileSize = fs::file_size(config.sourceHeightmapPath, ec);
    if (ec) {
        SDL_Log("Terrain cache: cannot read source file size for metadata");
        return false;
    }

    file << "source=" << config.sourceHeightmapPath << "\n";
    file << "sourceFileSize=" << sourceFileSize << "\n";
    file << "minAltitude=" << config.minAltitude << "\n";
    file << "maxAltitude=" << config.maxAltitude << "\n";
    file << "metersPerPixel=" << config.metersPerPixel << "\n";
    file << "tileResolution=" << config.tileResolution << "\n";
    file << "numLODLevels=" << config.numLODLevels << "\n";
    file << "sourceWidth=" << sourceWidth << "\n";
    file << "sourceHeight=" << sourceHeight << "\n";
    file << "tilesX=" << tilesX << "\n";
    file << "tilesZ=" << tilesZ << "\n";

    return true;
}

bool TerrainImporter::loadSourceHeightmap(const std::string& path) {
    int width, height, channels;

    // Load as 16-bit
    uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s - %s", path.c_str(), stbi_failure_reason());
        return false;
    }

    sourceWidth = static_cast<uint32_t>(width);
    sourceHeight = static_cast<uint32_t>(height);

    // Copy data to our vector
    size_t pixelCount = sourceWidth * sourceHeight;
    sourceData.resize(pixelCount);
    std::memcpy(sourceData.data(), data, pixelCount * sizeof(uint16_t));

    stbi_image_free(data);

    SDL_Log("Loaded heightmap: %ux%u pixels", sourceWidth, sourceHeight);
    return true;
}

bool TerrainImporter::import(const TerrainImportConfig& config, ImportProgressCallback progressCallback) {
    if (progressCallback) {
        progressCallback(0.0f, "Loading source heightmap...");
    }

    // Load source heightmap
    if (!loadSourceHeightmap(config.sourceHeightmapPath)) {
        return false;
    }

    // Create cache directory
    fs::create_directories(config.cacheDirectory);
    SDL_Log("Terrain cache: writing tiles to %s", fs::canonical(config.cacheDirectory).c_str());

    // Calculate world dimensions
    worldWidth = sourceWidth * config.metersPerPixel;
    worldHeight = sourceHeight * config.metersPerPixel;

    // Calculate tiles for LOD 0 based on pixel dimensions
    // Each tile is exactly tileResolution x tileResolution pixels
    tilesX = (sourceWidth + config.tileResolution - 1) / config.tileResolution;
    tilesZ = (sourceHeight + config.tileResolution - 1) / config.tileResolution;

    SDL_Log("Source: %ux%u pixels", sourceWidth, sourceHeight);
    SDL_Log("World size: %.1fm x %.1fm", worldWidth, worldHeight);
    SDL_Log("LOD 0: %ux%u tiles (%ux%u each)", tilesX, tilesZ, config.tileResolution, config.tileResolution);

    // Initialize LOD data with source
    lodData = sourceData;
    lodWidth = sourceWidth;
    lodHeight = sourceHeight;

    // Generate tiles for each LOD level
    float progressPerLOD = 0.9f / config.numLODLevels;

    for (uint32_t lod = 0; lod < config.numLODLevels; lod++) {
        float progressBase = 0.05f + lod * progressPerLOD;

        if (progressCallback) {
            std::ostringstream oss;
            oss << "Generating LOD " << lod << " tiles...";
            progressCallback(progressBase, oss.str());
        }

        if (!generateLODLevel(config, lod, progressCallback, progressBase, progressPerLOD)) {
            return false;
        }

        // Downsample for next LOD level
        if (lod + 1 < config.numLODLevels) {
            downsampleForLOD(lod + 1);
        }
    }

    // Save metadata
    if (!saveMetadata(config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save cache metadata");
        return false;
    }

    if (progressCallback) {
        progressCallback(1.0f, "Import complete!");
    }

    return true;
}

void TerrainImporter::downsampleForLOD(uint32_t lod) {
    // Each LOD level is half resolution of previous
    uint32_t newWidth = lodWidth / 2;
    uint32_t newHeight = lodHeight / 2;

    if (newWidth < 1) newWidth = 1;
    if (newHeight < 1) newHeight = 1;

    std::vector<uint16_t> newData(newWidth * newHeight);

    // Parallelize by rows
    ParallelProgress::parallel_for(0, static_cast<int>(newHeight), [&](int y) {
        for (uint32_t x = 0; x < newWidth; x++) {
            // Box filter (2x2 average)
            uint32_t srcX = x * 2;
            uint32_t srcY = static_cast<uint32_t>(y) * 2;

            uint32_t sum = 0;
            uint32_t count = 0;

            for (uint32_t dy = 0; dy < 2 && srcY + dy < lodHeight; dy++) {
                for (uint32_t dx = 0; dx < 2 && srcX + dx < lodWidth; dx++) {
                    sum += lodData[(srcY + dy) * lodWidth + (srcX + dx)];
                    count++;
                }
            }

            newData[y * newWidth + x] = static_cast<uint16_t>(sum / count);
        }
    });

    lodData = std::move(newData);
    lodWidth = newWidth;
    lodHeight = newHeight;

    SDL_Log("Downsampled to %ux%u for LOD %u", lodWidth, lodHeight, lod);
}

bool TerrainImporter::generateLODLevel(const TerrainImportConfig& config, uint32_t lod,
                                        ImportProgressCallback progressCallback,
                                        float progressBase, float progressRange) {
    uint32_t tileRes = config.tileResolution;

    // Calculate number of tiles based on current LOD source dimensions
    // Each tile is exactly tileRes x tileRes pixels extracted from lodData
    uint32_t numTilesX = (lodWidth + tileRes - 1) / tileRes;  // Ceiling division
    uint32_t numTilesZ = (lodHeight + tileRes - 1) / tileRes;

    uint32_t totalTiles = numTilesX * numTilesZ;
    std::atomic<uint32_t> processedTiles{0};
    std::atomic<bool> hasError{false};

    SDL_Log("LOD %u: %ux%u tiles from %ux%u source (%u threads)",
            lod, numTilesX, numTilesZ, lodWidth, lodHeight, ParallelProgress::getThreadCount());

    // Parallel tile generation
    ParallelProgress::parallel_for(0, static_cast<int>(totalTiles), [&](int tileIndex) {
        if (hasError.load()) return;  // Early exit on error

        uint32_t tx = tileIndex % numTilesX;
        uint32_t tz = tileIndex / numTilesX;

        // Each thread has its own tile buffer
        std::vector<uint16_t> tileData(tileRes * tileRes);

        // Source pixel start for this tile
        uint32_t srcStartX = tx * tileRes;
        uint32_t srcStartZ = tz * tileRes;

        // Extract pixels directly - no resampling
        for (uint32_t py = 0; py < tileRes; py++) {
            for (uint32_t px = 0; px < tileRes; px++) {
                uint32_t srcX = srcStartX + px;
                uint32_t srcZ = srcStartZ + py;

                // Clamp to source bounds for edge tiles
                srcX = std::min(srcX, lodWidth - 1);
                srcZ = std::min(srcZ, lodHeight - 1);

                tileData[py * tileRes + px] = lodData[srcZ * lodWidth + srcX];
            }
        }

        // Save tile
        std::string tilePath = getTilePath(config.cacheDirectory, tx, tz, lod);
        if (!saveTile(tilePath, tileData, tileRes)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save tile: %s", tilePath.c_str());
            hasError.store(true);
            return;
        }

        uint32_t completed = ++processedTiles;

        // Report progress periodically
        uint32_t reportInterval = std::max(1u, totalTiles / 20);
        if (progressCallback && (completed % reportInterval == 0 || completed == totalTiles)) {
            float progress = progressBase + progressRange * (static_cast<float>(completed) / totalTiles);
            std::ostringstream oss;
            oss << "LOD " << lod << ": " << completed << "/" << totalTiles << " tiles";
            progressCallback(progress, oss.str());
        }
    });

    return !hasError.load();
}

bool TerrainImporter::saveTile(const std::string& path, const std::vector<uint16_t>& data, uint32_t resolution) {
    // Convert to big-endian for PNG format (PNG uses network byte order)
    std::vector<unsigned char> pngData(data.size() * 2);
    for (size_t i = 0; i < data.size(); i++) {
        uint16_t value = data[i];
        pngData[i * 2] = static_cast<unsigned char>((value >> 8) & 0xFF);     // High byte first
        pngData[i * 2 + 1] = static_cast<unsigned char>(value & 0xFF);        // Low byte second
    }

    // Encode as 16-bit grayscale PNG
    std::vector<unsigned char> png;
    unsigned error = lodepng::encode(png, pngData, resolution, resolution, LCT_GREY, 16);

    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PNG encoding error %u: %s", error, lodepng_error_text(error));
        return false;
    }

    // Write PNG file
    error = lodepng::save_file(png, path);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save PNG file: %s", lodepng_error_text(error));
        return false;
    }

    return true;
}
