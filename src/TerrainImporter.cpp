#include <stb_image.h>
#include "TerrainImporter.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

TerrainImporter::~TerrainImporter() {
    sourceData.clear();
    lodData.clear();
}

std::string TerrainImporter::getTilePath(const std::string& cacheDir, int32_t x, int32_t z, uint32_t lod) {
    std::ostringstream oss;
    oss << cacheDir << "/tile_" << x << "_" << z << "_lod" << lod << ".raw";
    return oss.str();
}

std::string TerrainImporter::getMetadataPath(const std::string& cacheDir) {
    return cacheDir + "/terrain_cache.meta";
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
        return false;
    }

    std::string line;
    std::string cachedSourcePath;
    float cachedMinAlt = 0, cachedMaxAlt = 0, cachedMpp = 0;
    uint32_t cachedTileRes = 0, cachedLODLevels = 0;

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
        }
    }

    // Validate config matches
    if (cachedSourcePath != config.sourceHeightmapPath) return false;
    if (std::abs(cachedMinAlt - config.minAltitude) > 0.01f) return false;
    if (std::abs(cachedMaxAlt - config.maxAltitude) > 0.01f) return false;
    if (std::abs(cachedMpp - config.metersPerPixel) > 0.001f) return false;
    if (cachedTileRes != config.tileResolution) return false;
    if (cachedLODLevels != config.numLODLevels) return false;

    // Check source file modification time vs cache
    if (!fs::exists(config.sourceHeightmapPath)) return false;

    auto sourceTime = fs::last_write_time(config.sourceHeightmapPath);
    auto cacheTime = fs::last_write_time(metaPath);

    if (sourceTime > cacheTime) {
        return false;  // Source is newer than cache
    }

    return true;
}

bool TerrainImporter::saveMetadata(const TerrainImportConfig& config) const {
    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ofstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    file << "source=" << config.sourceHeightmapPath << "\n";
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
        std::cerr << "Failed to load heightmap: " << path << " - " << stbi_failure_reason() << std::endl;
        return false;
    }

    sourceWidth = static_cast<uint32_t>(width);
    sourceHeight = static_cast<uint32_t>(height);

    // Copy data to our vector
    size_t pixelCount = sourceWidth * sourceHeight;
    sourceData.resize(pixelCount);
    std::memcpy(sourceData.data(), data, pixelCount * sizeof(uint16_t));

    stbi_image_free(data);

    std::cout << "Loaded heightmap: " << sourceWidth << "x" << sourceHeight << " pixels" << std::endl;
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

    // Calculate world dimensions
    worldWidth = sourceWidth * config.metersPerPixel;
    worldHeight = sourceHeight * config.metersPerPixel;

    // Calculate tiles for LOD 0 based on pixel dimensions
    // Each tile is exactly tileResolution x tileResolution pixels
    tilesX = (sourceWidth + config.tileResolution - 1) / config.tileResolution;
    tilesZ = (sourceHeight + config.tileResolution - 1) / config.tileResolution;

    std::cout << "Source: " << sourceWidth << "x" << sourceHeight << " pixels" << std::endl;
    std::cout << "World size: " << worldWidth << "m x " << worldHeight << "m" << std::endl;
    std::cout << "LOD 0: " << tilesX << "x" << tilesZ << " tiles (" << config.tileResolution << "x" << config.tileResolution << " each)" << std::endl;

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
        std::cerr << "Failed to save cache metadata" << std::endl;
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

    for (uint32_t y = 0; y < newHeight; y++) {
        for (uint32_t x = 0; x < newWidth; x++) {
            // Box filter (2x2 average)
            uint32_t srcX = x * 2;
            uint32_t srcY = y * 2;

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
    }

    lodData = std::move(newData);
    lodWidth = newWidth;
    lodHeight = newHeight;

    std::cout << "Downsampled to " << lodWidth << "x" << lodHeight << " for LOD " << lod << std::endl;
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
    uint32_t processedTiles = 0;

    std::cout << "LOD " << lod << ": " << numTilesX << "x" << numTilesZ << " tiles from "
              << lodWidth << "x" << lodHeight << " source" << std::endl;

    std::vector<uint16_t> tileData(tileRes * tileRes);

    for (uint32_t tz = 0; tz < numTilesZ; tz++) {
        for (uint32_t tx = 0; tx < numTilesX; tx++) {
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
                std::cerr << "Failed to save tile: " << tilePath << std::endl;
                return false;
            }

            processedTiles++;

            if (progressCallback && (processedTiles % 10 == 0 || processedTiles == totalTiles)) {
                float progress = progressBase + progressRange * (static_cast<float>(processedTiles) / totalTiles);
                std::ostringstream oss;
                oss << "LOD " << lod << ": " << processedTiles << "/" << totalTiles << " tiles";
                progressCallback(progress, oss.str());
            }
        }
    }

    return true;
}

bool TerrainImporter::saveTile(const std::string& path, const std::vector<uint16_t>& data, uint32_t resolution) {
    // Save as raw binary for 16-bit precision (stb_image_write doesn't support 16-bit PNG)
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Write resolution header
    file.write(reinterpret_cast<const char*>(&resolution), sizeof(resolution));
    file.write(reinterpret_cast<const char*>(&resolution), sizeof(resolution));

    // Write height data (16-bit per pixel)
    file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint16_t));

    return file.good();
}
