#include "BiomeGenerator.h"
#include <SDL3/SDL_log.h>
#include <stb_image.h>
#include <lodepng.h>
#include <fstream>
#include <queue>
#include <algorithm>
#include <cmath>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// Simple hash-based noise function
float BiomeGenerator::noise2D(float x, float y, float frequency) const {
    x *= frequency;
    y *= frequency;

    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));

    float xf = x - xi;
    float yf = y - yi;

    // Smoothstep
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);

    // Hash function
    auto hash = [](int x, int y) -> float {
        int n = x + y * 57;
        n = (n << 13) ^ n;
        return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
    };

    float n00 = hash(xi, yi);
    float n10 = hash(xi + 1, yi);
    float n01 = hash(xi, yi + 1);
    float n11 = hash(xi + 1, yi + 1);

    float nx0 = n00 * (1.0f - u) + n10 * u;
    float nx1 = n01 * (1.0f - u) + n11 * u;

    return nx0 * (1.0f - v) + nx1 * v;
}

glm::vec3 BiomeGenerator::getZoneColor(BiomeZone zone) {
    switch (zone) {
        case BiomeZone::Sea:         return glm::vec3(0.165f, 0.353f, 0.541f);  // #2a5a8a
        case BiomeZone::Beach:       return glm::vec3(0.831f, 0.753f, 0.565f);  // #d4c090
        case BiomeZone::ChalkCliff:  return glm::vec3(0.941f, 0.941f, 0.941f);  // #f0f0f0
        case BiomeZone::SaltMarsh:   return glm::vec3(0.353f, 0.478f, 0.353f);  // #5a7a5a
        case BiomeZone::River:       return glm::vec3(0.290f, 0.565f, 0.753f);  // #4a90c0
        case BiomeZone::Wetland:     return glm::vec3(0.416f, 0.541f, 0.416f);  // #6a8a6a
        case BiomeZone::Grassland:   return glm::vec3(0.565f, 0.690f, 0.376f);  // #90b060
        case BiomeZone::Agricultural:return glm::vec3(0.753f, 0.627f, 0.376f);  // #c0a060
        case BiomeZone::Woodland:    return glm::vec3(0.290f, 0.416f, 0.227f);  // #4a6a3a
        default:                     return glm::vec3(1.0f, 0.0f, 1.0f);        // Magenta for unknown
    }
}

const char* BiomeGenerator::getZoneName(BiomeZone zone) {
    switch (zone) {
        case BiomeZone::Sea:         return "Sea";
        case BiomeZone::Beach:       return "Beach";
        case BiomeZone::ChalkCliff:  return "Chalk Cliff";
        case BiomeZone::SaltMarsh:   return "Salt Marsh";
        case BiomeZone::River:       return "River";
        case BiomeZone::Wetland:     return "Wetland";
        case BiomeZone::Grassland:   return "Grassland";
        case BiomeZone::Agricultural:return "Agricultural";
        case BiomeZone::Woodland:    return "Woodland";
        default:                     return "Unknown";
    }
}

const char* BiomeGenerator::getSettlementTypeName(SettlementType type) {
    switch (type) {
        case SettlementType::Hamlet:         return "Hamlet";
        case SettlementType::Village:        return "Village";
        case SettlementType::Town:           return "Town";
        case SettlementType::FishingVillage: return "Fishing Village";
        default:                             return "Unknown";
    }
}

bool BiomeGenerator::loadHeightmap(const std::string& path, ProgressCallback callback) {
    if (callback) callback(0.0f, "Loading heightmap...");

    int width, height, channels;
    uint16_t* data16 = stbi_load_16(path.c_str(), &width, &height, &channels, 1);

    if (!data16) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s", path.c_str());
        return false;
    }

    heightmapWidth = static_cast<uint32_t>(width);
    heightmapHeight = static_cast<uint32_t>(height);
    heightData.resize(heightmapWidth * heightmapHeight);

    float heightRange = config.maxAltitude - config.minAltitude;

    for (uint32_t i = 0; i < heightmapWidth * heightmapHeight; i++) {
        float normalized = static_cast<float>(data16[i]) / 65535.0f;
        heightData[i] = config.minAltitude + normalized * heightRange;
    }

    stbi_image_free(data16);

    SDL_Log("Loaded heightmap: %ux%u, altitude range: %.1f to %.1f",
            heightmapWidth, heightmapHeight, config.minAltitude, config.maxAltitude);

    return true;
}

bool BiomeGenerator::loadErosionData(const std::string& cacheDir, ProgressCallback callback) {
    if (callback) callback(0.05f, "Loading erosion data...");

    // Try to load flow accumulation binary data
    std::string flowPath = cacheDir + "/flow_accumulation.bin";
    std::string dirPath = cacheDir + "/flow_direction.bin";

    std::ifstream flowFile(flowPath, std::ios::binary);
    std::ifstream dirFile(dirPath, std::ios::binary);

    if (!flowFile.is_open() || !dirFile.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Erosion data not found in %s, will estimate from heightmap", cacheDir.c_str());

        // Fall back to using heightmap resolution for flow data
        flowMapWidth = heightmapWidth;
        flowMapHeight = heightmapHeight;
        flowAccumulation.resize(flowMapWidth * flowMapHeight, 0.0f);
        flowDirection.resize(flowMapWidth * flowMapHeight, -1);

        // Simple flow direction calculation (D8)
        const int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
        const int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};

        for (uint32_t y = 0; y < flowMapHeight; y++) {
            for (uint32_t x = 0; x < flowMapWidth; x++) {
                float h = heightData[y * flowMapWidth + x];

                if (h < config.seaLevel) {
                    flowDirection[y * flowMapWidth + x] = -1;  // Sea outlet
                    continue;
                }

                float maxDrop = 0.0f;
                int8_t bestDir = -1;

                for (int d = 0; d < 8; d++) {
                    int nx = static_cast<int>(x) + dx[d];
                    int ny = static_cast<int>(y) + dy[d];

                    if (nx >= 0 && nx < static_cast<int>(flowMapWidth) &&
                        ny >= 0 && ny < static_cast<int>(flowMapHeight)) {
                        float nh = heightData[ny * flowMapWidth + nx];
                        float drop = h - nh;

                        // Diagonal distance correction
                        if (d % 2 == 1) drop /= 1.414f;

                        if (drop > maxDrop) {
                            maxDrop = drop;
                            bestDir = static_cast<int8_t>(d);
                        }
                    }
                }

                flowDirection[y * flowMapWidth + x] = bestDir;
            }
        }

        // Simple flow accumulation (count upstream cells)
        std::vector<uint32_t> accumCount(flowMapWidth * flowMapHeight, 1);

        // Sort cells by height (highest first)
        std::vector<uint32_t> sortedIndices(flowMapWidth * flowMapHeight);
        for (uint32_t i = 0; i < sortedIndices.size(); i++) sortedIndices[i] = i;

        std::sort(sortedIndices.begin(), sortedIndices.end(), [this](uint32_t a, uint32_t b) {
            return heightData[a] > heightData[b];
        });

        // Accumulate flow downstream
        for (uint32_t idx : sortedIndices) {
            int8_t dir = flowDirection[idx];
            if (dir < 0 || dir > 7) continue;

            uint32_t x = idx % flowMapWidth;
            uint32_t y = idx / flowMapWidth;

            int nx = static_cast<int>(x) + dx[dir];
            int ny = static_cast<int>(y) + dy[dir];

            if (nx >= 0 && nx < static_cast<int>(flowMapWidth) &&
                ny >= 0 && ny < static_cast<int>(flowMapHeight)) {
                accumCount[ny * flowMapWidth + nx] += accumCount[idx];
            }
        }

        // Normalize flow accumulation
        uint32_t maxAccum = *std::max_element(accumCount.begin(), accumCount.end());
        for (uint32_t i = 0; i < flowAccumulation.size(); i++) {
            flowAccumulation[i] = static_cast<float>(accumCount[i]) / static_cast<float>(maxAccum);
        }

        SDL_Log("Generated flow data from heightmap: %ux%u", flowMapWidth, flowMapHeight);
        return true;
    }

    // Read dimensions
    flowFile.read(reinterpret_cast<char*>(&flowMapWidth), sizeof(uint32_t));
    flowFile.read(reinterpret_cast<char*>(&flowMapHeight), sizeof(uint32_t));

    flowAccumulation.resize(flowMapWidth * flowMapHeight);
    flowFile.read(reinterpret_cast<char*>(flowAccumulation.data()),
                  flowMapWidth * flowMapHeight * sizeof(float));

    uint32_t dirWidth, dirHeight;
    dirFile.read(reinterpret_cast<char*>(&dirWidth), sizeof(uint32_t));
    dirFile.read(reinterpret_cast<char*>(&dirHeight), sizeof(uint32_t));

    flowDirection.resize(dirWidth * dirHeight);
    dirFile.read(reinterpret_cast<char*>(flowDirection.data()),
                 dirWidth * dirHeight * sizeof(int8_t));

    SDL_Log("Loaded erosion data: flow %ux%u, direction %ux%u",
            flowMapWidth, flowMapHeight, dirWidth, dirHeight);

    return true;
}

float BiomeGenerator::sampleHeight(float x, float z) const {
    float u = x / config.terrainSize;
    float v = z / config.terrainSize;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float fx = u * (heightmapWidth - 1);
    float fy = v * (heightmapHeight - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(heightmapWidth) - 1);
    int y1 = std::min(y0 + 1, static_cast<int>(heightmapHeight) - 1);

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = heightData[y0 * heightmapWidth + x0];
    float h10 = heightData[y0 * heightmapWidth + x1];
    float h01 = heightData[y1 * heightmapWidth + x0];
    float h11 = heightData[y1 * heightmapWidth + x1];

    return (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty;
}

float BiomeGenerator::sampleSlope(float x, float z) const {
    float u = x / config.terrainSize;
    float v = z / config.terrainSize;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    int px = static_cast<int>(u * (result.width - 1));
    int py = static_cast<int>(v * (result.height - 1));

    px = std::clamp(px, 0, static_cast<int>(result.width) - 1);
    py = std::clamp(py, 0, static_cast<int>(result.height) - 1);

    return result.slopeMap[py * result.width + px];
}

float BiomeGenerator::sampleFlowAccumulation(float x, float z) const {
    float u = x / config.terrainSize;
    float v = z / config.terrainSize;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    int px = static_cast<int>(u * (flowMapWidth - 1));
    int py = static_cast<int>(v * (flowMapHeight - 1));

    px = std::clamp(px, 0, static_cast<int>(flowMapWidth) - 1);
    py = std::clamp(py, 0, static_cast<int>(flowMapHeight) - 1);

    return flowAccumulation[py * flowMapWidth + px];
}

int8_t BiomeGenerator::sampleFlowDirection(float x, float z) const {
    float u = x / config.terrainSize;
    float v = z / config.terrainSize;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    int px = static_cast<int>(u * (flowMapWidth - 1));
    int py = static_cast<int>(v * (flowMapHeight - 1));

    px = std::clamp(px, 0, static_cast<int>(flowMapWidth) - 1);
    py = std::clamp(py, 0, static_cast<int>(flowMapHeight) - 1);

    return flowDirection[py * flowMapWidth + px];
}

void BiomeGenerator::computeSlopeMap(ProgressCallback callback) {
    if (callback) callback(0.1f, "Computing slope map...");

    float cellSize = config.terrainSize / result.width;

    // Parallel slope computation - each pixel is independent
    #pragma omp parallel for schedule(dynamic, 64) collapse(2)
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            // Sample heights for gradient
            float hC = sampleHeight(worldX, worldZ);
            float hL = sampleHeight(worldX - cellSize, worldZ);
            float hR = sampleHeight(worldX + cellSize, worldZ);
            float hU = sampleHeight(worldX, worldZ - cellSize);
            float hD = sampleHeight(worldX, worldZ + cellSize);

            float dhdx = (hR - hL) / (2.0f * cellSize);
            float dhdz = (hD - hU) / (2.0f * cellSize);

            float slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
            result.slopeMap[y * result.width + x] = slope;
        }
    }

    SDL_Log("Computed slope map");
}

void BiomeGenerator::computeDistanceToSea(ProgressCallback callback) {
    if (callback) callback(0.15f, "Computing distance to sea...");

    // BFS from sea level cells
    std::queue<std::pair<uint32_t, uint32_t>> queue;
    float cellSize = config.terrainSize / result.width;

    // Initialize with max distance, mark sea cells as 0
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;
            float h = sampleHeight(worldX, worldZ);

            if (h < config.seaLevel) {
                result.distanceToSea[y * result.width + x] = 0.0f;
                queue.push({x, y});
            } else {
                result.distanceToSea[y * result.width + x] = std::numeric_limits<float>::max();
            }
        }
    }

    // BFS propagation
    const int dx[] = {1, 0, -1, 0, 1, 1, -1, -1};
    const int dy[] = {0, 1, 0, -1, 1, -1, 1, -1};
    const float dd[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f};

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        float currentDist = result.distanceToSea[cy * result.width + cx];

        for (int d = 0; d < 8; d++) {
            int nx = static_cast<int>(cx) + dx[d];
            int ny = static_cast<int>(cy) + dy[d];

            if (nx >= 0 && nx < static_cast<int>(result.width) &&
                ny >= 0 && ny < static_cast<int>(result.height)) {
                float newDist = currentDist + cellSize * dd[d];

                if (newDist < result.distanceToSea[ny * result.width + nx]) {
                    result.distanceToSea[ny * result.width + nx] = newDist;
                    queue.push({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                }
            }
        }
    }

    SDL_Log("Computed distance to sea");
}

void BiomeGenerator::computeWatershedMetrics(ProgressCallback callback) {
    // Delegate to WatershedMetrics class
    WatershedMetricsConfig wsConfig;
    wsConfig.terrainSize = config.terrainSize;
    wsConfig.seaLevel = config.seaLevel;
    wsConfig.riverFlowThreshold = config.riverFlowThreshold;
    wsConfig.erosionCacheDir = config.erosionCacheDir;

    // Compute TWI
    WatershedMetrics::computeTWI(
        watershedMetrics,
        result.slopeMap,
        flowAccumulation,
        flowMapWidth, flowMapHeight,
        result.width, result.height,
        config.terrainSize,
        callback
    );

    // Compute stream order
    WatershedMetrics::computeStreamOrder(
        watershedMetrics,
        flowAccumulation,
        flowDirection,
        heightData,
        flowMapWidth, flowMapHeight,
        heightmapWidth, heightmapHeight,
        wsConfig,
        callback
    );

    // Load or generate basin labels
    WatershedMetrics::loadOrGenerateBasins(
        watershedMetrics,
        heightData,
        flowDirection,
        heightmapWidth, heightmapHeight,
        flowMapWidth, flowMapHeight,
        wsConfig,
        callback
    );

    // Copy results to BiomeResult for compatibility
    result.twiMap = watershedMetrics.twiMap;
    result.streamOrderMap = watershedMetrics.streamOrderMap;
    result.basinLabels = watershedMetrics.basinLabels;
    result.basinCount = watershedMetrics.basinCount;
}

void BiomeGenerator::computeDistanceToRiver(ProgressCallback callback) {
    if (callback) callback(0.2f, "Computing distance to rivers...");

    std::queue<std::pair<uint32_t, uint32_t>> queue;
    float cellSize = config.terrainSize / result.width;

    // Initialize - mark river cells as 0
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            float flow = sampleFlowAccumulation(worldX, worldZ);
            float h = sampleHeight(worldX, worldZ);

            if (flow > config.riverFlowThreshold && h >= config.seaLevel) {
                result.distanceToRiver[y * result.width + x] = 0.0f;
                queue.push({x, y});
            } else {
                result.distanceToRiver[y * result.width + x] = std::numeric_limits<float>::max();
            }
        }
    }

    // BFS propagation
    const int dx[] = {1, 0, -1, 0, 1, 1, -1, -1};
    const int dy[] = {0, 1, 0, -1, 1, -1, 1, -1};
    const float dd[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f};

    while (!queue.empty()) {
        auto [cx, cy] = queue.front();
        queue.pop();

        float currentDist = result.distanceToRiver[cy * result.width + cx];

        for (int d = 0; d < 8; d++) {
            int nx = static_cast<int>(cx) + dx[d];
            int ny = static_cast<int>(cy) + dy[d];

            if (nx >= 0 && nx < static_cast<int>(result.width) &&
                ny >= 0 && ny < static_cast<int>(result.height)) {
                float newDist = currentDist + cellSize * dd[d];

                if (newDist < result.distanceToRiver[ny * result.width + nx]) {
                    result.distanceToRiver[ny * result.width + nx] = newDist;
                    queue.push({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                }
            }
        }
    }

    SDL_Log("Computed distance to rivers");
}

void BiomeGenerator::classifyZones(ProgressCallback callback) {
    if (callback) callback(0.3f, "Classifying zones...");

    // Parallel zone classification - each cell is independent
    #pragma omp parallel for schedule(dynamic, 64) collapse(2)
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            float height = sampleHeight(worldX, worldZ);
            float slope = result.slopeMap[y * result.width + x];
            float distSea = result.distanceToSea[y * result.width + x];
            float distRiver = result.distanceToRiver[y * result.width + x];
            float flow = sampleFlowAccumulation(worldX, worldZ);

            // Get TWI and stream order from watershed metrics
            float twi = WatershedMetrics::sampleTWI(watershedMetrics, worldX, worldZ, config.terrainSize);
            uint8_t streamOrder = WatershedMetrics::sampleStreamOrder(watershedMetrics, worldX, worldZ, config.terrainSize);

            // Compute riparian buffer distance based on stream order
            float riparianDist = config.streamOrderRiparianScale * streamOrder;

            bool isCoastal = distSea < config.coastalDistance;
            bool isRiver = flow > config.riverFlowThreshold && height >= config.seaLevel;
            bool nearRiver = distRiver < config.wetlandRiverDistance;
            bool inRiparianZone = distRiver < riparianDist && streamOrder > 0;

            // TWI-based moisture classification
            bool isWetByTwi = twi > config.twiWetlandThreshold;
            bool isWetMeadow = twi > config.twiWetMeadowThreshold && twi <= config.twiWetlandThreshold;
            bool isDryChalk = twi < config.twiDryThreshold;
            bool isValleyBottom = twi > config.valleyBottomTwi && slope < 0.1f;

            BiomeZone zone = BiomeZone::Grassland;  // Default

            // Priority-based classification with TWI enhancement
            if (height < config.seaLevel) {
                zone = BiomeZone::Sea;
            }
            else if (isRiver) {
                zone = BiomeZone::River;
            }
            else if (isCoastal && slope > config.cliffSlopeThreshold) {
                zone = BiomeZone::ChalkCliff;
            }
            else if (isCoastal && height < config.beachMaxHeight && slope < config.beachMaxSlope) {
                zone = BiomeZone::Beach;
            }
            else if (isCoastal && height < config.marshMaxHeight && slope < config.marshMaxSlope) {
                zone = BiomeZone::SaltMarsh;
            }
            else if (isWetByTwi && height < config.agriculturalMaxHeight) {
                // TWI indicates wetland-prone area (high flow accumulation, low slope)
                zone = BiomeZone::Wetland;
            }
            else if (nearRiver && slope < 0.1f && height < config.agriculturalMaxHeight) {
                zone = BiomeZone::Wetland;
            }
            else if (isDryChalk && height > config.grasslandMinHeight && slope < config.grasslandMaxSlope) {
                // Dry chalk downs - classic downland
                zone = BiomeZone::Grassland;
            }
            else if (isValleyBottom || inRiparianZone) {
                // Valley bottoms and riparian zones become woodland
                zone = BiomeZone::Woodland;
            }
            else if (isWetMeadow && slope < config.agriculturalMaxSlope) {
                // Wet meadow areas are good for agriculture (water meadows)
                zone = BiomeZone::Agricultural;
            }
            else if (height > config.grasslandMinHeight && slope < config.grasslandMaxSlope) {
                zone = BiomeZone::Grassland;
            }
            else if (slope < config.agriculturalMaxSlope &&
                     height > config.agriculturalMinHeight &&
                     height < config.agriculturalMaxHeight) {
                zone = BiomeZone::Agricultural;
            }
            else if (slope > 0.15f || nearRiver) {
                // Valleys, sheltered areas, steeper slopes = woodland
                zone = BiomeZone::Woodland;
            }

            result.cells[y * result.width + x].zone = zone;
        }
    }

    // Count zones
    std::vector<uint32_t> zoneCounts(static_cast<size_t>(BiomeZone::Count), 0);
    for (const auto& cell : result.cells) {
        zoneCounts[static_cast<size_t>(cell.zone)]++;
    }

    SDL_Log("Zone classification complete:");
    for (size_t i = 0; i < zoneCounts.size(); i++) {
        if (zoneCounts[i] > 0) {
            float percent = 100.0f * zoneCounts[i] / result.cells.size();
            SDL_Log("  %s: %u cells (%.1f%%)", getZoneName(static_cast<BiomeZone>(i)),
                    zoneCounts[i], percent);
        }
    }
}

void BiomeGenerator::applySubZoneNoise(ProgressCallback callback) {
    if (callback) callback(0.5f, "Applying sub-zone variation...");

    // Parallel sub-zone noise application
    #pragma omp parallel for schedule(dynamic, 64) collapse(2)
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            BiomeCell& cell = result.cells[y * result.width + x];

            // Generate noise for sub-zone selection
            float n1 = noise2D(worldX, worldZ, 0.001f);  // Large scale variation
            float n2 = noise2D(worldX, worldZ, 0.005f);  // Medium scale

            // Get basin label for watershed-based variation
            uint32_t basinLabel = WatershedMetrics::sampleBasinLabel(watershedMetrics, worldX, worldZ, config.terrainSize);

            // Generate basin-specific offset using basin label as seed
            // This creates distinct vegetation patterns per watershed
            float basinNoise = 0.0f;
            if (basinLabel > 0 && watershedMetrics.basinCount > 0) {
                // Use basin ID to create a deterministic but varied offset
                uint32_t basinHash = basinLabel * 2654435761u;  // Knuth multiplicative hash
                basinNoise = (static_cast<float>(basinHash & 0xFFFF) / 65535.0f - 0.5f) * 2.0f;  // [-1, 1]
            }

            // Combine noise values with basin variation
            float noiseVal = (n1 + n2 * 0.5f + basinNoise * config.basinVariationStrength) / (1.5f + config.basinVariationStrength);
            noiseVal = std::clamp((noiseVal + 1.0f) * 0.5f, 0.0f, 1.0f);  // [0, 1]

            // Map to sub-zone (4 sub-zones per zone type)
            uint8_t subZoneIdx = static_cast<uint8_t>(noiseVal * 3.99f);  // 0-3
            cell.subZone = static_cast<BiomeSubZone>(subZoneIdx);
        }
    }

    SDL_Log("Applied sub-zone noise variation with basin boundaries");
}

float BiomeGenerator::calculateSettlementScore(float x, float z) const {
    float height = sampleHeight(x, z);
    float slope = sampleSlope(x, z);

    // Can't build in sea
    if (height < config.seaLevel) return -100.0f;

    // Sample distances
    float u = x / config.terrainSize;
    float v = z / config.terrainSize;
    int px = static_cast<int>(std::clamp(u, 0.0f, 1.0f) * (result.width - 1));
    int py = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (result.height - 1));

    float distSea = result.distanceToSea[py * result.width + px];
    float distRiver = result.distanceToRiver[py * result.width + px];

    BiomeZone zone = result.cells[py * result.width + px].zone;

    float score = 0.0f;

    // Positive factors
    if (distRiver < 200.0f && distRiver > 20.0f) score += 3.0f;   // Near river but not flooded
    if (distSea < 500.0f && distSea > 50.0f) score += 2.0f;       // Near coast
    if (slope < 0.1f) score += 2.0f;                               // Flat, buildable
    if (height > 20.0f && height < 60.0f) score += 1.0f;          // Defensible elevation

    // Check for river crossing (high flow at low slope)
    float flow = sampleFlowAccumulation(x, z);
    if (flow > 0.2f && slope < 0.15f) score += 2.0f;              // Potential ford/crossing

    // Zone bonuses
    if (zone == BiomeZone::Agricultural) score += 1.0f;
    if (zone == BiomeZone::Grassland) score += 0.5f;

    // Negative factors
    if (zone == BiomeZone::SaltMarsh || zone == BiomeZone::Wetland) score -= 5.0f;
    if (zone == BiomeZone::ChalkCliff) score -= 3.0f;
    if (slope > 0.3f) score -= 3.0f;
    if (distRiver < 20.0f) score -= 4.0f;  // Flood zone

    return score;
}

bool BiomeGenerator::isValidSettlementLocation(float x, float z,
                                                const std::vector<Settlement>& existing) const {
    // Check minimum distance to existing settlements
    for (const auto& s : existing) {
        float dist = glm::distance(glm::vec2(x, z), s.position);

        float minDist = config.hamletMinDistance;
        if (s.type == SettlementType::Village || s.type == SettlementType::FishingVillage) {
            minDist = config.villageMinDistance;
        } else if (s.type == SettlementType::Town) {
            minDist = config.townMinDistance;
        }

        if (dist < minDist) return false;
    }
    return true;
}

void BiomeGenerator::placeSettlements(ProgressCallback callback) {
    if (callback) callback(0.6f, "Placing settlements...");

    // Grid sample candidate locations
    float sampleStep = 200.0f;  // Sample every 200m
    std::vector<std::pair<glm::vec2, float>> candidates;

    for (float z = sampleStep; z < config.terrainSize - sampleStep; z += sampleStep) {
        for (float x = sampleStep; x < config.terrainSize - sampleStep; x += sampleStep) {
            float score = calculateSettlementScore(x, z);
            if (score > 3.0f) {
                candidates.push_back({glm::vec2(x, z), score});
            }
        }
    }

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    SDL_Log("Found %zu settlement candidates", candidates.size());

    // Greedy placement
    uint32_t settlementId = 0;
    for (const auto& [pos, score] : candidates) {
        if (result.settlements.size() >= config.numSettlements) break;

        if (!isValidSettlementLocation(pos.x, pos.y, result.settlements)) continue;

        Settlement settlement;
        settlement.id = settlementId++;
        settlement.position = pos;
        settlement.score = score;

        // Determine type
        float distSea = result.distanceToSea[
            static_cast<int>(pos.y / config.terrainSize * (result.height - 1)) * result.width +
            static_cast<int>(pos.x / config.terrainSize * (result.width - 1))
        ];

        float flow = sampleFlowAccumulation(pos.x, pos.y);

        if (score > 8.0f && (distSea < 300.0f || flow > 0.4f)) {
            settlement.type = SettlementType::Town;
            settlement.features.push_back("market");
        } else if (distSea < 400.0f && score > 5.0f) {
            settlement.type = SettlementType::FishingVillage;
            settlement.features.push_back("harbour");
        } else if (score > 5.0f) {
            settlement.type = SettlementType::Village;
        } else {
            settlement.type = SettlementType::Hamlet;
        }

        // Add feature tags
        if (flow > 0.2f) settlement.features.push_back("river_access");
        if (distSea < 500.0f) settlement.features.push_back("coastal");

        int px = static_cast<int>(pos.x / config.terrainSize * (result.width - 1));
        int py = static_cast<int>(pos.y / config.terrainSize * (result.height - 1));
        BiomeZone zone = result.cells[py * result.width + px].zone;

        if (zone == BiomeZone::Agricultural) settlement.features.push_back("agricultural");
        if (zone == BiomeZone::Grassland) settlement.features.push_back("downland");

        result.settlements.push_back(settlement);
    }

    // Log settlement summary
    SDL_Log("Placed %zu settlements:", result.settlements.size());
    int towns = 0, villages = 0, hamlets = 0, fishing = 0;
    for (const auto& s : result.settlements) {
        switch (s.type) {
            case SettlementType::Town: towns++; break;
            case SettlementType::Village: villages++; break;
            case SettlementType::Hamlet: hamlets++; break;
            case SettlementType::FishingVillage: fishing++; break;
        }
    }
    SDL_Log("  Towns: %d, Villages: %d, Hamlets: %d, Fishing Villages: %d",
            towns, villages, hamlets, fishing);
}

void BiomeGenerator::computeSettlementDistances(ProgressCallback callback) {
    if (callback) callback(0.8f, "Computing settlement distances...");

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            float minDist = std::numeric_limits<float>::max();
            for (const auto& s : result.settlements) {
                float dist = glm::distance(glm::vec2(worldX, worldZ), s.position);
                minDist = std::min(minDist, dist);
            }

            result.cells[y * result.width + x].distanceToSettlement = minDist;
        }
    }
}

bool BiomeGenerator::generate(const BiomeConfig& cfg, ProgressCallback callback) {
    config = cfg;

    // Initialize result
    result.width = config.outputResolution;
    result.height = config.outputResolution;
    result.cells.resize(result.width * result.height);
    result.slopeMap.resize(result.width * result.height);
    result.distanceToSea.resize(result.width * result.height);
    result.distanceToRiver.resize(result.width * result.height);

    // Load input data
    if (!loadHeightmap(config.heightmapPath, callback)) return false;
    if (!loadErosionData(config.erosionCacheDir, callback)) return false;

    // Compute derived layers
    computeSlopeMap(callback);
    computeDistanceToSea(callback);
    computeDistanceToRiver(callback);

    // Compute watershed-derived metrics (TWI, stream order, basin labels)
    computeWatershedMetrics(callback);

    // Classify zones (now uses TWI and stream order)
    classifyZones(callback);
    applySubZoneNoise(callback);

    // Place settlements
    placeSettlements(callback);
    computeSettlementDistances(callback);

    if (callback) callback(1.0f, "Biome generation complete");

    return true;
}

bool BiomeGenerator::saveBiomeMap(const std::string& path) const {
    std::vector<uint8_t> imageData(result.width * result.height * 4);

    for (uint32_t i = 0; i < result.cells.size(); i++) {
        const BiomeCell& cell = result.cells[i];

        // R: zone ID
        imageData[i * 4 + 0] = static_cast<uint8_t>(cell.zone);
        // G: sub-zone
        imageData[i * 4 + 1] = static_cast<uint8_t>(cell.subZone);
        // B: settlement proximity (0-255 mapped from 0-2000m)
        float distNorm = std::min(cell.distanceToSettlement / 2000.0f, 1.0f);
        imageData[i * 4 + 2] = static_cast<uint8_t>(distNorm * 255.0f);
        // A: reserved
        imageData[i * 4 + 3] = 255;
    }

    unsigned error = lodepng::encode(path, imageData, result.width, result.height);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save biome map: %s",
                     lodepng_error_text(error));
        return false;
    }

    SDL_Log("Saved biome map: %s", path.c_str());
    return true;
}

bool BiomeGenerator::saveDebugVisualization(const std::string& path) const {
    std::vector<uint8_t> imageData(result.width * result.height * 4);

    for (uint32_t i = 0; i < result.cells.size(); i++) {
        const BiomeCell& cell = result.cells[i];
        glm::vec3 color = getZoneColor(cell.zone);

        // Add sub-zone variation
        float subZoneOffset = (static_cast<float>(cell.subZone) - 1.5f) * 0.05f;
        color = glm::clamp(color + subZoneOffset, 0.0f, 1.0f);

        imageData[i * 4 + 0] = static_cast<uint8_t>(color.r * 255.0f);
        imageData[i * 4 + 1] = static_cast<uint8_t>(color.g * 255.0f);
        imageData[i * 4 + 2] = static_cast<uint8_t>(color.b * 255.0f);
        imageData[i * 4 + 3] = 255;
    }

    // Draw settlements as circles
    for (const auto& s : result.settlements) {
        int cx = static_cast<int>(s.position.x / config.terrainSize * result.width);
        int cy = static_cast<int>(s.position.y / config.terrainSize * result.height);

        int radius = 3;
        if (s.type == SettlementType::Village || s.type == SettlementType::FishingVillage) radius = 5;
        if (s.type == SettlementType::Town) radius = 8;

        glm::vec3 color(1.0f, 0.2f, 0.2f);  // Red for settlements

        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius * radius) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < static_cast<int>(result.width) &&
                        py >= 0 && py < static_cast<int>(result.height)) {
                        int idx = py * result.width + px;
                        imageData[idx * 4 + 0] = static_cast<uint8_t>(color.r * 255.0f);
                        imageData[idx * 4 + 1] = static_cast<uint8_t>(color.g * 255.0f);
                        imageData[idx * 4 + 2] = static_cast<uint8_t>(color.b * 255.0f);
                    }
                }
            }
        }
    }

    unsigned error = lodepng::encode(path, imageData, result.width, result.height);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save debug visualization: %s",
                     lodepng_error_text(error));
        return false;
    }

    SDL_Log("Saved debug visualization: %s", path.c_str());
    return true;
}

bool BiomeGenerator::saveSettlements(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create settlements file: %s",
                     path.c_str());
        return false;
    }

    file << "{\n";
    file << "  \"terrain_size\": " << config.terrainSize << ",\n";
    file << "  \"settlements\": [\n";

    for (size_t i = 0; i < result.settlements.size(); i++) {
        const auto& s = result.settlements[i];

        file << "    {\n";
        file << "      \"id\": " << s.id << ",\n";
        file << "      \"type\": \"" << getSettlementTypeName(s.type) << "\",\n";
        file << "      \"x\": " << s.position.x << ",\n";
        file << "      \"z\": " << s.position.y << ",\n";
        file << "      \"score\": " << s.score << ",\n";
        file << "      \"features\": [";

        for (size_t j = 0; j < s.features.size(); j++) {
            file << "\"" << s.features[j] << "\"";
            if (j < s.features.size() - 1) file << ", ";
        }

        file << "]\n";
        file << "    }";
        if (i < result.settlements.size() - 1) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    SDL_Log("Saved settlements: %s (%zu settlements)", path.c_str(), result.settlements.size());
    return true;
}
