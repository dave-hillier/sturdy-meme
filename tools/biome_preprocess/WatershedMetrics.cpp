#include "WatershedMetrics.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
    // D8 flow direction offsets
    const int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};

    float sampleFromGrid(const std::vector<float>& data, uint32_t gridWidth, uint32_t gridHeight,
                         float x, float z, float terrainSize) {
        float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
        float v = std::clamp(z / terrainSize, 0.0f, 1.0f);
        int px = static_cast<int>(u * (gridWidth - 1));
        int py = static_cast<int>(v * (gridHeight - 1));
        px = std::clamp(px, 0, static_cast<int>(gridWidth) - 1);
        py = std::clamp(py, 0, static_cast<int>(gridHeight) - 1);
        return data[py * gridWidth + px];
    }

    int8_t sampleFlowDir(const std::vector<int8_t>& flowDir, uint32_t width, uint32_t height,
                         float x, float z, float terrainSize) {
        float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
        float v = std::clamp(z / terrainSize, 0.0f, 1.0f);
        int px = static_cast<int>(u * (width - 1));
        int py = static_cast<int>(v * (height - 1));
        px = std::clamp(px, 0, static_cast<int>(width) - 1);
        py = std::clamp(py, 0, static_cast<int>(height) - 1);
        return flowDir[py * width + px];
    }
}

void WatershedMetrics::computeTWI(
    WatershedMetricsResult& result,
    const std::vector<float>& slopeMap,
    const std::vector<float>& flowAccumulation,
    uint32_t flowMapWidth,
    uint32_t flowMapHeight,
    uint32_t outputWidth,
    uint32_t outputHeight,
    float terrainSize,
    ProgressCallback callback
) {
    if (callback) callback(0.22f, "Computing Topographic Wetness Index...");

    result.width = outputWidth;
    result.height = outputHeight;
    result.twiMap.resize(outputWidth * outputHeight);

    const float minSlope = 0.001f;
    const float epsilon = 0.0001f;

    float maxTwi = 0.0f;
    float minTwi = std::numeric_limits<float>::max();

    // Parallel TWI computation with reduction for min/max
    #pragma omp parallel for schedule(dynamic, 64) collapse(2) reduction(max:maxTwi) reduction(min:minTwi)
    for (uint32_t y = 0; y < outputHeight; y++) {
        for (uint32_t x = 0; x < outputWidth; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / outputWidth * terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / outputHeight * terrainSize;

            float slope = slopeMap[y * outputWidth + x];
            float flow = sampleFromGrid(flowAccumulation, flowMapWidth, flowMapHeight, worldX, worldZ, terrainSize);

            float tanSlope = std::max(slope, minSlope);
            float upstreamArea = (flow + epsilon) * (flowMapWidth * flowMapHeight);
            float twi = std::log(upstreamArea / tanSlope);

            result.twiMap[y * outputWidth + x] = twi;
            maxTwi = std::max(maxTwi, twi);
            minTwi = std::min(minTwi, twi);
        }
    }

    SDL_Log("Computed TWI map: range [%.2f, %.2f]", minTwi, maxTwi);
}

void WatershedMetrics::computeStreamOrder(
    WatershedMetricsResult& result,
    const std::vector<float>& flowAccumulation,
    const std::vector<int8_t>& flowDirection,
    const std::vector<float>& heightData,
    uint32_t flowMapWidth,
    uint32_t flowMapHeight,
    uint32_t heightmapWidth,
    uint32_t heightmapHeight,
    const WatershedMetricsConfig& config,
    ProgressCallback callback
) {
    if (callback) callback(0.25f, "Computing stream order...");

    result.streamOrderMap.resize(result.width * result.height, 0);

    // Identify river cells
    std::vector<bool> isRiver(result.width * result.height, false);

    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
            float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;

            float flow = sampleFromGrid(flowAccumulation, flowMapWidth, flowMapHeight, worldX, worldZ, config.terrainSize);
            float height = sampleFromGrid(heightData, heightmapWidth, heightmapHeight, worldX, worldZ, config.terrainSize);

            if (flow > config.riverFlowThreshold && height >= config.seaLevel) {
                isRiver[y * result.width + x] = true;
            }
        }
    }

    // Sort river cells by flow (ascending)
    std::vector<std::tuple<uint32_t, uint32_t, float>> riverCells;
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            if (isRiver[y * result.width + x]) {
                float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
                float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;
                float flow = sampleFromGrid(flowAccumulation, flowMapWidth, flowMapHeight, worldX, worldZ, config.terrainSize);
                riverCells.push_back({x, y, flow});
            }
        }
    }

    std::sort(riverCells.begin(), riverCells.end(),
              [](const auto& a, const auto& b) { return std::get<2>(a) < std::get<2>(b); });

    // Assign stream orders
    for (const auto& [x, y, flow] : riverCells) {
        uint32_t idx = y * result.width + x;
        std::vector<uint8_t> upstreamOrders;

        for (int d = 0; d < 8; d++) {
            int nx = static_cast<int>(x) + dx[d];
            int ny = static_cast<int>(y) + dy[d];

            if (nx >= 0 && nx < static_cast<int>(result.width) &&
                ny >= 0 && ny < static_cast<int>(result.height)) {

                uint32_t nidx = ny * result.width + nx;
                float nWorldX = (static_cast<float>(nx) + 0.5f) / result.width * config.terrainSize;
                float nWorldZ = (static_cast<float>(ny) + 0.5f) / result.height * config.terrainSize;
                int8_t nDir = sampleFlowDir(flowDirection, flowMapWidth, flowMapHeight, nWorldX, nWorldZ, config.terrainSize);

                if (nDir >= 0 && nDir < 8) {
                    int targetX = nx + dx[nDir];
                    int targetY = ny + dy[nDir];
                    if (targetX == static_cast<int>(x) && targetY == static_cast<int>(y)) {
                        if (result.streamOrderMap[nidx] > 0) {
                            upstreamOrders.push_back(result.streamOrderMap[nidx]);
                        }
                    }
                }
            }
        }

        if (upstreamOrders.empty()) {
            result.streamOrderMap[idx] = 1;
        } else {
            std::sort(upstreamOrders.begin(), upstreamOrders.end(), std::greater<uint8_t>());
            uint8_t maxOrder = upstreamOrders[0];
            int countMax = std::count(upstreamOrders.begin(), upstreamOrders.end(), maxOrder);
            result.streamOrderMap[idx] = (countMax >= 2) ? maxOrder + 1 : maxOrder;
        }
    }

    // Log statistics
    std::vector<uint32_t> orderCounts(10, 0);
    uint8_t maxOrder = 0;
    for (uint32_t i = 0; i < result.streamOrderMap.size(); i++) {
        if (result.streamOrderMap[i] > 0 && result.streamOrderMap[i] < 10) {
            orderCounts[result.streamOrderMap[i]]++;
            maxOrder = std::max(maxOrder, result.streamOrderMap[i]);
        }
    }

    SDL_Log("Computed stream orders (max order: %d):", maxOrder);
    for (uint8_t i = 1; i <= maxOrder; i++) {
        SDL_Log("  Order %d: %u cells", i, orderCounts[i]);
    }
}

void WatershedMetrics::loadOrGenerateBasins(
    WatershedMetricsResult& result,
    const std::vector<float>& heightData,
    const std::vector<int8_t>& flowDirection,
    uint32_t heightmapWidth,
    uint32_t heightmapHeight,
    uint32_t flowMapWidth,
    uint32_t flowMapHeight,
    const WatershedMetricsConfig& config,
    ProgressCallback callback
) {
    if (callback) callback(0.28f, "Loading watershed basins...");

    std::string basinPath = config.erosionCacheDir + "/watershed_labels.bin";
    std::ifstream basinFile(basinPath, std::ios::binary);

    if (!basinFile.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Watershed basin data not found at %s, generating from flow", basinPath.c_str());

        result.basinLabels.resize(result.width * result.height, 0);

        uint32_t basinId = 1;
        for (uint32_t y = 0; y < result.height; y++) {
            for (uint32_t x = 0; x < result.width; x++) {
                if (result.basinLabels[y * result.width + x] != 0) continue;

                float worldX = (static_cast<float>(x) + 0.5f) / result.width * config.terrainSize;
                float worldZ = (static_cast<float>(y) + 0.5f) / result.height * config.terrainSize;
                float height = sampleFromGrid(heightData, heightmapWidth, heightmapHeight, worldX, worldZ, config.terrainSize);

                if (height < config.seaLevel) continue;

                std::vector<std::pair<uint32_t, uint32_t>> path;
                uint32_t cx = x, cy = y;
                uint32_t foundBasin = 0;

                while (true) {
                    uint32_t cidx = cy * result.width + cx;

                    if (result.basinLabels[cidx] != 0) {
                        foundBasin = result.basinLabels[cidx];
                        break;
                    }

                    float cworldX = (static_cast<float>(cx) + 0.5f) / result.width * config.terrainSize;
                    float cworldZ = (static_cast<float>(cy) + 0.5f) / result.height * config.terrainSize;
                    float ch = sampleFromGrid(heightData, heightmapWidth, heightmapHeight, cworldX, cworldZ, config.terrainSize);

                    if (ch < config.seaLevel) {
                        foundBasin = basinId++;
                        break;
                    }

                    path.push_back({cx, cy});

                    int8_t dir = sampleFlowDir(flowDirection, flowMapWidth, flowMapHeight, cworldX, cworldZ, config.terrainSize);
                    if (dir < 0 || dir > 7) {
                        foundBasin = basinId++;
                        break;
                    }

                    int ncx = static_cast<int>(cx) + dx[dir];
                    int ncy = static_cast<int>(cy) + dy[dir];

                    if (ncx < 0 || ncx >= static_cast<int>(result.width) ||
                        ncy < 0 || ncy >= static_cast<int>(result.height)) {
                        foundBasin = basinId++;
                        break;
                    }

                    cx = static_cast<uint32_t>(ncx);
                    cy = static_cast<uint32_t>(ncy);

                    if (path.size() > result.width * result.height) {
                        foundBasin = basinId++;
                        break;
                    }
                }

                for (const auto& [px, py] : path) {
                    result.basinLabels[py * result.width + px] = foundBasin;
                }
            }
        }

        result.basinCount = basinId - 1;
        SDL_Log("Generated %u watershed basins from flow directions", result.basinCount);
        return;
    }

    uint32_t width, height;
    basinFile.read(reinterpret_cast<char*>(&width), sizeof(uint32_t));
    basinFile.read(reinterpret_cast<char*>(&height), sizeof(uint32_t));
    basinFile.read(reinterpret_cast<char*>(&result.basinCount), sizeof(uint32_t));

    std::vector<uint32_t> rawLabels(width * height);
    basinFile.read(reinterpret_cast<char*>(rawLabels.data()), width * height * sizeof(uint32_t));

    result.basinLabels.resize(result.width * result.height);
    for (uint32_t y = 0; y < result.height; y++) {
        for (uint32_t x = 0; x < result.width; x++) {
            uint32_t srcX = x * width / result.width;
            uint32_t srcY = y * height / result.height;
            result.basinLabels[y * result.width + x] = rawLabels[srcY * width + srcX];
        }
    }

    SDL_Log("Loaded %u watershed basins from %s", result.basinCount, basinPath.c_str());
}

float WatershedMetrics::sampleTWI(const WatershedMetricsResult& result, float x, float z, float terrainSize) {
    if (result.twiMap.empty()) return 0.0f;
    float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = std::clamp(z / terrainSize, 0.0f, 1.0f);
    int px = static_cast<int>(u * (result.width - 1));
    int py = static_cast<int>(v * (result.height - 1));
    px = std::clamp(px, 0, static_cast<int>(result.width) - 1);
    py = std::clamp(py, 0, static_cast<int>(result.height) - 1);
    return result.twiMap[py * result.width + px];
}

uint8_t WatershedMetrics::sampleStreamOrder(const WatershedMetricsResult& result, float x, float z, float terrainSize) {
    if (result.streamOrderMap.empty()) return 0;
    float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = std::clamp(z / terrainSize, 0.0f, 1.0f);
    int px = static_cast<int>(u * (result.width - 1));
    int py = static_cast<int>(v * (result.height - 1));
    px = std::clamp(px, 0, static_cast<int>(result.width) - 1);
    py = std::clamp(py, 0, static_cast<int>(result.height) - 1);
    return result.streamOrderMap[py * result.width + px];
}

uint32_t WatershedMetrics::sampleBasinLabel(const WatershedMetricsResult& result, float x, float z, float terrainSize) {
    if (result.basinLabels.empty()) return 0;
    float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = std::clamp(z / terrainSize, 0.0f, 1.0f);
    int px = static_cast<int>(u * (result.width - 1));
    int py = static_cast<int>(v * (result.height - 1));
    px = std::clamp(px, 0, static_cast<int>(result.width) - 1);
    py = std::clamp(py, 0, static_cast<int>(result.height) - 1);
    return result.basinLabels[py * result.width + px];
}
