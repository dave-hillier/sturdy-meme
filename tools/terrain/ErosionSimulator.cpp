#include "ErosionSimulator.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <SDL3/SDL_log.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <queue>
#include <unordered_set>

namespace fs = std::filesystem;

// Cache file paths
std::string ErosionSimulator::getFlowMapPath(const std::string& cacheDir) {
    return cacheDir + "/flow_accumulation.raw";
}

std::string ErosionSimulator::getRiversPath(const std::string& cacheDir) {
    return cacheDir + "/rivers.dat";
}

std::string ErosionSimulator::getLakesPath(const std::string& cacheDir) {
    return cacheDir + "/lakes.dat";
}

std::string ErosionSimulator::getMetadataPath(const std::string& cacheDir) {
    return cacheDir + "/erosion_data.meta";
}

std::string ErosionSimulator::getPreviewPath(const std::string& cacheDir) {
    return cacheDir + "/erosion_preview.png";
}

bool ErosionSimulator::isCacheValid(const ErosionConfig& config) const {
    return loadAndValidateMetadata(config);
}

bool ErosionSimulator::loadAndValidateMetadata(const ErosionConfig& config) const {
    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        SDL_Log("Erosion cache: metadata file not found at %s", metaPath.c_str());
        return false;
    }

    std::string line;
    std::string cachedSourcePath;
    uint32_t cachedNumDroplets = 0;
    uint32_t cachedOutputRes = 0;
    float cachedRiverThreshold = 0;
    uintmax_t cachedSourceSize = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "source") cachedSourcePath = value;
            else if (key == "numDroplets") cachedNumDroplets = std::stoul(value);
            else if (key == "outputResolution") cachedOutputRes = std::stoul(value);
            else if (key == "riverFlowThreshold") cachedRiverThreshold = std::stof(value);
            else if (key == "sourceFileSize") cachedSourceSize = std::stoull(value);
        }
    }

    // Validate source file size matches (path may differ between preprocessing and runtime)
    std::error_code sizeEc;
    uintmax_t currentSourceSize = fs::file_size(config.sourceHeightmapPath, sizeEc);
    if (sizeEc || cachedSourceSize != currentSourceSize) {
        SDL_Log("Erosion cache: source file size mismatch (cached: %ju, current: %ju)",
                static_cast<uintmax_t>(cachedSourceSize),
                sizeEc ? static_cast<uintmax_t>(0) : static_cast<uintmax_t>(currentSourceSize));
        return false;
    }

    // Check all cache files exist
    if (!fs::exists(getFlowMapPath(config.cacheDirectory)) ||
        !fs::exists(getRiversPath(config.cacheDirectory)) ||
        !fs::exists(getLakesPath(config.cacheDirectory))) {
        SDL_Log("Erosion cache: missing cache files");
        return false;
    }

    SDL_Log("Erosion cache: valid cache found");
    return true;
}

bool ErosionSimulator::saveMetadata(const ErosionConfig& config) const {
    std::string metaPath = getMetadataPath(config.cacheDirectory);
    std::ofstream file(metaPath);
    if (!file.is_open()) {
        return false;
    }

    std::error_code ec;
    uintmax_t sourceFileSize = fs::file_size(config.sourceHeightmapPath, ec);
    if (ec) {
        return false;
    }

    file << "source=" << config.sourceHeightmapPath << "\n";
    file << "numDroplets=" << config.numDroplets << "\n";
    file << "outputResolution=" << config.outputResolution << "\n";
    file << "riverFlowThreshold=" << config.riverFlowThreshold << "\n";
    file << "sourceFileSize=" << sourceFileSize << "\n";

    return true;
}

bool ErosionSimulator::savePreviewImage(const ErosionConfig& config) const {
    // Create a simple water placement preview showing actual water locations:
    // - Gray = land
    // - Blue = sea (below sea level)
    // - Red = rivers (only the strongest streams, drawn on top)

    std::string previewPath = getPreviewPath(config.cacheDirectory);

    // Use flow map resolution for preview (or cap at 2048)
    uint32_t previewSize = std::min(flowWidth, 2048u);
    float heightScale = config.maxAltitude - config.minAltitude;

    // Sea level in normalized height space [0,1]
    float seaLevelNorm = (config.seaLevel - config.minAltitude) / heightScale;

    // Find threshold for top ~0.5% of flow values (strongest streams only)
    std::vector<float> flowSample;
    flowSample.reserve(flowAccum.size() / 16);
    for (size_t i = 0; i < flowAccum.size(); i += 16) {
        flowSample.push_back(flowAccum[i]);
    }
    std::sort(flowSample.begin(), flowSample.end());

    size_t percentileIdx = static_cast<size_t>(flowSample.size() * 0.995f);
    float riverThreshold = flowSample[std::min(percentileIdx, flowSample.size() - 1)];

    SDL_Log("Erosion preview: river threshold = %.4f (99.5th percentile)", riverThreshold);

    std::vector<uint8_t> pixels(previewSize * previewSize * 3);

    float heightToPreview = static_cast<float>(sourceWidth) / static_cast<float>(previewSize);
    float flowToPreview = static_cast<float>(flowWidth) / static_cast<float>(previewSize);

    // First pass: render terrain and sea
    for (uint32_t y = 0; y < previewSize; y++) {
        for (uint32_t x = 0; x < previewSize; x++) {
            size_t idx = (y * previewSize + x) * 3;

            float srcX = x * heightToPreview;
            float srcY = y * heightToPreview;
            float h = getHeightAt(srcX, srcY);

            if (h <= seaLevelNorm) {
                // Sea - blue
                pixels[idx + 0] = 30;
                pixels[idx + 1] = 100;
                pixels[idx + 2] = 200;
            } else {
                // Land - grayscale based on height
                uint8_t gray = static_cast<uint8_t>(60 + h * 120);
                pixels[idx + 0] = gray;
                pixels[idx + 1] = gray;
                pixels[idx + 2] = gray;
            }
        }
    }

    // Second pass: overlay rivers on top (including where they meet the sea)
    // Rivers are drawn ON TOP of everything so they visibly flow to the coast
    for (uint32_t y = 0; y < previewSize; y++) {
        for (uint32_t x = 0; x < previewSize; x++) {
            uint32_t flowX = static_cast<uint32_t>(x * flowToPreview);
            uint32_t flowY = static_cast<uint32_t>(y * flowToPreview);
            flowX = std::min(flowX, flowWidth - 1);
            flowY = std::min(flowY, flowHeight - 1);
            float flow = flowAccum[flowY * flowWidth + flowX];

            if (flow >= riverThreshold) {
                size_t idx = (y * previewSize + x) * 3;
                float t = (flow - riverThreshold) / (1.0f - riverThreshold);
                pixels[idx + 0] = static_cast<uint8_t>(180 + t * 75);
                pixels[idx + 1] = static_cast<uint8_t>(30 + t * 30);
                pixels[idx + 2] = static_cast<uint8_t>(30 + t * 30);
            }
        }
    }

    // Write PNG
    int result = stbi_write_png(previewPath.c_str(), previewSize, previewSize, 3,
                                 pixels.data(), previewSize * 3);

    if (result == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write erosion preview: %s",
                     previewPath.c_str());
        return false;
    }

    SDL_Log("Erosion preview saved: %s (%ux%u)", previewPath.c_str(), previewSize, previewSize);
    return true;
}

bool ErosionSimulator::loadSourceHeightmap(const std::string& path, float minAlt, float maxAlt) {
    // Try loading as 16-bit first
    int width, height, channels;
    uint16_t* data16 = stbi_load_16(path.c_str(), &width, &height, &channels, 1);

    if (data16) {
        sourceWidth = static_cast<uint32_t>(width);
        sourceHeight_ = static_cast<uint32_t>(height);
        sourceHeight.resize(sourceWidth * sourceHeight_);

        float range = maxAlt - minAlt;
        for (size_t i = 0; i < sourceHeight.size(); i++) {
            // Normalize to [0, 1] based on altitude range
            sourceHeight[i] = static_cast<float>(data16[i]) / 65535.0f;
        }

        stbi_image_free(data16);
        SDL_Log("Loaded 16-bit heightmap: %ux%u", sourceWidth, sourceHeight_);
        return true;
    }

    // Fall back to 8-bit
    uint8_t* data8 = stbi_load(path.c_str(), &width, &height, &channels, 1);
    if (data8) {
        sourceWidth = static_cast<uint32_t>(width);
        sourceHeight_ = static_cast<uint32_t>(height);
        sourceHeight.resize(sourceWidth * sourceHeight_);

        for (size_t i = 0; i < sourceHeight.size(); i++) {
            sourceHeight[i] = static_cast<float>(data8[i]) / 255.0f;
        }

        stbi_image_free(data8);
        SDL_Log("Loaded 8-bit heightmap: %ux%u", sourceWidth, sourceHeight_);
        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s", path.c_str());
    return false;
}

float ErosionSimulator::getHeightAt(float x, float y) const {
    // Bilinear interpolation
    float fx = std::clamp(x, 0.0f, static_cast<float>(sourceWidth - 1));
    float fy = std::clamp(y, 0.0f, static_cast<float>(sourceHeight_ - 1));

    uint32_t x0 = static_cast<uint32_t>(fx);
    uint32_t y0 = static_cast<uint32_t>(fy);
    uint32_t x1 = std::min(x0 + 1, sourceWidth - 1);
    uint32_t y1 = std::min(y0 + 1, sourceHeight_ - 1);

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = sourceHeight[y0 * sourceWidth + x0];
    float h10 = sourceHeight[y0 * sourceWidth + x1];
    float h01 = sourceHeight[y1 * sourceWidth + x0];
    float h11 = sourceHeight[y1 * sourceWidth + x1];

    float h0 = h00 * (1 - tx) + h10 * tx;
    float h1 = h01 * (1 - tx) + h11 * tx;

    return h0 * (1 - ty) + h1 * ty;
}

glm::vec2 ErosionSimulator::getGradientAt(float x, float y) const {
    // Central difference gradient
    float eps = 1.0f;
    float hL = getHeightAt(x - eps, y);
    float hR = getHeightAt(x + eps, y);
    float hD = getHeightAt(x, y - eps);
    float hU = getHeightAt(x, y + eps);

    return glm::vec2(hR - hL, hU - hD) / (2.0f * eps);
}

glm::vec2 ErosionSimulator::pixelToWorld(float px, float py, float terrainSize) const {
    // Map pixel coords [0, size) to world coords [-terrainSize/2, terrainSize/2]
    float u = px / static_cast<float>(sourceWidth);
    float v = py / static_cast<float>(sourceHeight_);
    return glm::vec2(
        (u - 0.5f) * terrainSize,
        (v - 0.5f) * terrainSize
    );
}

glm::vec2 ErosionSimulator::worldToPixel(float wx, float wy, float terrainSize) const {
    float u = (wx / terrainSize) + 0.5f;
    float v = (wy / terrainSize) + 0.5f;
    return glm::vec2(
        u * static_cast<float>(sourceWidth),
        v * static_cast<float>(sourceHeight_)
    );
}

void ErosionSimulator::simulateDroplets(const ErosionConfig& config, ErosionProgressCallback progressCallback) {
    // Use D8 flow accumulation algorithm instead of random droplets
    // This gives clean river networks by calculating upstream contributing area

    flowWidth = config.outputResolution;
    flowHeight = config.outputResolution;
    flowAccum.resize(flowWidth * flowHeight, 0.0f);

    // Scale factor from source heightmap to flow map
    float scaleX = static_cast<float>(sourceWidth) / static_cast<float>(flowWidth);
    float scaleY = static_cast<float>(sourceHeight_) / static_cast<float>(flowHeight);

    // Sea level in normalized height space
    float heightScale = config.maxAltitude - config.minAltitude;
    float seaLevelNorm = (config.seaLevel - config.minAltitude) / heightScale;

    if (progressCallback) {
        progressCallback(0.1f, "Computing flow directions (D8)...");
    }

    // D8 direction offsets (8 neighbors)
    // Index: 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE
    const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    // Distance weights (diagonal = sqrt(2))
    const float dist[8] = {1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f};

    // Step 1: Build flow direction map
    // flowDir[i] = direction index (0-7) that water flows to, or -1 for outlet (sea/edge)
    flowDir.resize(flowWidth * flowHeight, -1);

    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            // Sample height at this flow cell (use center of cell in source coords)
            float srcX = (x + 0.5f) * scaleX;
            float srcY = (y + 0.5f) * scaleY;
            float h = getHeightAt(srcX, srcY);

            // Cells at or below sea level are outlets - no flow direction needed
            if (h <= seaLevelNorm) {
                flowDir[y * flowWidth + x] = -1;
                continue;
            }

            // Find steepest downhill neighbor
            float maxSlope = 0.0f;
            int bestDir = -1;
            float lowestNeighborHeight = h;
            int lowestNeighborDir = -1;

            for (int d = 0; d < 8; d++) {
                int nx = static_cast<int>(x) + dx[d];
                int ny = static_cast<int>(y) + dy[d];

                if (nx < 0 || nx >= static_cast<int>(flowWidth) ||
                    ny < 0 || ny >= static_cast<int>(flowHeight)) {
                    continue;
                }

                float nSrcX = (nx + 0.5f) * scaleX;
                float nSrcY = (ny + 0.5f) * scaleY;
                float nh = getHeightAt(nSrcX, nSrcY);

                // Track lowest neighbor for pit-breaching
                if (nh < lowestNeighborHeight) {
                    lowestNeighborHeight = nh;
                    lowestNeighborDir = d;
                }

                // Slope = drop / distance
                float slope = (h - nh) / dist[d];

                if (slope > maxSlope) {
                    maxSlope = slope;
                    bestDir = d;
                }
            }

            // If no downhill neighbor found (internal pit), breach to lowest neighbor
            // This ensures water always flows toward lower areas eventually reaching sea
            if (bestDir < 0 && lowestNeighborDir >= 0) {
                bestDir = lowestNeighborDir;
            }

            flowDir[y * flowWidth + x] = static_cast<int8_t>(bestDir);
        }

        if (progressCallback && (y % (flowHeight / 20) == 0)) {
            float progress = 0.1f + (static_cast<float>(y) / flowHeight) * 0.3f;
            progressCallback(progress, "Computing flow directions (D8)...");
        }
    }

    if (progressCallback) {
        progressCallback(0.4f, "Computing flow accumulation...");
    }

    // Step 2: Compute flow accumulation using recursive upstream counting
    // Each cell starts with 1 (itself) and adds all upstream contributors

    // First, count how many cells flow INTO each cell (in-degree)
    std::vector<uint32_t> inDegree(flowWidth * flowHeight, 0);
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            int dir = flowDir[y * flowWidth + x];
            if (dir >= 0) {
                int nx = static_cast<int>(x) + dx[dir];
                int ny = static_cast<int>(y) + dy[dir];
                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    inDegree[ny * flowWidth + nx]++;
                }
            }
        }
    }

    // Initialize flow accumulation to 1 for each cell
    for (size_t i = 0; i < flowAccum.size(); i++) {
        flowAccum[i] = 1.0f;
    }

    // Process cells in topological order (cells with no upstream first)
    std::queue<std::pair<uint32_t, uint32_t>> toProcess;

    // Start with cells that have no upstream (in-degree = 0)
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            if (inDegree[y * flowWidth + x] == 0) {
                toProcess.push({x, y});
            }
        }
    }

    uint32_t processed = 0;
    uint32_t totalCells = flowWidth * flowHeight;

    while (!toProcess.empty()) {
        auto [x, y] = toProcess.front();
        toProcess.pop();
        processed++;

        int dir = flowDir[y * flowWidth + x];
        if (dir >= 0) {
            int nx = static_cast<int>(x) + dx[dir];
            int ny = static_cast<int>(y) + dy[dir];

            if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                ny >= 0 && ny < static_cast<int>(flowHeight)) {
                // Add this cell's accumulation to downstream cell
                flowAccum[ny * flowWidth + nx] += flowAccum[y * flowWidth + x];

                // Decrease in-degree of downstream cell
                inDegree[ny * flowWidth + nx]--;

                // If downstream cell has no more upstream to process, add to queue
                if (inDegree[ny * flowWidth + nx] == 0) {
                    toProcess.push({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                }
            }
        }

        if (progressCallback && (processed % (totalCells / 20) == 0)) {
            float progress = 0.4f + (static_cast<float>(processed) / totalCells) * 0.5f;
            progressCallback(progress, "Computing flow accumulation...");
        }
    }

    // Normalize flow accumulation (use log scale for better visualization)
    float maxFlow = 0.0f;
    for (float f : flowAccum) {
        maxFlow = std::max(maxFlow, f);
    }

    waterData.maxFlowValue = maxFlow;
    SDL_Log("Erosion: max flow accumulation = %.0f cells", maxFlow);

    // Normalize using log scale to make rivers visible
    // log(1) = 0, log(maxFlow) = max
    float logMax = std::log(maxFlow + 1.0f);
    for (float& f : flowAccum) {
        f = std::log(f + 1.0f) / logMax;
    }

    waterData.numDropletsSimulated = totalCells;  // Not really droplets anymore
}

RiverSpline ErosionSimulator::traceRiver(uint32_t startX, uint32_t startY, const ErosionConfig& config) {
    RiverSpline spline;

    float posX = static_cast<float>(startX);
    float posY = static_cast<float>(startY);

    // Scale factors
    float srcScaleX = static_cast<float>(sourceWidth) / static_cast<float>(flowWidth);
    float srcScaleY = static_cast<float>(sourceHeight_) / static_cast<float>(flowHeight);

    float heightScale = config.maxAltitude - config.minAltitude;

    while (true) {
        uint32_t fx = static_cast<uint32_t>(posX);
        uint32_t fy = static_cast<uint32_t>(posY);

        if (fx >= flowWidth || fy >= flowHeight) break;

        // Mark as visited
        riverVisited[fy * flowWidth + fx] = true;

        // Get world position
        float srcX = posX * srcScaleX;
        float srcY = posY * srcScaleY;
        glm::vec2 worldPos = pixelToWorld(srcX, srcY, config.terrainSize);
        float height = getHeightAt(srcX, srcY) * heightScale;

        // Calculate width from flow
        float flow = flowAccum[fy * flowWidth + fx];
        float widthT = std::sqrt(flow);  // Square root for more natural width distribution
        float width = config.riverMinWidth + widthT * (config.riverMaxWidth - config.riverMinWidth);

        spline.controlPoints.push_back(glm::vec3(worldPos.x, height, worldPos.y));
        spline.widths.push_back(width);
        spline.totalFlow += flow;

        // Find next position (follow downhill gradient in flow map)
        float bestFlow = 0.0f;
        int bestDx = 0, bestDy = 0;
        bool found = false;

        // Look for highest flow neighbor that is downhill or same level
        float currentHeight = getHeightAt(srcX, srcY);

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;

                int nx = static_cast<int>(fx) + dx;
                int ny = static_cast<int>(fy) + dy;

                if (nx < 0 || nx >= static_cast<int>(flowWidth) ||
                    ny < 0 || ny >= static_cast<int>(flowHeight)) continue;

                if (riverVisited[ny * flowWidth + nx]) continue;

                float neighborFlow = flowAccum[ny * flowWidth + nx];
                if (neighborFlow < config.riverFlowThreshold) continue;

                // Check height - should be same or lower
                float nSrcX = nx * srcScaleX;
                float nSrcY = ny * srcScaleY;
                float neighborHeight = getHeightAt(nSrcX, nSrcY);

                if (neighborHeight <= currentHeight + 0.001f) {
                    if (neighborFlow > bestFlow) {
                        bestFlow = neighborFlow;
                        bestDx = dx;
                        bestDy = dy;
                        found = true;
                    }
                }
            }
        }

        if (!found) break;

        posX += bestDx;
        posY += bestDy;

        // Safety limit
        if (spline.controlPoints.size() > 10000) break;
    }

    return spline;
}

void ErosionSimulator::simplifySpline(RiverSpline& spline, float tolerance) {
    if (spline.controlPoints.size() < 3) return;

    // Douglas-Peucker algorithm
    std::vector<bool> keep(spline.controlPoints.size(), false);
    keep[0] = true;
    keep[spline.controlPoints.size() - 1] = true;

    std::function<void(size_t, size_t)> simplifySection = [&](size_t start, size_t end) {
        if (end <= start + 1) return;

        // Find point with maximum distance from line
        glm::vec3 lineStart = spline.controlPoints[start];
        glm::vec3 lineEnd = spline.controlPoints[end];
        glm::vec3 lineDir = lineEnd - lineStart;
        float lineLen = glm::length(lineDir);

        if (lineLen < 0.0001f) return;

        lineDir /= lineLen;

        float maxDist = 0.0f;
        size_t maxIdx = start;

        for (size_t i = start + 1; i < end; i++) {
            glm::vec3 toPoint = spline.controlPoints[i] - lineStart;
            float proj = glm::dot(toPoint, lineDir);
            glm::vec3 closestOnLine = lineStart + lineDir * proj;
            float dist = glm::length(spline.controlPoints[i] - closestOnLine);

            if (dist > maxDist) {
                maxDist = dist;
                maxIdx = i;
            }
        }

        if (maxDist > tolerance) {
            keep[maxIdx] = true;
            simplifySection(start, maxIdx);
            simplifySection(maxIdx, end);
        }
    };

    simplifySection(0, spline.controlPoints.size() - 1);

    // Build simplified spline
    std::vector<glm::vec3> newPoints;
    std::vector<float> newWidths;

    for (size_t i = 0; i < spline.controlPoints.size(); i++) {
        if (keep[i]) {
            newPoints.push_back(spline.controlPoints[i]);
            newWidths.push_back(spline.widths[i]);
        }
    }

    spline.controlPoints = std::move(newPoints);
    spline.widths = std::move(newWidths);
}

void ErosionSimulator::extractRivers(const ErosionConfig& config, ErosionProgressCallback progressCallback) {
    if (progressCallback) {
        progressCallback(0.6f, "Extracting rivers...");
    }

    riverVisited.resize(flowWidth * flowHeight, false);

    // Find high-flow starting points (local maxima above threshold)
    std::vector<std::pair<float, std::pair<uint32_t, uint32_t>>> candidates;

    for (uint32_t y = 1; y < flowHeight - 1; y++) {
        for (uint32_t x = 1; x < flowWidth - 1; x++) {
            float flow = flowAccum[y * flowWidth + x];
            if (flow < config.riverFlowThreshold) continue;

            // Check if local maximum
            bool isMax = true;
            for (int dy = -1; dy <= 1 && isMax; dy++) {
                for (int dx = -1; dx <= 1 && isMax; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (flowAccum[(y + dy) * flowWidth + (x + dx)] > flow) {
                        isMax = false;
                    }
                }
            }

            if (isMax) {
                candidates.push_back({flow, {x, y}});
            }
        }
    }

    // Sort by flow (highest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    SDL_Log("Erosion: found %zu river source candidates", candidates.size());

    // Trace rivers from high-flow sources
    for (const auto& candidate : candidates) {
        uint32_t x = candidate.second.first;
        uint32_t y = candidate.second.second;

        if (riverVisited[y * flowWidth + x]) continue;

        RiverSpline river = traceRiver(x, y, config);

        // Only keep rivers with enough points
        if (river.controlPoints.size() >= 10) {
            simplifySpline(river, config.splineSimplifyTolerance);

            if (river.controlPoints.size() >= 3) {
                waterData.rivers.push_back(std::move(river));
            }
        }
    }

    SDL_Log("Erosion: extracted %zu rivers", waterData.rivers.size());
}

Lake ErosionSimulator::floodFillLake(uint32_t startX, uint32_t startY, const ErosionConfig& config,
                                      std::vector<bool>& visited) {
    Lake lake;

    float srcScaleX = static_cast<float>(sourceWidth) / static_cast<float>(flowWidth);
    float srcScaleY = static_cast<float>(sourceHeight_) / static_cast<float>(flowHeight);
    float heightScale = config.maxAltitude - config.minAltitude;

    // Get the starting height (depression minimum)
    float srcX = startX * srcScaleX;
    float srcY = startY * srcScaleY;
    float minHeight = getHeightAt(srcX, srcY);

    // Find the spillover height by flood filling
    std::queue<std::pair<uint32_t, uint32_t>> queue;
    std::vector<std::pair<uint32_t, uint32_t>> lakePixels;

    queue.push({startX, startY});
    visited[startY * flowWidth + startX] = true;

    float spillHeight = minHeight;
    float maxSearchHeight = minHeight + 0.05f;  // Max 5% of height range for lake depth

    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop();

        float sx = x * srcScaleX;
        float sy = y * srcScaleY;
        float h = getHeightAt(sx, sy);

        if (h > maxSearchHeight) {
            spillHeight = std::max(spillHeight, h);
            continue;  // This is the edge
        }

        lakePixels.push_back({x, y});

        // Check neighbors
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;

                int nx = static_cast<int>(x) + dx;
                int ny = static_cast<int>(y) + dy;

                if (nx < 0 || nx >= static_cast<int>(flowWidth) ||
                    ny < 0 || ny >= static_cast<int>(flowHeight)) continue;

                if (visited[ny * flowWidth + nx]) continue;

                visited[ny * flowWidth + nx] = true;

                float nsx = nx * srcScaleX;
                float nsy = ny * srcScaleY;
                float nh = getHeightAt(nsx, nsy);

                // Include if below spill threshold
                if (nh <= maxSearchHeight) {
                    queue.push({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                } else {
                    spillHeight = std::max(spillHeight, nh);
                }
            }
        }
    }

    if (lakePixels.empty()) {
        lake.area = 0;
        return lake;
    }

    // Calculate lake properties
    float sumX = 0, sumZ = 0;
    float maxDist = 0;

    for (const auto& [x, y] : lakePixels) {
        float sx = x * srcScaleX;
        float sy = y * srcScaleY;
        glm::vec2 worldPos = pixelToWorld(sx, sy, config.terrainSize);
        sumX += worldPos.x;
        sumZ += worldPos.y;
    }

    lake.position = glm::vec2(sumX / lakePixels.size(), sumZ / lakePixels.size());
    lake.waterLevel = spillHeight * heightScale;
    lake.depth = (spillHeight - minHeight) * heightScale;

    // Calculate approximate radius
    for (const auto& [x, y] : lakePixels) {
        float sx = x * srcScaleX;
        float sy = y * srcScaleY;
        glm::vec2 worldPos = pixelToWorld(sx, sy, config.terrainSize);
        float dist = glm::length(worldPos - lake.position);
        maxDist = std::max(maxDist, dist);
    }

    lake.radius = maxDist;

    // Calculate area (in world units squared)
    float pixelSize = config.terrainSize / flowWidth;
    lake.area = lakePixels.size() * pixelSize * pixelSize;

    return lake;
}

void ErosionSimulator::detectLakes(const ErosionConfig& config, ErosionProgressCallback progressCallback) {
    if (progressCallback) {
        progressCallback(0.8f, "Detecting lakes...");
    }

    std::vector<bool> visited(flowWidth * flowHeight, false);

    float srcScaleX = static_cast<float>(sourceWidth) / static_cast<float>(flowWidth);
    float srcScaleY = static_cast<float>(sourceHeight_) / static_cast<float>(flowHeight);

    // Find local minima (depressions)
    std::vector<std::pair<uint32_t, uint32_t>> depressions;

    for (uint32_t y = 1; y < flowHeight - 1; y++) {
        for (uint32_t x = 1; x < flowWidth - 1; x++) {
            float sx = x * srcScaleX;
            float sy = y * srcScaleY;
            float h = getHeightAt(sx, sy);

            // Skip areas at sea level
            float heightScale = config.maxAltitude - config.minAltitude;
            if (h * heightScale <= config.seaLevel) continue;

            // Check if local minimum
            bool isMin = true;
            for (int dy = -1; dy <= 1 && isMin; dy++) {
                for (int dx = -1; dx <= 1 && isMin; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    float nsx = (x + dx) * srcScaleX;
                    float nsy = (y + dy) * srcScaleY;
                    if (getHeightAt(nsx, nsy) < h) {
                        isMin = false;
                    }
                }
            }

            if (isMin) {
                depressions.push_back({x, y});
            }
        }
    }

    SDL_Log("Erosion: found %zu depression candidates", depressions.size());

    // Flood fill each depression to find lakes
    for (const auto& [x, y] : depressions) {
        if (visited[y * flowWidth + x]) continue;

        Lake lake = floodFillLake(x, y, config, visited);

        if (lake.area >= config.lakeMinArea && lake.depth >= config.lakeMinDepth) {
            waterData.lakes.push_back(lake);
        }
    }

    SDL_Log("Erosion: detected %zu lakes", waterData.lakes.size());
}

bool ErosionSimulator::saveToCache(const ErosionConfig& config) const {
    fs::create_directories(config.cacheDirectory);

    // Save flow map and direction
    {
        std::ofstream file(getFlowMapPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&flowWidth), sizeof(flowWidth));
        file.write(reinterpret_cast<const char*>(&flowHeight), sizeof(flowHeight));
        file.write(reinterpret_cast<const char*>(flowAccum.data()),
                   flowAccum.size() * sizeof(float));
        file.write(reinterpret_cast<const char*>(flowDir.data()),
                   flowDir.size() * sizeof(int8_t));
    }

    // Save rivers
    {
        std::ofstream file(getRiversPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numRivers = static_cast<uint32_t>(waterData.rivers.size());
        file.write(reinterpret_cast<const char*>(&numRivers), sizeof(numRivers));

        for (const auto& river : waterData.rivers) {
            uint32_t numPoints = static_cast<uint32_t>(river.controlPoints.size());
            file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));
            file.write(reinterpret_cast<const char*>(river.controlPoints.data()),
                       numPoints * sizeof(glm::vec3));
            file.write(reinterpret_cast<const char*>(river.widths.data()),
                       numPoints * sizeof(float));
            file.write(reinterpret_cast<const char*>(&river.totalFlow), sizeof(float));
        }
    }

    // Save lakes
    {
        std::ofstream file(getLakesPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numLakes = static_cast<uint32_t>(waterData.lakes.size());
        file.write(reinterpret_cast<const char*>(&numLakes), sizeof(numLakes));

        for (const auto& lake : waterData.lakes) {
            file.write(reinterpret_cast<const char*>(&lake.position), sizeof(glm::vec2));
            file.write(reinterpret_cast<const char*>(&lake.waterLevel), sizeof(float));
            file.write(reinterpret_cast<const char*>(&lake.radius), sizeof(float));
            file.write(reinterpret_cast<const char*>(&lake.area), sizeof(float));
            file.write(reinterpret_cast<const char*>(&lake.depth), sizeof(float));
        }
    }

    // Save preview image for visualization
    savePreviewImage(config);

    return saveMetadata(config);
}

bool ErosionSimulator::loadFromCache(const ErosionConfig& config) {
    // Load flow map and direction
    {
        std::ifstream file(getFlowMapPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&flowWidth), sizeof(flowWidth));
        file.read(reinterpret_cast<char*>(&flowHeight), sizeof(flowHeight));

        flowAccum.resize(flowWidth * flowHeight);
        file.read(reinterpret_cast<char*>(flowAccum.data()),
                  flowAccum.size() * sizeof(float));

        // Try to load flow direction (may not exist in older caches)
        flowDir.resize(flowWidth * flowHeight, -1);
        file.read(reinterpret_cast<char*>(flowDir.data()),
                  flowDir.size() * sizeof(int8_t));

        waterData.flowAccumulation = flowAccum;
        waterData.flowDirection = flowDir;
        waterData.flowMapWidth = flowWidth;
        waterData.flowMapHeight = flowHeight;
    }

    // Load rivers
    {
        std::ifstream file(getRiversPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numRivers;
        file.read(reinterpret_cast<char*>(&numRivers), sizeof(numRivers));

        waterData.rivers.resize(numRivers);
        for (auto& river : waterData.rivers) {
            uint32_t numPoints;
            file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));

            river.controlPoints.resize(numPoints);
            river.widths.resize(numPoints);

            file.read(reinterpret_cast<char*>(river.controlPoints.data()),
                      numPoints * sizeof(glm::vec3));
            file.read(reinterpret_cast<char*>(river.widths.data()),
                      numPoints * sizeof(float));
            file.read(reinterpret_cast<char*>(&river.totalFlow), sizeof(float));
        }
    }

    // Load lakes
    {
        std::ifstream file(getLakesPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t numLakes;
        file.read(reinterpret_cast<char*>(&numLakes), sizeof(numLakes));

        waterData.lakes.resize(numLakes);
        for (auto& lake : waterData.lakes) {
            file.read(reinterpret_cast<char*>(&lake.position), sizeof(glm::vec2));
            file.read(reinterpret_cast<char*>(&lake.waterLevel), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.radius), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.area), sizeof(float));
            file.read(reinterpret_cast<char*>(&lake.depth), sizeof(float));
        }
    }

    waterData.seaLevel = config.seaLevel;
    SDL_Log("Erosion: loaded from cache - %zu rivers, %zu lakes",
            waterData.rivers.size(), waterData.lakes.size());

    return true;
}

bool ErosionSimulator::simulate(const ErosionConfig& config, ErosionProgressCallback progressCallback) {
    if (progressCallback) {
        progressCallback(0.0f, "Loading heightmap...");
    }

    // Load source heightmap at full resolution
    if (!loadSourceHeightmap(config.sourceHeightmapPath, config.minAltitude, config.maxAltitude)) {
        return false;
    }

    if (progressCallback) {
        progressCallback(0.1f, "Starting erosion simulation...");
    }

    // Run droplet simulation
    simulateDroplets(config, progressCallback);

    // Extract rivers from flow accumulation
    extractRivers(config, progressCallback);

    // Detect lakes from terrain depressions
    detectLakes(config, progressCallback);

    // Copy flow data to output
    waterData.flowAccumulation = flowAccum;
    waterData.flowDirection = flowDir;
    waterData.flowMapWidth = flowWidth;
    waterData.flowMapHeight = flowHeight;
    waterData.seaLevel = config.seaLevel;

    // Save to cache
    if (progressCallback) {
        progressCallback(0.95f, "Saving to cache...");
    }

    if (!saveToCache(config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save erosion cache");
        return false;
    }

    if (progressCallback) {
        progressCallback(1.0f, "Erosion simulation complete!");
    }

    SDL_Log("Erosion simulation complete:");
    SDL_Log("  - %u droplets simulated", waterData.numDropletsSimulated);
    SDL_Log("  - %zu rivers extracted", waterData.rivers.size());
    SDL_Log("  - %zu lakes detected", waterData.lakes.size());
    SDL_Log("  - Flow map: %ux%u", waterData.flowMapWidth, waterData.flowMapHeight);

    return true;
}
