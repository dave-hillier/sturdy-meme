#include "RoadPathfinder.h"
#include <SDL3/SDL_log.h>
#include <lodepng.h>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace RoadGen {

// Hash function for grid positions
struct GridPosHash {
    size_t operator()(const glm::ivec2& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 16);
    }
};

// TerrainData implementations
float TerrainData::sampleHeight(float x, float z, float terrainSize) const {
    if (heights.empty() || width == 0 || height == 0) return 0.0f;

    // Convert world coords to texture coords
    float u = x / terrainSize;
    float v = z / terrainSize;

    // Clamp to valid range
    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);

    // Bilinear sample
    float fx = u * (width - 1);
    float fy = v * (height - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(width - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(height - 1));

    float fracX = fx - x0;
    float fracY = fy - y0;

    float h00 = heights[y0 * width + x0];
    float h10 = heights[y0 * width + x1];
    float h01 = heights[y1 * width + x0];
    float h11 = heights[y1 * width + x1];

    float h0 = glm::mix(h00, h10, fracX);
    float h1 = glm::mix(h01, h11, fracX);

    return glm::mix(h0, h1, fracY);
}

float TerrainData::sampleSlope(float x, float z, float terrainSize) const {
    float cellSize = terrainSize / width;
    float hL = sampleHeight(x - cellSize, z, terrainSize);
    float hR = sampleHeight(x + cellSize, z, terrainSize);
    float hU = sampleHeight(x, z - cellSize, terrainSize);
    float hD = sampleHeight(x, z + cellSize, terrainSize);

    float dzdx = (hR - hL) / (2.0f * cellSize);
    float dzdy = (hD - hU) / (2.0f * cellSize);

    return std::sqrt(dzdx * dzdx + dzdy * dzdy);
}

BiomeZone TerrainData::sampleBiome(float x, float z, float terrainSize) const {
    if (biomeZones.empty() || width == 0 || height == 0) return BiomeZone::Grassland;

    float u = glm::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = glm::clamp(z / terrainSize, 0.0f, 1.0f);

    int px = static_cast<int>(u * (width - 1));
    int py = static_cast<int>(v * (height - 1));

    px = glm::clamp(px, 0, static_cast<int>(width - 1));
    py = glm::clamp(py, 0, static_cast<int>(height - 1));

    return static_cast<BiomeZone>(biomeZones[py * width + px]);
}

bool TerrainData::isWater(float x, float z, float terrainSize) const {
    BiomeZone zone = sampleBiome(x, z, terrainSize);
    return zone == BiomeZone::Sea || zone == BiomeZone::River;
}

// RoadPathfinder implementations
RoadPathfinder::RoadPathfinder() = default;

void RoadPathfinder::init(const PathfinderConfig& cfg) {
    config = cfg;
    gridSize = cfg.gridResolution;
}

bool RoadPathfinder::loadHeightmap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;

    unsigned error = lodepng::decode(image, w, h, path, LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    terrain.width = w;
    terrain.height = h;
    terrain.heights.resize(w * h);

    // Convert 16-bit grayscale to normalized float
    for (size_t i = 0; i < w * h; i++) {
        uint16_t val = (static_cast<uint16_t>(image[i * 2]) << 8) | image[i * 2 + 1];
        terrain.heights[i] = static_cast<float>(val) / 65535.0f;
    }

    SDL_Log("Loaded heightmap: %s (%u x %u)", path.c_str(), w, h);
    return true;
}

bool RoadPathfinder::loadBiomeMap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;

    unsigned error = lodepng::decode(image, w, h, path, LCT_RGBA);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load biome map %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    // Use heightmap dimensions if available, otherwise use biome map dimensions
    if (terrain.width == 0) {
        terrain.width = w;
        terrain.height = h;
    }

    terrain.biomeZones.resize(w * h);

    // Extract zone from red channel (RGBA8)
    for (size_t i = 0; i < w * h; i++) {
        terrain.biomeZones[i] = image[i * 4]; // R channel = zone
    }

    SDL_Log("Loaded biome map: %s (%u x %u)", path.c_str(), w, h);
    return true;
}

glm::ivec2 RoadPathfinder::worldToGrid(glm::vec2 worldPos) const {
    float u = worldPos.x / config.terrainSize;
    float v = worldPos.y / config.terrainSize;
    return glm::ivec2(
        static_cast<int>(u * (gridSize - 1)),
        static_cast<int>(v * (gridSize - 1))
    );
}

glm::vec2 RoadPathfinder::gridToWorld(glm::ivec2 gridPos) const {
    float u = static_cast<float>(gridPos.x) / (gridSize - 1);
    float v = static_cast<float>(gridPos.y) / (gridSize - 1);
    return glm::vec2(u * config.terrainSize, v * config.terrainSize);
}

bool RoadPathfinder::isValidGridPos(glm::ivec2 pos) const {
    return pos.x >= 0 && pos.x < static_cast<int>(gridSize) &&
           pos.y >= 0 && pos.y < static_cast<int>(gridSize);
}

std::vector<glm::ivec2> RoadPathfinder::getNeighbors(glm::ivec2 pos) const {
    std::vector<glm::ivec2> neighbors;
    neighbors.reserve(8);

    // 8-directional movement
    static const glm::ivec2 offsets[] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0},          {1,  0},
        {-1,  1}, {0,  1}, {1,  1}
    };

    for (const auto& offset : offsets) {
        glm::ivec2 neighbor = pos + offset;
        if (isValidGridPos(neighbor)) {
            neighbors.push_back(neighbor);
        }
    }

    return neighbors;
}

float RoadPathfinder::calculateCost(glm::ivec2 from, glm::ivec2 to) const {
    glm::vec2 worldFrom = gridToWorld(from);
    glm::vec2 worldTo = gridToWorld(to);

    // Base cost is Euclidean distance
    float distance = glm::length(worldTo - worldFrom);

    // Get terrain properties at destination
    float slope = terrain.sampleSlope(worldTo.x, worldTo.y, config.terrainSize);
    bool isWater = terrain.isWater(worldTo.x, worldTo.y, config.terrainSize);

    // Apply cost modifiers
    float cost = distance;

    // Slope penalty
    cost *= (1.0f + slope * config.slopeCostMultiplier);

    // Water penalty (avoid crossing water if possible)
    if (isWater) {
        cost += config.waterPenalty;
    }

    // Cliff penalty (steep slopes)
    if (slope > config.cliffSlopeThreshold) {
        cost += config.cliffPenalty;
    }

    return cost;
}

float RoadPathfinder::heuristic(glm::ivec2 from, glm::ivec2 to) const {
    // Euclidean distance heuristic (in world coordinates for consistency)
    glm::vec2 worldFrom = gridToWorld(from);
    glm::vec2 worldTo = gridToWorld(to);
    return glm::length(worldTo - worldFrom);
}

bool RoadPathfinder::findPath(glm::vec2 start, glm::vec2 end, std::vector<RoadControlPoint>& outPath) {
    outPath.clear();

    glm::ivec2 startGrid = worldToGrid(start);
    glm::ivec2 endGrid = worldToGrid(end);

    // Clamp to valid grid positions
    startGrid = glm::clamp(startGrid, glm::ivec2(0), glm::ivec2(gridSize - 1));
    endGrid = glm::clamp(endGrid, glm::ivec2(0), glm::ivec2(gridSize - 1));

    // If start == end, return single point
    if (startGrid == endGrid) {
        outPath.push_back(RoadControlPoint(start));
        outPath.push_back(RoadControlPoint(end));
        return true;
    }

    // A* algorithm
    auto cmp = [](const PathNode& a, const PathNode& b) {
        return a.fCost() > b.fCost();
    };
    std::priority_queue<PathNode, std::vector<PathNode>, decltype(cmp)> openSet(cmp);

    std::unordered_map<glm::ivec2, PathNode, GridPosHash> allNodes;
    std::unordered_set<glm::ivec2, GridPosHash> closedSet;

    // Initialize start node
    PathNode startNode;
    startNode.x = startGrid.x;
    startNode.y = startGrid.y;
    startNode.gCost = 0.0f;
    startNode.hCost = heuristic(startGrid, endGrid);
    startNode.parentX = -1;
    startNode.parentY = -1;

    openSet.push(startNode);
    allNodes[startGrid] = startNode;

    int iterations = 0;
    const int maxIterations = gridSize * gridSize; // Safety limit

    while (!openSet.empty() && iterations < maxIterations) {
        iterations++;

        PathNode current = openSet.top();
        openSet.pop();

        glm::ivec2 currentPos(current.x, current.y);

        // Check if we've reached the goal
        if (currentPos == endGrid) {
            // Reconstruct path
            std::vector<glm::vec2> gridPath;
            glm::ivec2 pos = currentPos;

            while (pos.x >= 0 && pos.y >= 0) {
                gridPath.push_back(gridToWorld(pos));
                const PathNode& node = allNodes[pos];
                pos = glm::ivec2(node.parentX, node.parentY);
            }

            // Reverse to get start-to-end order
            std::reverse(gridPath.begin(), gridPath.end());

            // Ensure we include exact start and end points
            if (!gridPath.empty()) {
                gridPath[0] = start;
                gridPath.back() = end;
            }

            // Convert to control points
            for (const auto& p : gridPath) {
                outPath.push_back(RoadControlPoint(p));
            }

            // Simplify the path
            simplifyPath(outPath);

            return true;
        }

        // Skip if already processed
        if (closedSet.count(currentPos)) continue;
        closedSet.insert(currentPos);

        // Process neighbors
        for (const glm::ivec2& neighborPos : getNeighbors(currentPos)) {
            if (closedSet.count(neighborPos)) continue;

            float tentativeG = current.gCost + calculateCost(currentPos, neighborPos);

            auto it = allNodes.find(neighborPos);
            if (it == allNodes.end() || tentativeG < it->second.gCost) {
                PathNode neighbor;
                neighbor.x = neighborPos.x;
                neighbor.y = neighborPos.y;
                neighbor.gCost = tentativeG;
                neighbor.hCost = heuristic(neighborPos, endGrid);
                neighbor.parentX = current.x;
                neighbor.parentY = current.y;

                allNodes[neighborPos] = neighbor;
                openSet.push(neighbor);
            }
        }
    }

    // No path found - create direct line as fallback
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "No path found from (%.1f, %.1f) to (%.1f, %.1f), using direct line",
                start.x, start.y, end.x, end.y);

    outPath.push_back(RoadControlPoint(start));
    outPath.push_back(RoadControlPoint(end));
    return false;
}

void RoadPathfinder::simplifyPath(std::vector<RoadControlPoint>& path) const {
    if (path.size() <= 2) return;

    std::vector<glm::vec2> points;
    points.reserve(path.size());
    for (const auto& cp : path) {
        points.push_back(cp.position);
    }

    std::vector<glm::vec2> simplified;
    simplified.push_back(points.front());
    douglasPeucker(points, config.simplifyEpsilon, simplified, 0, points.size() - 1);
    simplified.push_back(points.back());

    // Rebuild path with simplified points
    path.clear();
    for (const auto& p : simplified) {
        path.push_back(RoadControlPoint(p));
    }
}

void RoadPathfinder::douglasPeucker(const std::vector<glm::vec2>& points, float epsilon,
                                     std::vector<glm::vec2>& outPoints,
                                     size_t startIdx, size_t endIdx) const {
    if (endIdx <= startIdx + 1) return;

    // Find the point with maximum distance from the line
    glm::vec2 lineStart = points[startIdx];
    glm::vec2 lineEnd = points[endIdx];
    glm::vec2 lineDir = lineEnd - lineStart;
    float lineLength = glm::length(lineDir);

    if (lineLength < 0.0001f) return;

    lineDir /= lineLength;

    float maxDist = 0.0f;
    size_t maxIdx = startIdx;

    for (size_t i = startIdx + 1; i < endIdx; i++) {
        glm::vec2 toPoint = points[i] - lineStart;
        float projLength = glm::dot(toPoint, lineDir);
        glm::vec2 projPoint = lineStart + lineDir * projLength;
        float dist = glm::length(points[i] - projPoint);

        if (dist > maxDist) {
            maxDist = dist;
            maxIdx = i;
        }
    }

    // If max distance exceeds epsilon, recursively simplify
    if (maxDist > epsilon) {
        douglasPeucker(points, epsilon, outPoints, startIdx, maxIdx);
        outPoints.push_back(points[maxIdx]);
        douglasPeucker(points, epsilon, outPoints, maxIdx, endIdx);
    }
}

std::vector<RoadPathfinder::ConnectionCandidate>
RoadPathfinder::determineConnections(const std::vector<Settlement>& settlements) const {
    std::vector<ConnectionCandidate> connections;

    // Maximum connection distances based on settlement importance
    const float maxDistTownToTown = 8000.0f;
    const float maxDistTownToVillage = 5000.0f;
    const float maxDistVillageToVillage = 3000.0f;
    const float maxDistToHamlet = 2000.0f;

    // Track which pairs we've considered (to avoid duplicates)
    std::unordered_set<uint64_t> consideredPairs;

    auto makePairKey = [](size_t a, size_t b) -> uint64_t {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    };

    for (size_t i = 0; i < settlements.size(); i++) {
        const Settlement& from = settlements[i];

        for (size_t j = i + 1; j < settlements.size(); j++) {
            const Settlement& to = settlements[j];

            uint64_t pairKey = makePairKey(i, j);
            if (consideredPairs.count(pairKey)) continue;
            consideredPairs.insert(pairKey);

            float distance = glm::length(to.position - from.position);

            // Determine max distance based on settlement types
            float maxDist = maxDistToHamlet;
            if ((from.type == SettlementType::Town && to.type == SettlementType::Town)) {
                maxDist = maxDistTownToTown;
            } else if ((from.type == SettlementType::Town || to.type == SettlementType::Town) &&
                       (from.type == SettlementType::Village || to.type == SettlementType::Village)) {
                maxDist = maxDistTownToVillage;
            } else if (from.type == SettlementType::Village && to.type == SettlementType::Village) {
                maxDist = maxDistVillageToVillage;
            }

            if (distance <= maxDist) {
                ConnectionCandidate candidate;
                candidate.fromIdx = i;
                candidate.toIdx = j;
                candidate.distance = distance;
                candidate.roadType = determineRoadType(from.type, to.type);
                connections.push_back(candidate);
            }
        }
    }

    // Sort by importance (main roads first, then by distance)
    std::sort(connections.begin(), connections.end(),
              [](const ConnectionCandidate& a, const ConnectionCandidate& b) {
        // Higher road types are more important
        if (a.roadType != b.roadType) {
            return static_cast<int>(a.roadType) > static_cast<int>(b.roadType);
        }
        // Shorter distances first for same road type
        return a.distance < b.distance;
    });

    return connections;
}

bool RoadPathfinder::generateRoadNetwork(const std::vector<Settlement>& settlements,
                                          RoadNetwork& outNetwork,
                                          ProgressCallback callback) {
    outNetwork.roads.clear();
    outNetwork.terrainSize = config.terrainSize;

    if (settlements.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No settlements provided for road generation");
        return true;
    }

    if (callback) callback(0.0f, "Determining road connections...");

    // Determine which settlements to connect
    auto connections = determineConnections(settlements);

    SDL_Log("Found %zu potential road connections for %zu settlements",
            connections.size(), settlements.size());

    if (connections.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No road connections determined");
        return true;
    }

    // Generate paths for each connection
    size_t completed = 0;
    size_t failed = 0;

    for (size_t i = 0; i < connections.size(); i++) {
        const auto& conn = connections[i];
        const Settlement& from = settlements[conn.fromIdx];
        const Settlement& to = settlements[conn.toIdx];

        if (callback) {
            float progress = static_cast<float>(i + 1) / connections.size();
            std::string status = "Generating road " + std::to_string(i + 1) + "/" +
                                 std::to_string(connections.size());
            callback(progress, status);
        }

        RoadSpline road;
        road.type = conn.roadType;
        road.fromSettlementId = from.id;
        road.toSettlementId = to.id;

        // Calculate road start/end points at settlement edges
        glm::vec2 diff = to.position - from.position;
        float dist = glm::length(diff);

        glm::vec2 startPos, endPos;
        if (dist > 0.001f) {
            glm::vec2 direction = diff / dist;  // Normalize safely
            startPos = from.position + direction * from.radius;
            endPos = to.position - direction * to.radius;
        } else {
            // Settlements at same position - use center points
            startPos = from.position;
            endPos = to.position;
        }

        if (findPath(startPos, endPos, road.controlPoints)) {
            outNetwork.roads.push_back(std::move(road));
            completed++;
        } else {
            // Still add the road even if pathfinding failed (uses direct line)
            outNetwork.roads.push_back(std::move(road));
            failed++;
        }
    }

    SDL_Log("Road generation complete: %zu roads (%zu with pathfinding, %zu direct)",
            outNetwork.roads.size(), completed - failed, failed);

    // Log statistics by road type
    SDL_Log("Road breakdown:");
    SDL_Log("  Main Roads: %zu", outNetwork.countByType(RoadType::MainRoad));
    SDL_Log("  Roads: %zu", outNetwork.countByType(RoadType::Road));
    SDL_Log("  Lanes: %zu", outNetwork.countByType(RoadType::Lane));
    SDL_Log("  Bridleways: %zu", outNetwork.countByType(RoadType::Bridleway));
    SDL_Log("  Footpaths: %zu", outNetwork.countByType(RoadType::Footpath));
    SDL_Log("Total road length: %.1f km", outNetwork.getTotalLength() / 1000.0f);

    return true;
}

} // namespace RoadGen
