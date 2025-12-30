#include "VegetationPlacer.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <filesystem>

using json = nlohmann::json;

// Poisson Disk implementation using Bridson's algorithm
void VegetationPlacer::PoissonDisk::init(float areaWidth, float areaHeight, float minDist) {
    minDistance = minDist;
    cellSize = minDist / std::sqrt(2.0f);
    gridWidth = static_cast<int>(std::ceil(areaWidth / cellSize));
    gridHeight = static_cast<int>(std::ceil(areaHeight / cellSize));
    grid.assign(gridWidth * gridHeight, -1);
    points.clear();
}

bool VegetationPlacer::PoissonDisk::addPoint(const glm::vec2& p) {
    if (!isValid(p)) return false;

    int idx = getGridIndex(p);
    if (idx < 0 || idx >= static_cast<int>(grid.size())) return false;

    grid[idx] = static_cast<int>(points.size());
    points.push_back(p);
    return true;
}

bool VegetationPlacer::PoissonDisk::isValid(const glm::vec2& p) const {
    int cellX = static_cast<int>(p.x / cellSize);
    int cellY = static_cast<int>(p.y / cellSize);

    // Check neighboring cells (5x5 neighborhood)
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int nx = cellX + dx;
            int ny = cellY + dy;

            if (nx < 0 || nx >= gridWidth || ny < 0 || ny >= gridHeight)
                continue;

            int idx = ny * gridWidth + nx;
            if (grid[idx] >= 0) {
                const glm::vec2& other = points[grid[idx]];
                float dist = glm::length(p - other);
                if (dist < minDistance) {
                    return false;
                }
            }
        }
    }
    return true;
}

int VegetationPlacer::PoissonDisk::getGridIndex(const glm::vec2& p) const {
    int cellX = static_cast<int>(p.x / cellSize);
    int cellY = static_cast<int>(p.y / cellSize);
    if (cellX < 0 || cellX >= gridWidth || cellY < 0 || cellY >= gridHeight)
        return -1;
    return cellY * gridWidth + cellX;
}

void VegetationPlacer::poissonDiskSample(
    const glm::vec2& boundsMin,
    const glm::vec2& boundsMax,
    float minDist,
    float density,
    std::mt19937& rng,
    std::vector<glm::vec2>& outPoints
) {
    float width = boundsMax.x - boundsMin.x;
    float height = boundsMax.y - boundsMin.y;
    float area = width * height;

    // Target number of points based on density
    int targetCount = static_cast<int>(area * density);
    if (targetCount <= 0) return;

    // Limit iterations for performance
    int maxAttempts = targetCount * 30;

    PoissonDisk disk;
    disk.init(width, height, minDist);

    std::uniform_real_distribution<float> distX(0.0f, width);
    std::uniform_real_distribution<float> distY(0.0f, height);
    std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> distRadius(minDist, minDist * 2.0f);

    // Initial random point
    glm::vec2 first(distX(rng), distY(rng));
    disk.addPoint(first);

    std::vector<int> activeList;
    activeList.push_back(0);

    int attempts = 0;
    const int k = 30;  // Candidates per point

    while (!activeList.empty() && disk.points.size() < static_cast<size_t>(targetCount)
           && attempts < maxAttempts) {
        attempts++;

        // Pick random active point
        std::uniform_int_distribution<int> distActive(0, static_cast<int>(activeList.size()) - 1);
        int activeIdx = distActive(rng);
        int pointIdx = activeList[activeIdx];
        const glm::vec2& point = disk.points[pointIdx];

        bool found = false;
        for (int i = 0; i < k; i++) {
            float angle = distAngle(rng);
            float radius = distRadius(rng);

            glm::vec2 candidate(
                point.x + radius * std::cos(angle),
                point.y + radius * std::sin(angle)
            );

            // Check bounds
            if (candidate.x < 0 || candidate.x >= width ||
                candidate.y < 0 || candidate.y >= height)
                continue;

            if (disk.addPoint(candidate)) {
                activeList.push_back(static_cast<int>(disk.points.size()) - 1);
                found = true;
                break;
            }
        }

        if (!found) {
            // Remove from active list
            activeList[activeIdx] = activeList.back();
            activeList.pop_back();
        }
    }

    // Convert to world coordinates
    outPoints.reserve(outPoints.size() + disk.points.size());
    for (const auto& p : disk.points) {
        outPoints.push_back(p + boundsMin);
    }
}

VegetationType VegetationPlacer::selectTreeType(BiomeZone biome, std::mt19937& rng) {
    const BiomeDensityConfig& density = getDensityForBiome(biome);

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng);

    float cumulative = 0.0f;
    cumulative += density.oakProbability;
    if (r < cumulative) return VegetationType::OakMedium;

    cumulative += density.ashProbability;
    if (r < cumulative) return VegetationType::AshMedium;

    cumulative += density.beechProbability;
    if (r < cumulative) return VegetationType::BeechMedium;

    return VegetationType::PineMedium;  // Pine as fallback
}

VegetationType VegetationPlacer::selectTreeSize(VegetationType baseType, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng);

    // Base type is the medium variant
    int base = static_cast<int>(baseType);

    // Map to species base (oak=0, ash=3, beech=6, pine=9)
    int speciesBase = (base / 3) * 3;

    // Size distribution: 30% small, 50% medium, 20% large
    if (r < 0.3f) {
        return static_cast<VegetationType>(speciesBase);      // Small
    } else if (r < 0.8f) {
        return static_cast<VegetationType>(speciesBase + 1);  // Medium
    } else {
        return static_cast<VegetationType>(speciesBase + 2);  // Large
    }
}

VegetationType VegetationPlacer::selectBushType(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 2);
    int bushIdx = dist(rng);
    return static_cast<VegetationType>(static_cast<int>(VegetationType::Bush1) + bushIdx);
}

VegetationType VegetationPlacer::selectDetritusType(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng);

    // Distribution of detritus types
    if (r < 0.4f) return VegetationType::FallenBranch;
    if (r < 0.6f) return VegetationType::Mushroom;
    if (r < 0.75f) return VegetationType::Fern;
    if (r < 0.85f) return VegetationType::Log;
    if (r < 0.95f) return VegetationType::Stump;
    return VegetationType::Bramble;
}

const BiomeDensityConfig& VegetationPlacer::getDensityForBiome(BiomeZone biome) const {
    switch (biome) {
        case BiomeZone::Woodland:
            return config_.woodlandDensity;
        case BiomeZone::Grassland:
            return config_.grasslandDensity;
        case BiomeZone::Wetland:
        case BiomeZone::SaltMarsh:
            return config_.wetlandDensity;
        case BiomeZone::Agricultural:
            return config_.agriculturalDensity;
        default:
            // Return empty density for non-vegetated biomes
            static BiomeDensityConfig empty{};
            return empty;
    }
}

bool VegetationPlacer::loadBiomeMap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;

    unsigned error = lodepng::decode(image, w, h, path.c_str());
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load biome map: %s", path.c_str());
        return false;
    }

    biomeWidth_ = w;
    biomeHeight_ = h;

    // Extract biome zone from red channel (encoded as BiomeZone enum value)
    biomeData_.resize(w * h);
    for (size_t i = 0; i < w * h; i++) {
        biomeData_[i] = image[i * 4];  // Red channel = biome zone
    }

    SDL_Log("Loaded biome map: %ux%u", w, h);
    return true;
}

BiomeZone VegetationPlacer::getBiomeAt(float worldX, float worldZ) const {
    if (biomeData_.empty()) return BiomeZone::Grassland;

    // Convert world coords to biome map coords
    float u = (worldX / config_.terrainSize + 0.5f);
    float v = (worldZ / config_.terrainSize + 0.5f);

    int x = static_cast<int>(u * biomeWidth_);
    int y = static_cast<int>(v * biomeHeight_);

    x = std::clamp(x, 0, static_cast<int>(biomeWidth_) - 1);
    y = std::clamp(y, 0, static_cast<int>(biomeHeight_) - 1);

    uint8_t biomeVal = biomeData_[y * biomeWidth_ + x];
    if (biomeVal >= static_cast<uint8_t>(BiomeZone::Count)) {
        return BiomeZone::Grassland;
    }
    return static_cast<BiomeZone>(biomeVal);
}

bool VegetationPlacer::loadHeightmap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;

    unsigned error = lodepng::decode(image, w, h, path.c_str(), LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s", path.c_str());
        return false;
    }

    heightWidth_ = w;
    heightHeight_ = h;

    // Convert 16-bit grayscale to normalized float
    heightData_.resize(w * h);
    for (size_t i = 0; i < w * h; i++) {
        uint16_t val = (image[i * 2] << 8) | image[i * 2 + 1];
        heightData_[i] = static_cast<float>(val) / 65535.0f;
    }

    SDL_Log("Loaded heightmap: %ux%u", w, h);
    return true;
}

float VegetationPlacer::getHeightAt(float worldX, float worldZ) const {
    if (heightData_.empty()) return 0.0f;

    float u = (worldX / config_.terrainSize + 0.5f);
    float v = (worldZ / config_.terrainSize + 0.5f);

    int x = static_cast<int>(u * heightWidth_);
    int y = static_cast<int>(v * heightHeight_);

    x = std::clamp(x, 0, static_cast<int>(heightWidth_) - 1);
    y = std::clamp(y, 0, static_cast<int>(heightHeight_) - 1);

    float h = heightData_[y * heightWidth_ + x];
    return config_.minAltitude + h * (config_.maxAltitude - config_.minAltitude);
}

float VegetationPlacer::getSlopeAt(float worldX, float worldZ) const {
    if (heightData_.empty()) return 0.0f;

    float sampleDist = config_.terrainSize / heightWidth_;
    float hL = getHeightAt(worldX - sampleDist, worldZ);
    float hR = getHeightAt(worldX + sampleDist, worldZ);
    float hD = getHeightAt(worldX, worldZ - sampleDist);
    float hU = getHeightAt(worldX, worldZ + sampleDist);

    float dx = (hR - hL) / (2.0f * sampleDist);
    float dz = (hU - hD) / (2.0f * sampleDist);

    return std::sqrt(dx * dx + dz * dz);
}

bool VegetationPlacer::generate(const VegetationGeneratorConfig& config, ProgressCallback callback) {
    config_ = config;
    tiles_.clear();
    stats_ = Statistics{};

    if (callback) callback(0.0f, "Loading biome map...");

    if (!config.biomemapPath.empty()) {
        if (!loadBiomeMap(config.biomemapPath)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Could not load biome map, using default biome distribution");
        }
    }

    if (!config.heightmapPath.empty()) {
        if (!loadHeightmap(config.heightmapPath)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Could not load heightmap, slope filtering disabled");
        }
    }

    // Calculate tile grid
    float halfSize = config.terrainSize / 2.0f;
    int tilesPerSide = static_cast<int>(std::ceil(config.terrainSize / config.tileSize));
    int totalTiles = tilesPerSide * tilesPerSide;

    if (callback) callback(0.1f, "Generating vegetation tiles...");

    int tilesProcessed = 0;
    for (int tz = 0; tz < tilesPerSide; tz++) {
        for (int tx = 0; tx < tilesPerSide; tx++) {
            VegetationTile tile;
            tile.tileX = tx;
            tile.tileZ = tz;
            tile.worldMin = glm::vec2(
                -halfSize + tx * config.tileSize,
                -halfSize + tz * config.tileSize
            );
            tile.worldMax = tile.worldMin + glm::vec2(config.tileSize);

            generateTile(tx, tz, config, tile);

            if (!tile.instances.empty()) {
                tiles_.push_back(std::move(tile));
            }

            tilesProcessed++;
            if (callback && tilesProcessed % 10 == 0) {
                float progress = 0.1f + 0.8f * (static_cast<float>(tilesProcessed) / totalTiles);
                callback(progress, "Generating tile " + std::to_string(tilesProcessed) +
                         "/" + std::to_string(totalTiles));
            }
        }
    }

    stats_.tilesGenerated = tiles_.size();

    if (callback) callback(1.0f, "Vegetation generation complete");

    SDL_Log("Generated %zu tiles with %zu total instances",
            tiles_.size(), getTotalInstanceCount());

    return true;
}

bool VegetationPlacer::generateTile(int32_t tileX, int32_t tileZ,
                                     const VegetationGeneratorConfig& config,
                                     VegetationTile& outTile) {
    // Create deterministic seed for this tile
    uint32_t tileSeed = config.seed ^ (static_cast<uint32_t>(tileX) * 73856093u)
                                    ^ (static_cast<uint32_t>(tileZ) * 19349663u);
    std::mt19937 rng(tileSeed);

    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);

    // Sample biome at tile center to determine densities
    glm::vec2 tileCenter = (outTile.worldMin + outTile.worldMax) * 0.5f;

    // Generate trees using Poisson disk sampling
    std::vector<glm::vec2> treePoints;
    std::vector<glm::vec2> bushPoints;
    std::vector<glm::vec2> rockPoints;
    std::vector<glm::vec2> detritusPoints;

    // For varied density, we sample biome at multiple points
    // For now, use tile-level density with local biome checks
    BiomeZone tileBiome = getBiomeAt(tileCenter.x, tileCenter.y);
    const BiomeDensityConfig& density = getDensityForBiome(tileBiome);

    // Apply global density multiplier
    float treeDensity = density.treeDensity * config.densityMultiplier;
    float bushDensity = density.bushDensity * config.densityMultiplier;
    float rockDensity = density.rockDensity * config.densityMultiplier;
    float detritusDensity = density.detritusDensity * config.densityMultiplier;

    // Generate points for each layer
    if (treeDensity > 0) {
        poissonDiskSample(outTile.worldMin, outTile.worldMax,
                          config.minTreeSpacing, treeDensity, rng, treePoints);
    }

    if (bushDensity > 0) {
        poissonDiskSample(outTile.worldMin, outTile.worldMax,
                          config.minBushSpacing, bushDensity, rng, bushPoints);
    }

    if (rockDensity > 0) {
        poissonDiskSample(outTile.worldMin, outTile.worldMax,
                          config.minRockSpacing, rockDensity, rng, rockPoints);
    }

    if (detritusDensity > 0) {
        poissonDiskSample(outTile.worldMin, outTile.worldMax,
                          config.minDetritusSpacing, detritusDensity, rng, detritusPoints);
    }

    // Convert points to instances with local biome checks
    for (const auto& p : treePoints) {
        BiomeZone localBiome = getBiomeAt(p.x, p.y);

        // Skip placement in water/sea/beach biomes
        if (localBiome == BiomeZone::Sea ||
            localBiome == BiomeZone::Beach ||
            localBiome == BiomeZone::River) {
            continue;
        }

        // Slope filter - no trees on very steep slopes
        float slope = getSlopeAt(p.x, p.y);
        if (slope > 0.5f) continue;

        VegetationType baseType = selectTreeType(localBiome, rng);
        VegetationType finalType = selectTreeSize(baseType, rng);

        VegetationInstance inst;
        inst.position = p;
        inst.rotation = angleDist(rng);
        inst.scale = scaleDist(rng);
        inst.type = finalType;
        inst.seed = rng();

        outTile.instances.push_back(inst);
        stats_.totalTrees++;
        stats_.byType[getVegetationTypeName(finalType)]++;
    }

    for (const auto& p : bushPoints) {
        BiomeZone localBiome = getBiomeAt(p.x, p.y);

        if (localBiome == BiomeZone::Sea ||
            localBiome == BiomeZone::River) {
            continue;
        }

        VegetationInstance inst;
        inst.position = p;
        inst.rotation = angleDist(rng);
        inst.scale = scaleDist(rng) * 0.8f;  // Bushes slightly smaller variance
        inst.type = selectBushType(rng);
        inst.seed = rng();

        outTile.instances.push_back(inst);
        stats_.totalBushes++;
        stats_.byType[getVegetationTypeName(inst.type)]++;
    }

    for (const auto& p : rockPoints) {
        BiomeZone localBiome = getBiomeAt(p.x, p.y);

        if (localBiome == BiomeZone::Sea) continue;

        VegetationInstance inst;
        inst.position = p;
        inst.rotation = angleDist(rng);
        inst.scale = scaleDist(rng) * 1.5f;  // Rocks have more size variation
        inst.type = VegetationType::Rock;
        inst.seed = rng();

        outTile.instances.push_back(inst);
        stats_.totalRocks++;
        stats_.byType["rock"]++;
    }

    for (const auto& p : detritusPoints) {
        BiomeZone localBiome = getBiomeAt(p.x, p.y);

        if (localBiome == BiomeZone::Sea ||
            localBiome == BiomeZone::River ||
            localBiome == BiomeZone::Beach) {
            continue;
        }

        VegetationInstance inst;
        inst.position = p;
        inst.rotation = angleDist(rng);
        inst.scale = scaleDist(rng);
        inst.type = selectDetritusType(rng);
        inst.seed = rng();

        outTile.instances.push_back(inst);
        stats_.totalDetritus++;
        stats_.byType[getVegetationTypeName(inst.type)]++;
    }

    return true;
}

size_t VegetationPlacer::getTotalInstanceCount() const {
    size_t total = 0;
    for (const auto& tile : tiles_) {
        total += tile.instances.size();
    }
    return total;
}

bool VegetationPlacer::saveTiles(const std::string& outputDir) const {
    std::filesystem::create_directories(outputDir);

    for (const auto& tile : tiles_) {
        json tileJson;
        tileJson["tileX"] = tile.tileX;
        tileJson["tileZ"] = tile.tileZ;
        tileJson["worldMin"] = {tile.worldMin.x, tile.worldMin.y};
        tileJson["worldMax"] = {tile.worldMax.x, tile.worldMax.y};

        json instances = json::array();
        for (const auto& inst : tile.instances) {
            json instJson;
            instJson["position"] = {inst.position.x, inst.position.y};
            instJson["rotation"] = inst.rotation;
            instJson["scale"] = inst.scale;
            instJson["type"] = getVegetationTypeName(inst.type);
            instJson["seed"] = inst.seed;

            // Include preset path for tree types
            const char* preset = getVegetationPreset(inst.type);
            if (preset) {
                instJson["preset"] = preset;
            }

            instances.push_back(instJson);
        }
        tileJson["instances"] = instances;

        std::ostringstream filename;
        filename << outputDir << "/tile_" << tile.tileX << "_" << tile.tileZ << ".json";

        std::ofstream file(filename.str());
        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write tile: %s",
                         filename.str().c_str());
            return false;
        }
        file << tileJson.dump(2);
    }

    SDL_Log("Saved %zu tiles to %s", tiles_.size(), outputDir.c_str());
    return true;
}

bool VegetationPlacer::saveManifest(const std::string& path) const {
    json manifest;
    manifest["version"] = 1;
    manifest["tileSize"] = config_.tileSize;
    manifest["terrainSize"] = config_.terrainSize;
    manifest["seed"] = config_.seed;
    manifest["totalInstances"] = getTotalInstanceCount();

    json tileList = json::array();
    for (const auto& tile : tiles_) {
        json tileInfo;
        tileInfo["x"] = tile.tileX;
        tileInfo["z"] = tile.tileZ;
        tileInfo["count"] = tile.instances.size();
        tileList.push_back(tileInfo);
    }
    manifest["tiles"] = tileList;

    json statsJson;
    statsJson["trees"] = stats_.totalTrees;
    statsJson["bushes"] = stats_.totalBushes;
    statsJson["rocks"] = stats_.totalRocks;
    statsJson["detritus"] = stats_.totalDetritus;

    json byType;
    for (const auto& [name, count] : stats_.byType) {
        byType[name] = count;
    }
    statsJson["byType"] = byType;
    manifest["statistics"] = statsJson;

    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write manifest: %s", path.c_str());
        return false;
    }
    file << manifest.dump(2);

    SDL_Log("Saved manifest to %s", path.c_str());
    return true;
}

bool VegetationPlacer::saveSVG(const std::string& path, int size) const {
    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write SVG: %s", path.c_str());
        return false;
    }

    float scale = static_cast<float>(size) / config_.terrainSize;
    float offset = config_.terrainSize / 2.0f;

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size
         << "\" height=\"" << size << "\" viewBox=\"0 0 " << size << " " << size << "\">\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"#2d5a27\"/>\n";

    // Draw tile boundaries
    file << "  <g stroke=\"#1a3d17\" stroke-width=\"0.5\" fill=\"none\">\n";
    for (const auto& tile : tiles_) {
        float x1 = (tile.worldMin.x + offset) * scale;
        float y1 = (tile.worldMin.y + offset) * scale;
        float w = (tile.worldMax.x - tile.worldMin.x) * scale;
        float h = (tile.worldMax.y - tile.worldMin.y) * scale;
        file << "    <rect x=\"" << x1 << "\" y=\"" << y1
             << "\" width=\"" << w << "\" height=\"" << h << "\"/>\n";
    }
    file << "  </g>\n";

    // Color map for vegetation types
    auto getColor = [](VegetationType type) -> const char* {
        switch (type) {
            case VegetationType::OakSmall:
            case VegetationType::OakMedium:
            case VegetationType::OakLarge:
                return "#228B22";  // Forest green
            case VegetationType::AshSmall:
            case VegetationType::AshMedium:
            case VegetationType::AshLarge:
                return "#32CD32";  // Lime green
            case VegetationType::BeechSmall:
            case VegetationType::BeechMedium:
            case VegetationType::BeechLarge:
                return "#3CB371";  // Medium sea green
            case VegetationType::PineSmall:
            case VegetationType::PineMedium:
            case VegetationType::PineLarge:
                return "#006400";  // Dark green
            case VegetationType::Bush1:
            case VegetationType::Bush2:
            case VegetationType::Bush3:
                return "#8FBC8F";  // Dark sea green
            case VegetationType::Rock:
                return "#808080";  // Gray
            case VegetationType::FallenBranch:
            case VegetationType::Log:
            case VegetationType::Stump:
                return "#8B4513";  // Saddle brown
            case VegetationType::Mushroom:
                return "#F5DEB3";  // Wheat
            case VegetationType::Fern:
                return "#90EE90";  // Light green
            case VegetationType::Bramble:
                return "#556B2F";  // Dark olive green
            case VegetationType::PlaceholderRed:
                return "#FF0000";
            case VegetationType::PlaceholderGreen:
                return "#00FF00";
            case VegetationType::PlaceholderBlue:
                return "#0000FF";
            case VegetationType::PlaceholderYellow:
                return "#FFFF00";
            default:
                return "#FFFFFF";
        }
    };

    auto getRadius = [](VegetationType type) -> float {
        if (isTreeType(type)) {
            // Tree size based on small/medium/large
            int typeIdx = static_cast<int>(type);
            int sizeIdx = typeIdx % 3;  // 0=small, 1=medium, 2=large
            return 2.0f + sizeIdx * 1.5f;
        }
        switch (type) {
            case VegetationType::Rock: return 1.5f;
            case VegetationType::FallenBranch:
            case VegetationType::Log: return 1.0f;
            default: return 0.8f;
        }
    };

    // Draw instances grouped by type
    file << "  <g>\n";
    for (const auto& tile : tiles_) {
        for (const auto& inst : tile.instances) {
            float x = (inst.position.x + offset) * scale;
            float y = (inst.position.y + offset) * scale;
            float r = getRadius(inst.type) * inst.scale;

            file << "    <circle cx=\"" << x << "\" cy=\"" << y
                 << "\" r=\"" << r << "\" fill=\"" << getColor(inst.type) << "\"/>\n";
        }
    }
    file << "  </g>\n";

    // Legend
    file << "  <g transform=\"translate(20, " << (size - 180) << ")\">\n";
    file << "    <rect x=\"0\" y=\"0\" width=\"150\" height=\"170\" fill=\"white\" "
         << "fill-opacity=\"0.8\" rx=\"5\"/>\n";
    file << "    <text x=\"10\" y=\"20\" font-family=\"sans-serif\" font-size=\"12\" "
         << "font-weight=\"bold\">Legend</text>\n";

    const char* legendItems[] = {
        "Oak", "#228B22",
        "Ash", "#32CD32",
        "Beech", "#3CB371",
        "Pine", "#006400",
        "Bush", "#8FBC8F",
        "Rock", "#808080",
        "Detritus", "#8B4513"
    };

    for (int i = 0; i < 7; i++) {
        int y = 35 + i * 18;
        file << "    <circle cx=\"20\" cy=\"" << y << "\" r=\"6\" fill=\""
             << legendItems[i * 2 + 1] << "\"/>\n";
        file << "    <text x=\"35\" y=\"" << (y + 4)
             << "\" font-family=\"sans-serif\" font-size=\"11\">"
             << legendItems[i * 2] << "</text>\n";
    }
    file << "  </g>\n";

    file << "</svg>\n";

    SDL_Log("Saved SVG visualization to %s", path.c_str());
    return true;
}
