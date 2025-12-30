#include "SettlementGenerator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <random>
#include <nlohmann/json.hpp>
#include <stb_image.h>

// Settlement SVG generation (reuse from biome_preprocess)
#include "SettlementSVG.h"

const char* SettlementGenerator::getSettlementTypeName(SettlementType type) {
    switch (type) {
        case SettlementType::Hamlet: return "hamlet";
        case SettlementType::Village: return "village";
        case SettlementType::Town: return "town";
        case SettlementType::FishingVillage: return "fishing_village";
        default: return "unknown";
    }
}

bool SettlementGenerator::generate(const SettlementConfig& cfg, ProgressCallback callback) {
    config = cfg;

    if (callback) callback(0.0f, "Loading heightmap...");
    if (!loadHeightmap(config.heightmapPath, callback)) {
        return false;
    }

    if (callback) callback(0.1f, "Loading erosion data...");
    if (!loadErosionData(config.erosionCacheDir, callback)) {
        return false;
    }

    // Try to load biome map, but it's optional
    if (!config.biomeMapPath.empty()) {
        if (callback) callback(0.2f, "Loading biome map...");
        if (loadBiomeMap(config.biomeMapPath, callback)) {
            hasBiomeMap = true;
        }
    }

    // Initialize result dimensions (use heightmap resolution)
    result.width = heightmapWidth;
    result.height = heightmapHeight;

    if (callback) callback(0.3f, "Computing slope map...");
    computeSlopeMap(callback);

    if (callback) callback(0.4f, "Computing distance to sea...");
    computeDistanceToSea(callback);

    if (callback) callback(0.5f, "Computing distance to river...");
    computeDistanceToRiver(callback);

    if (!hasBiomeMap) {
        if (callback) callback(0.6f, "Classifying basic zones...");
        classifyBasicZones(callback);
    }

    if (callback) callback(0.7f, "Placing settlements...");
    placeSettlements(callback);

    if (callback) callback(1.0f, "Settlement generation complete");
    return true;
}

bool SettlementGenerator::loadHeightmap(const std::string& path, ProgressCallback) {
    int w, h, channels;
    uint16_t* data16 = reinterpret_cast<uint16_t*>(stbi_load_16(path.c_str(), &w, &h, &channels, 1));

    if (!data16) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s", path.c_str());
        return false;
    }

    heightmapWidth = static_cast<uint32_t>(w);
    heightmapHeight = static_cast<uint32_t>(h);
    heightData.resize(heightmapWidth * heightmapHeight);

    float altitudeRange = config.maxAltitude - config.minAltitude;
    for (size_t i = 0; i < heightData.size(); i++) {
        float normalized = static_cast<float>(data16[i]) / 65535.0f;
        heightData[i] = config.minAltitude + normalized * altitudeRange;
    }

    stbi_image_free(data16);
    SDL_Log("Loaded heightmap: %ux%u", heightmapWidth, heightmapHeight);
    return true;
}

bool SettlementGenerator::loadErosionData(const std::string& cacheDir, ProgressCallback) {
    std::string flowAccPath = cacheDir + "/flow_accumulation.bin";
    std::string flowDirPath = cacheDir + "/flow_direction.bin";

    // Load flow accumulation
    {
        std::ifstream file(flowAccPath, std::ios::binary);
        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load flow accumulation: %s", flowAccPath.c_str());
            return false;
        }

        uint32_t w, h;
        file.read(reinterpret_cast<char*>(&w), sizeof(w));
        file.read(reinterpret_cast<char*>(&h), sizeof(h));

        flowMapWidth = w;
        flowMapHeight = h;
        flowAccumulation.resize(w * h);
        file.read(reinterpret_cast<char*>(flowAccumulation.data()), flowAccumulation.size() * sizeof(float));
    }

    // Load flow direction
    {
        std::ifstream file(flowDirPath, std::ios::binary);
        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load flow direction: %s", flowDirPath.c_str());
            return false;
        }

        uint32_t w, h;
        file.read(reinterpret_cast<char*>(&w), sizeof(w));
        file.read(reinterpret_cast<char*>(&h), sizeof(h));

        flowDirection.resize(w * h);
        file.read(reinterpret_cast<char*>(flowDirection.data()), flowDirection.size());
    }

    SDL_Log("Loaded erosion data: %ux%u", flowMapWidth, flowMapHeight);
    return true;
}

bool SettlementGenerator::loadBiomeMap(const std::string& path, ProgressCallback) {
    int w, h, channels;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 4);

    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load biome map: %s", path.c_str());
        return false;
    }

    biomeMapWidth = static_cast<uint32_t>(w);
    biomeMapHeight = static_cast<uint32_t>(h);
    biomeZones.resize(biomeMapWidth * biomeMapHeight);

    for (size_t i = 0; i < biomeZones.size(); i++) {
        // Red channel contains zone ID
        uint8_t zone = data[i * 4];
        biomeZones[i] = static_cast<BiomeZone>(zone < static_cast<uint8_t>(BiomeZone::Count) ? zone : 0);
    }

    stbi_image_free(data);
    SDL_Log("Loaded biome map: %ux%u", biomeMapWidth, biomeMapHeight);
    return true;
}

void SettlementGenerator::computeSlopeMap(ProgressCallback) {
    result.slopeMap.resize(result.width * result.height);

    float cellSize = config.terrainSize / static_cast<float>(heightmapWidth);

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            // Sample heights in a 3x3 neighborhood
            float hL = sampleHeight(static_cast<float>(x) - 1.0f, static_cast<float>(y));
            float hR = sampleHeight(static_cast<float>(x) + 1.0f, static_cast<float>(y));
            float hD = sampleHeight(static_cast<float>(x), static_cast<float>(y) - 1.0f);
            float hU = sampleHeight(static_cast<float>(x), static_cast<float>(y) + 1.0f);

            // Central differences
            float dzdx = (hR - hL) / (2.0f * cellSize);
            float dzdy = (hU - hD) / (2.0f * cellSize);

            float slope = std::sqrt(dzdx * dzdx + dzdy * dzdy);
            result.slopeMap[y * result.width + x] = slope;
        }
    }
}

void SettlementGenerator::computeDistanceToSea(ProgressCallback) {
    result.distanceToSea.resize(result.width * result.height, std::numeric_limits<float>::max());

    // Find sea cells and initialize queue
    std::queue<std::pair<int, int>> queue;

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float h = heightData[y * heightmapWidth + x];
            if (h <= config.seaLevel) {
                result.distanceToSea[y * result.width + x] = 0.0f;
                queue.push({x, y});
            }
        }
    }

    // BFS to compute distances
    float cellSize = config.terrainSize / static_cast<float>(result.width);
    const int dx[] = {-1, 0, 1, 0};
    const int dy[] = {0, -1, 0, 1};

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        float currentDist = result.distanceToSea[cy * result.width + cx];

        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx < 0 || nx >= static_cast<int>(result.width) ||
                ny < 0 || ny >= static_cast<int>(result.height)) {
                continue;
            }

            float newDist = currentDist + cellSize;
            if (newDist < result.distanceToSea[ny * result.width + nx]) {
                result.distanceToSea[ny * result.width + nx] = newDist;
                queue.push({nx, ny});
            }
        }
    }
}

void SettlementGenerator::computeDistanceToRiver(ProgressCallback) {
    result.distanceToRiver.resize(result.width * result.height, std::numeric_limits<float>::max());

    // Find river cells (high flow accumulation)
    std::queue<std::pair<int, int>> queue;
    float maxFlow = *std::max_element(flowAccumulation.begin(), flowAccumulation.end());
    float riverThreshold = maxFlow * config.riverFlowThreshold;

    for (uint32_t y = 0; y < flowMapHeight; y++) {
        for (uint32_t x = 0; x < flowMapWidth; x++) {
            if (flowAccumulation[y * flowMapWidth + x] >= riverThreshold) {
                // Map to result coordinates
                uint32_t rx = x * result.width / flowMapWidth;
                uint32_t ry = y * result.height / flowMapHeight;
                if (rx < result.width && ry < result.height) {
                    result.distanceToRiver[ry * result.width + rx] = 0.0f;
                    queue.push({static_cast<int>(rx), static_cast<int>(ry)});
                }
            }
        }
    }

    // BFS
    float cellSize = config.terrainSize / static_cast<float>(result.width);
    const int dx[] = {-1, 0, 1, 0};
    const int dy[] = {0, -1, 0, 1};

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        float currentDist = result.distanceToRiver[cy * result.width + cx];

        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx < 0 || nx >= static_cast<int>(result.width) ||
                ny < 0 || ny >= static_cast<int>(result.height)) {
                continue;
            }

            float newDist = currentDist + cellSize;
            if (newDist < result.distanceToRiver[ny * result.width + nx]) {
                result.distanceToRiver[ny * result.width + nx] = newDist;
                queue.push({nx, ny});
            }
        }
    }
}

void SettlementGenerator::classifyBasicZones(ProgressCallback) {
    // Simple zone classification when no biome map is provided
    biomeZones.resize(result.width * result.height);
    biomeMapWidth = result.width;
    biomeMapHeight = result.height;

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            size_t idx = y * result.width + x;
            float h = heightData[y * heightmapWidth + x];
            float distSea = result.distanceToSea[idx];
            float distRiver = result.distanceToRiver[idx];

            if (h <= config.seaLevel) {
                biomeZones[idx] = BiomeZone::Sea;
            } else if (h < config.beachMaxHeight && distSea < config.coastalDistance) {
                biomeZones[idx] = BiomeZone::Beach;
            } else if (distRiver < 50.0f) {
                biomeZones[idx] = BiomeZone::River;
            } else if (distRiver < 200.0f) {
                biomeZones[idx] = BiomeZone::Wetland;
            } else {
                biomeZones[idx] = BiomeZone::Grassland;
            }
        }
    }

    hasBiomeMap = true;
}

float SettlementGenerator::sampleHeight(float x, float z) const {
    int ix = static_cast<int>(x);
    int iz = static_cast<int>(z);
    ix = std::clamp(ix, 0, static_cast<int>(heightmapWidth) - 1);
    iz = std::clamp(iz, 0, static_cast<int>(heightmapHeight) - 1);
    return heightData[iz * heightmapWidth + ix];
}

float SettlementGenerator::sampleSlope(float x, float z) const {
    int ix = static_cast<int>(x);
    int iz = static_cast<int>(z);
    ix = std::clamp(ix, 0, static_cast<int>(result.width) - 1);
    iz = std::clamp(iz, 0, static_cast<int>(result.height) - 1);
    return result.slopeMap[iz * result.width + ix];
}

float SettlementGenerator::sampleFlowAccumulation(float x, float z) const {
    float fx = x * static_cast<float>(flowMapWidth) / static_cast<float>(heightmapWidth);
    float fz = z * static_cast<float>(flowMapHeight) / static_cast<float>(heightmapHeight);
    int ix = std::clamp(static_cast<int>(fx), 0, static_cast<int>(flowMapWidth) - 1);
    int iz = std::clamp(static_cast<int>(fz), 0, static_cast<int>(flowMapHeight) - 1);
    return flowAccumulation[iz * flowMapWidth + ix];
}

BiomeZone SettlementGenerator::sampleZone(float x, float z) const {
    if (!hasBiomeMap) return BiomeZone::Grassland;

    float fx = x * static_cast<float>(biomeMapWidth) / static_cast<float>(heightmapWidth);
    float fz = z * static_cast<float>(biomeMapHeight) / static_cast<float>(heightmapHeight);
    int ix = std::clamp(static_cast<int>(fx), 0, static_cast<int>(biomeMapWidth) - 1);
    int iz = std::clamp(static_cast<int>(fz), 0, static_cast<int>(biomeMapHeight) - 1);
    return biomeZones[iz * biomeMapWidth + ix];
}

float SettlementGenerator::calculateSettlementScore(float x, float z) const {
    float score = 0.0f;

    // Height (prefer moderate heights)
    float height = sampleHeight(x, z);
    if (height <= config.seaLevel) return -1000.0f;  // Not on sea

    float heightScore = 1.0f - std::abs(height - 30.0f) / 100.0f;
    score += heightScore * 20.0f;

    // Slope (prefer gentle slopes)
    float slope = sampleSlope(x, z);
    if (slope > 0.3f) return -1000.0f;  // Too steep
    float slopeScore = 1.0f - slope / 0.3f;
    score += slopeScore * 30.0f;

    // Distance to sea (coastal bonus for fishing villages)
    float cellSize = config.terrainSize / static_cast<float>(result.width);
    int ix = std::clamp(static_cast<int>(x), 0, static_cast<int>(result.width) - 1);
    int iz = std::clamp(static_cast<int>(z), 0, static_cast<int>(result.height) - 1);
    float distSea = result.distanceToSea[iz * result.width + ix];

    if (distSea < config.coastalDistance) {
        score += 25.0f;  // Coastal bonus
    }

    // Distance to river (access to water)
    float distRiver = result.distanceToRiver[iz * result.width + ix];
    if (distRiver < 500.0f) {
        float riverScore = 1.0f - distRiver / 500.0f;
        score += riverScore * 20.0f;
    }

    // Flow accumulation (water availability)
    float flow = sampleFlowAccumulation(x, z);
    float maxFlow = *std::max_element(flowAccumulation.begin(), flowAccumulation.end());
    if (maxFlow > 0) {
        float flowNorm = flow / maxFlow;
        // Prefer some flow but not too much (flooding)
        if (flowNorm < 0.1f) {
            score += flowNorm * 100.0f;
        } else if (flowNorm < 0.5f) {
            score += 10.0f - (flowNorm - 0.1f) * 20.0f;
        }
    }

    // Zone-based modifiers
    BiomeZone zone = sampleZone(x, z);
    switch (zone) {
        case BiomeZone::Sea:
        case BiomeZone::River:
            return -1000.0f;  // Can't build here
        case BiomeZone::Beach:
        case BiomeZone::ChalkCliff:
        case BiomeZone::SaltMarsh:
            score -= 10.0f;  // Less favorable
            break;
        case BiomeZone::Wetland:
            score -= 5.0f;
            break;
        case BiomeZone::Grassland:
        case BiomeZone::Agricultural:
            score += 10.0f;  // Good for settlements
            break;
        case BiomeZone::Woodland:
            score += 5.0f;   // Moderate
            break;
        default:
            break;
    }

    return score;
}

bool SettlementGenerator::isValidSettlementLocation(float x, float z, const std::vector<Settlement>& existing) const {
    // Check minimum distance to existing settlements
    float worldX = x * config.terrainSize / static_cast<float>(heightmapWidth);
    float worldZ = z * config.terrainSize / static_cast<float>(heightmapHeight);

    for (const auto& s : existing) {
        float dx = worldX - s.position.x;
        float dz = worldZ - s.position.y;
        float dist = std::sqrt(dx * dx + dz * dz);

        float minDist;
        switch (s.type) {
            case SettlementType::Town:
                minDist = config.townMinDistance;
                break;
            case SettlementType::Village:
            case SettlementType::FishingVillage:
                minDist = config.villageMinDistance;
                break;
            default:
                minDist = config.hamletMinDistance;
                break;
        }

        if (dist < minDist) return false;
    }

    return true;
}

void SettlementGenerator::placeSettlements(ProgressCallback callback) {
    result.settlements.clear();

    // Sample candidate locations on a grid
    const int gridStep = 16;
    std::vector<std::pair<float, glm::ivec2>> candidates;

    for (uint32_t y = gridStep; y < heightmapHeight - gridStep; y += gridStep) {
        for (uint32_t x = gridStep; x < heightmapWidth - gridStep; x += gridStep) {
            float score = calculateSettlementScore(static_cast<float>(x), static_cast<float>(y));
            if (score > 0.0f) {
                candidates.push_back({score, glm::ivec2(x, y)});
            }
        }
    }

    // Sort by score (highest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    SDL_Log("Found %zu candidate settlement locations", candidates.size());

    // Greedily place settlements
    uint32_t settlementId = 0;
    float cellToWorld = config.terrainSize / static_cast<float>(heightmapWidth);

    for (const auto& [score, pos] : candidates) {
        if (result.settlements.size() >= config.numSettlements) break;

        float x = static_cast<float>(pos.x);
        float z = static_cast<float>(pos.y);

        if (!isValidSettlementLocation(x, z, result.settlements)) {
            continue;
        }

        Settlement settlement;
        settlement.id = settlementId++;
        settlement.position = glm::vec2(x * cellToWorld, z * cellToWorld);
        settlement.score = score;

        // Determine type based on location and score
        int ix = std::clamp(pos.x, 0, static_cast<int>(result.width) - 1);
        int iz = std::clamp(pos.y, 0, static_cast<int>(result.height) - 1);
        float distSea = result.distanceToSea[iz * result.width + ix];
        float distRiver = result.distanceToRiver[iz * result.width + ix];

        if (distSea < config.coastalDistance && score > 50.0f) {
            settlement.type = SettlementType::FishingVillage;
            settlement.radius = config.fishingVillageRadius;
            settlement.features.push_back("coastal");
            settlement.features.push_back("harbour");
        } else if (score > 60.0f && result.settlements.size() < 3) {
            settlement.type = SettlementType::Town;
            settlement.radius = config.townRadius;
            settlement.features.push_back("market");
        } else if (score > 40.0f) {
            settlement.type = SettlementType::Village;
            settlement.radius = config.villageRadius;
        } else {
            settlement.type = SettlementType::Hamlet;
            settlement.radius = config.hamletRadius;
        }

        // Add features based on terrain
        if (distRiver < 200.0f) {
            settlement.features.push_back("river_access");
        }

        BiomeZone zone = sampleZone(x, z);
        if (zone == BiomeZone::Agricultural) {
            settlement.features.push_back("agricultural");
        } else if (zone == BiomeZone::Grassland) {
            settlement.features.push_back("downland");
        }

        result.settlements.push_back(settlement);
        SDL_Log("Placed %s #%u at (%.0f, %.0f) score=%.1f",
                getSettlementTypeName(settlement.type),
                settlement.id,
                settlement.position.x, settlement.position.y,
                settlement.score);
    }

    SDL_Log("Placed %zu settlements", result.settlements.size());
}

bool SettlementGenerator::saveSettlements(const std::string& path) const {
    nlohmann::json j;
    j["version"] = 1;
    j["terrain_size"] = config.terrainSize;

    nlohmann::json settlementsJson = nlohmann::json::array();
    for (const auto& s : result.settlements) {
        nlohmann::json sj;
        sj["id"] = s.id;
        sj["type"] = getSettlementTypeName(s.type);
        sj["position"] = {s.position.x, s.position.y};
        sj["radius"] = s.radius;
        sj["score"] = s.score;
        sj["features"] = s.features;
        settlementsJson.push_back(sj);
    }
    j["settlements"] = settlementsJson;

    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write settlements: %s", path.c_str());
        return false;
    }

    file << j.dump(2);
    SDL_Log("Saved settlements to: %s", path.c_str());
    return true;
}

bool SettlementGenerator::saveSettlementsSVG(const std::string& path) const {
    writeSettlementsSVG(path, result.settlements, config.terrainSize, config.svgWidth, config.svgHeight);
    return true;
}
