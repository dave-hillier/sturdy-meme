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
    return cacheDir + "/erosion_cache.meta";
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

    // Validate config matches
    std::error_code ec;
    fs::path cachedCanonical = fs::canonical(cachedSourcePath, ec);
    if (ec) {
        SDL_Log("Erosion cache: cached source path invalid");
        return false;
    }
    fs::path configCanonical = fs::canonical(config.sourceHeightmapPath, ec);
    if (ec) {
        SDL_Log("Erosion cache: config source path invalid");
        return false;
    }
    if (cachedCanonical != configCanonical) {
        SDL_Log("Erosion cache: source path mismatch");
        return false;
    }
    if (cachedNumDroplets != config.numDroplets) {
        SDL_Log("Erosion cache: numDroplets mismatch");
        return false;
    }
    if (cachedOutputRes != config.outputResolution) {
        SDL_Log("Erosion cache: outputResolution mismatch");
        return false;
    }
    if (std::abs(cachedRiverThreshold - config.riverFlowThreshold) > 0.001f) {
        SDL_Log("Erosion cache: riverFlowThreshold mismatch");
        return false;
    }

    // Check source file size
    std::error_code sizeEc;
    uintmax_t currentSourceSize = fs::file_size(config.sourceHeightmapPath, sizeEc);
    if (sizeEc || cachedSourceSize != currentSourceSize) {
        SDL_Log("Erosion cache: source file changed");
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
    // Create a simple water placement preview:
    // - Dark gray/black = land
    // - Blue = sea (below sea level)
    // - Green = lakes
    // - Red = rivers

    std::string previewPath = getPreviewPath(config.cacheDirectory);

    // Use flow map resolution for preview (or cap at 2048)
    uint32_t previewSize = std::min(flowWidth, 2048u);
    float scale = static_cast<float>(sourceWidth) / static_cast<float>(previewSize);

    std::vector<uint8_t> pixels(previewSize * previewSize * 3);

    float heightScale = config.maxAltitude - config.minAltitude;
    float seaLevelNorm = config.seaLevel / heightScale;

    // First pass: render terrain with sea level
    for (uint32_t y = 0; y < previewSize; y++) {
        for (uint32_t x = 0; x < previewSize; x++) {
            // Sample height from source heightmap
            float srcX = x * scale;
            float srcY = y * scale;
            float h = getHeightAt(srcX, srcY);

            size_t idx = (y * previewSize + x) * 3;

            if (h <= seaLevelNorm) {
                // Sea - blue
                pixels[idx + 0] = 30;
                pixels[idx + 1] = 100;
                pixels[idx + 2] = 200;
            } else {
                // Land - dark grayscale based on height
                uint8_t gray = static_cast<uint8_t>(40 + h * 80);
                pixels[idx + 0] = gray;
                pixels[idx + 1] = gray;
                pixels[idx + 2] = gray;
            }
        }
    }

    // Helper to convert world coords to preview pixel coords
    auto worldToPreview = [&](float worldX, float worldZ) -> std::pair<int, int> {
        float u = (worldX / config.terrainSize) + 0.5f;
        float v = (worldZ / config.terrainSize) + 0.5f;
        int px = static_cast<int>(u * previewSize);
        int py = static_cast<int>(v * previewSize);
        return {px, py};
    };

    float worldToPixelScale = static_cast<float>(previewSize) / config.terrainSize;

    // Second pass: draw lakes in green
    for (const auto& lake : waterData.lakes) {
        auto [cx, cy] = worldToPreview(lake.position.x, lake.position.y);
        int radius = std::max(2, static_cast<int>(lake.radius * worldToPixelScale));

        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius * radius) {
                    int nx = cx + dx;
                    int ny = cy + dy;
                    if (nx >= 0 && nx < static_cast<int>(previewSize) &&
                        ny >= 0 && ny < static_cast<int>(previewSize)) {
                        size_t idx = (ny * previewSize + nx) * 3;
                        pixels[idx + 0] = 50;
                        pixels[idx + 1] = 200;
                        pixels[idx + 2] = 80;
                    }
                }
            }
        }
    }

    // Third pass: draw rivers in red (draw lines between control points)
    for (const auto& river : waterData.rivers) {
        for (size_t i = 0; i + 1 < river.controlPoints.size(); i++) {
            const auto& p0 = river.controlPoints[i];
            const auto& p1 = river.controlPoints[i + 1];
            float w0 = river.widths[i];
            float w1 = river.widths[i + 1];

            auto [x0, y0] = worldToPreview(p0.x, p0.z);
            auto [x1, y1] = worldToPreview(p1.x, p1.z);

            // Draw line from p0 to p1 with varying width
            float dx = static_cast<float>(x1 - x0);
            float dy = static_cast<float>(y1 - y0);
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.5f) continue;

            int steps = static_cast<int>(len * 2) + 1;
            for (int s = 0; s <= steps; s++) {
                float t = static_cast<float>(s) / steps;
                int px = static_cast<int>(x0 + dx * t);
                int py = static_cast<int>(y0 + dy * t);
                float width = w0 + (w1 - w0) * t;
                int radius = std::max(1, static_cast<int>(width * worldToPixelScale * 0.5f));

                for (int ry = -radius; ry <= radius; ry++) {
                    for (int rx = -radius; rx <= radius; rx++) {
                        if (rx * rx + ry * ry <= radius * radius) {
                            int nx = px + rx;
                            int ny = py + ry;
                            if (nx >= 0 && nx < static_cast<int>(previewSize) &&
                                ny >= 0 && ny < static_cast<int>(previewSize)) {
                                size_t idx = (ny * previewSize + nx) * 3;
                                pixels[idx + 0] = 220;
                                pixels[idx + 1] = 50;
                                pixels[idx + 2] = 50;
                            }
                        }
                    }
                }
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

void ErosionSimulator::simulateDroplet(const ErosionConfig& config, uint32_t startX, uint32_t startY) {
    float posX = static_cast<float>(startX);
    float posY = static_cast<float>(startY);
    float dirX = 0.0f;
    float dirY = 0.0f;
    float speed = 1.0f;
    float water = 1.0f;

    // Scale factor to map source coords to flow map coords
    float flowScaleX = static_cast<float>(flowWidth) / static_cast<float>(sourceWidth);
    float flowScaleY = static_cast<float>(flowHeight) / static_cast<float>(sourceHeight_);

    for (uint32_t step = 0; step < config.maxDropletLifetime; step++) {
        // Get gradient at current position
        glm::vec2 grad = getGradientAt(posX, posY);

        // Update direction with inertia
        dirX = dirX * config.inertia - grad.x * (1.0f - config.inertia);
        dirY = dirY * config.inertia - grad.y * (1.0f - config.inertia);

        // Normalize direction
        float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len > 0.0001f) {
            dirX /= len;
            dirY /= len;
        } else {
            // Random direction if flat
            float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
            dirX = std::cos(angle);
            dirY = std::sin(angle);
        }

        // Move droplet
        float newPosX = posX + dirX * speed;
        float newPosY = posY + dirY * speed;

        // Check bounds
        if (newPosX < 0 || newPosX >= sourceWidth - 1 ||
            newPosY < 0 || newPosY >= sourceHeight_ - 1) {
            break;
        }

        // Record flow at current position (in flow map resolution)
        uint32_t flowX = static_cast<uint32_t>(posX * flowScaleX);
        uint32_t flowY = static_cast<uint32_t>(posY * flowScaleY);
        flowX = std::min(flowX, flowWidth - 1);
        flowY = std::min(flowY, flowHeight - 1);
        flowAccum[flowY * flowWidth + flowX] += water;

        // Update speed based on height difference
        float heightOld = getHeightAt(posX, posY);
        float heightNew = getHeightAt(newPosX, newPosY);
        float deltaH = heightOld - heightNew;

        speed = std::sqrt(std::max(0.01f, speed * speed + deltaH * config.gravity));
        speed = std::min(speed, 10.0f);  // Cap speed

        // Evaporate water
        water *= (1.0f - config.evaporationRate);
        if (water < config.minWater) {
            break;
        }

        posX = newPosX;
        posY = newPosY;
    }
}

void ErosionSimulator::simulateDroplets(const ErosionConfig& config, ErosionProgressCallback progressCallback) {
    // Initialize flow accumulation map
    flowWidth = config.outputResolution;
    flowHeight = config.outputResolution;
    flowAccum.resize(flowWidth * flowHeight, 0.0f);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> distX(0, sourceWidth - 1);
    std::uniform_int_distribution<uint32_t> distY(0, sourceHeight_ - 1);

    uint32_t reportInterval = config.numDroplets / 100;
    if (reportInterval == 0) reportInterval = 1;

    for (uint32_t i = 0; i < config.numDroplets; i++) {
        uint32_t startX = distX(gen);
        uint32_t startY = distY(gen);

        simulateDroplet(config, startX, startY);

        if (progressCallback && (i % reportInterval == 0)) {
            float progress = 0.1f + (static_cast<float>(i) / config.numDroplets) * 0.5f;
            std::ostringstream oss;
            oss << "Simulating droplets: " << (i * 100 / config.numDroplets) << "%";
            progressCallback(progress, oss.str());
        }
    }

    // Normalize flow accumulation
    float maxFlow = 0.0f;
    for (float f : flowAccum) {
        maxFlow = std::max(maxFlow, f);
    }

    waterData.maxFlowValue = maxFlow;
    SDL_Log("Erosion: max flow value = %.2f", maxFlow);

    if (maxFlow > 0.0f) {
        for (float& f : flowAccum) {
            f /= maxFlow;
        }
    }

    waterData.numDropletsSimulated = config.numDroplets;
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

    // Save flow map
    {
        std::ofstream file(getFlowMapPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&flowWidth), sizeof(flowWidth));
        file.write(reinterpret_cast<const char*>(&flowHeight), sizeof(flowHeight));
        file.write(reinterpret_cast<const char*>(flowAccum.data()),
                   flowAccum.size() * sizeof(float));
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
    // Load flow map
    {
        std::ifstream file(getFlowMapPath(config.cacheDirectory), std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&flowWidth), sizeof(flowWidth));
        file.read(reinterpret_cast<char*>(&flowHeight), sizeof(flowHeight));

        flowAccum.resize(flowWidth * flowHeight);
        file.read(reinterpret_cast<char*>(flowAccum.data()),
                  flowAccum.size() * sizeof(float));

        waterData.flowAccumulation = flowAccum;
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
