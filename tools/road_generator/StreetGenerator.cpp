#include "StreetGenerator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>

namespace RoadGen {

// ============================================================================
// StreetNetwork helper methods
// ============================================================================

uint32_t StreetNetwork::addNode(glm::vec2 position) {
    uint32_t id = static_cast<uint32_t>(nodes.size());
    StreetNode node;
    node.id = id;
    node.position = position;
    node.parentId = UINT32_MAX;
    node.depth = 0;
    nodes.push_back(node);
    return id;
}

uint32_t StreetNetwork::addSegment(uint32_t fromNode, uint32_t toNode, StreetType type, bool isInfill) {
    uint32_t id = static_cast<uint32_t>(segments.size());
    StreetSegment seg;
    seg.id = id;
    seg.fromNode = fromNode;
    seg.toNode = toNode;
    seg.length = glm::distance(nodes[fromNode].position, nodes[toNode].position);
    seg.type = type;
    seg.isInfill = isInfill;
    segments.push_back(seg);
    return id;
}

StreetNode* StreetNetwork::findNode(uint32_t id) {
    if (id < nodes.size() && !nodes[id].deleted) {
        return &nodes[id];
    }
    return nullptr;
}

StreetSegment* StreetNetwork::findSegment(uint32_t fromNode, uint32_t toNode) {
    for (auto& seg : segments) {
        if (seg.deleted) continue;
        if ((seg.fromNode == fromNode && seg.toNode == toNode) ||
            (seg.fromNode == toNode && seg.toNode == fromNode)) {
            return &seg;
        }
    }
    return nullptr;
}

void StreetNetwork::redirectConnections(uint32_t oldNodeId, uint32_t newNodeId) {
    for (auto& seg : segments) {
        if (seg.deleted) continue;
        if (seg.fromNode == oldNodeId) seg.fromNode = newNodeId;
        if (seg.toNode == oldNodeId) seg.toNode = newNodeId;
    }
}

float StreetNetwork::getTotalStreetLength() const {
    float total = 0.0f;
    for (const auto& seg : segments) {
        if (!seg.deleted) total += seg.length;
    }
    return total;
}

size_t StreetNetwork::countByType(StreetType type) const {
    size_t count = 0;
    for (const auto& seg : segments) {
        if (!seg.deleted && seg.type == type) count++;
    }
    return count;
}

// ============================================================================
// StreetGenerator implementation
// ============================================================================

void StreetGenerator::init(const TerrainData& terrainData, float size) {
    terrain = terrainData;
    terrainSize = size;
}

bool StreetGenerator::generate(
    glm::vec2 center,
    float radius,
    SettlementType settlementType,
    const RoadNetwork& externalRoads,
    uint32_t settlementId,
    const StreetGenConfig& config,
    StreetNetwork& outNetwork,
    ProgressCallback callback
) {
    rng.seed(config.seed);

    outNetwork = StreetNetwork();
    outNetwork.center = center;
    outNetwork.radius = radius;
    outNetwork.terrainSize = terrainSize;

    // Phase 1: Find entry points
    if (callback) callback(0.0f, "Finding entry points...");
    auto entries = findEntryPoints(center, radius, externalRoads, settlementId);
    outNetwork.entries = entries;

    if (entries.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "No entry points found for settlement at (%.1f, %.1f)", center.x, center.y);
        // Create a default entry from the north
        SettlementEntry defaultEntry;
        defaultEntry.position = center + glm::vec2(0.0f, -radius);
        defaultEntry.direction = glm::vec2(0.0f, 1.0f);
        defaultEntry.roadType = RoadType::Lane;
        defaultEntry.roadId = UINT32_MAX;
        entries.push_back(defaultEntry);
        outNetwork.entries = entries;
    }

    SDL_Log("Phase 1: Found %zu entry points", entries.size());

    // Phase 2: Place key buildings
    if (callback) callback(0.1f, "Placing key buildings...");
    auto keyBuildings = placeKeyBuildings(settlementType, center, radius, entries);
    outNetwork.keyBuildings = keyBuildings;

    SDL_Log("Phase 2: Placed %zu key buildings", keyBuildings.size());

    // Phase 3: Generate organic skeleton
    if (callback) callback(0.2f, "Generating street skeleton...");
    if (!generateSkeleton(entries, keyBuildings, center, radius, config.skeleton, outNetwork)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate street skeleton");
        return false;
    }

    SDL_Log("Phase 3: Generated skeleton with %zu nodes, %zu segments",
            outNetwork.nodes.size(), outNetwork.segments.size());

    // Phase 4: Identify blocks
    if (callback) callback(0.4f, "Identifying blocks...");
    auto blocks = identifyBlocks(outNetwork, center, radius);

    SDL_Log("Phase 4: Identified %zu blocks", blocks.size());

    // Phase 5: Subdivide oversized blocks
    if (callback) callback(0.5f, "Subdividing blocks...");
    subdivideBlocks(blocks, outNetwork, config.infill);

    SDL_Log("Phase 5: After subdivision: %zu blocks, %zu segments",
            blocks.size(), outNetwork.segments.size());

    // Phase 6: Assign hierarchy
    if (callback) callback(0.7f, "Assigning street hierarchy...");
    assignHierarchy(outNetwork, entries, keyBuildings);

    SDL_Log("Phase 6: Hierarchy assigned - Main: %zu, Street: %zu, Lane: %zu, Alley: %zu",
            outNetwork.countByType(StreetType::MainStreet),
            outNetwork.countByType(StreetType::Street),
            outNetwork.countByType(StreetType::Lane),
            outNetwork.countByType(StreetType::Alley));

    // Phase 7: Subdivide into lots
    if (callback) callback(0.8f, "Subdividing lots...");
    outNetwork.lots = subdivideLots(blocks, outNetwork, config.lot);
    outNetwork.blocks = blocks;

    SDL_Log("Phase 7: Created %zu lots", outNetwork.lots.size());

    if (callback) callback(1.0f, "Street generation complete");

    SDL_Log("Street generation complete: %.1f m total length",
            outNetwork.getTotalStreetLength());

    return true;
}

// ============================================================================
// Phase 1: Entry Point Detection
// ============================================================================

std::vector<SettlementEntry> StreetGenerator::findEntryPoints(
    glm::vec2 center,
    float radius,
    const RoadNetwork& externalRoads,
    uint32_t settlementId
) {
    std::vector<SettlementEntry> entries;

    for (const auto& road : externalRoads.roads) {
        bool connectsHere = (road.fromSettlementId == settlementId) ||
                           (road.toSettlementId == settlementId);

        if (!connectsHere) continue;
        if (road.controlPoints.size() < 2) continue;

        // Find which end connects to this settlement
        glm::vec2 roadStart = road.controlPoints.front().position;
        glm::vec2 roadEnd = road.controlPoints.back().position;

        glm::vec2 nearPoint, farPoint;
        if (glm::distance(roadStart, center) < glm::distance(roadEnd, center)) {
            nearPoint = roadStart;
            farPoint = roadEnd;
        } else {
            nearPoint = roadEnd;
            farPoint = roadStart;
        }

        // Find intersection with settlement boundary circle
        glm::vec2 toCenter = center - farPoint;
        float distToCenter = glm::length(toCenter);

        if (distToCenter < 0.001f) continue;

        glm::vec2 dir = toCenter / distToCenter;

        // Entry point is on the circle boundary
        glm::vec2 entryPos = center - dir * radius;

        // Inward direction
        glm::vec2 inward = dir;

        SettlementEntry entry;
        entry.position = entryPos;
        entry.direction = inward;
        entry.roadType = road.type;
        entry.roadId = static_cast<uint32_t>(&road - externalRoads.roads.data());

        entries.push_back(entry);
    }

    // Sort by road importance
    std::sort(entries.begin(), entries.end(),
        [](const SettlementEntry& a, const SettlementEntry& b) {
            return static_cast<int>(a.roadType) > static_cast<int>(b.roadType);
        });

    return entries;
}

// ============================================================================
// Phase 2: Key Building Placement
// ============================================================================

std::vector<KeyBuilding> StreetGenerator::placeKeyBuildings(
    SettlementType settlementType,
    glm::vec2 center,
    float radius,
    const std::vector<SettlementEntry>& entries
) {
    std::vector<KeyBuilding> buildings;

    // Church: near center, prefer high ground
    glm::vec2 churchPos = findHighPoint(center, radius * 0.3f);
    buildings.push_back({
        KeyBuilding::Type::Church,
        churchPos,
        15.0f,
        1.0f
    });

    // Market: on axis from primary entry toward center (towns and villages)
    if (settlementType == SettlementType::Town ||
        settlementType == SettlementType::Village) {

        glm::vec2 marketPos = entries[0].position + entries[0].direction * (radius * 0.4f);
        marketPos = avoidCollision(marketPos, buildings, 25.0f);

        buildings.push_back({
            KeyBuilding::Type::Market,
            marketPos,
            20.0f,
            0.9f
        });
    }

    // Inn: near primary entry (not hamlets)
    if (settlementType != SettlementType::Hamlet) {
        glm::vec2 innPos = entries[0].position + entries[0].direction * 30.0f;
        innPos = avoidCollision(innPos, buildings, 15.0f);

        buildings.push_back({
            KeyBuilding::Type::Inn,
            innPos,
            10.0f,
            0.7f
        });
    }

    // Well: central location
    glm::vec2 wellPos = center + glm::vec2(
        std::uniform_real_distribution<float>(-20.0f, 20.0f)(rng),
        std::uniform_real_distribution<float>(-20.0f, 20.0f)(rng)
    );
    wellPos = avoidCollision(wellPos, buildings, 10.0f);

    buildings.push_back({
        KeyBuilding::Type::Well,
        wellPos,
        5.0f,
        0.5f
    });

    // Village green for villages
    if (settlementType == SettlementType::Village) {
        glm::vec2 greenPos = churchPos + glm::vec2(25.0f, 0.0f);
        greenPos = avoidCollision(greenPos, buildings, 20.0f);

        buildings.push_back({
            KeyBuilding::Type::Green,
            greenPos,
            25.0f,
            0.5f
        });
    }

    return buildings;
}

// ============================================================================
// Phase 3: Organic Skeleton Generation
// ============================================================================

bool StreetGenerator::generateSkeleton(
    const std::vector<SettlementEntry>& entries,
    const std::vector<KeyBuilding>& keyBuildings,
    glm::vec2 center,
    float radius,
    const SkeletonConfig& config,
    StreetNetwork& network
) {
    // Create attractors from key buildings
    struct Attractor {
        glm::vec2 position;
        float weight;
        bool reached;
    };

    std::vector<Attractor> attractors;
    for (const auto& kb : keyBuildings) {
        attractors.push_back({kb.position, kb.attractorWeight, false});
    }

    // Add boundary attractors for coverage
    int boundaryCount = 8;
    for (int i = 0; i < boundaryCount; i++) {
        float angle = static_cast<float>(i) * 2.0f * 3.14159f / boundaryCount;
        glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * (radius * 0.7f);
        attractors.push_back({pos, 0.3f, false});
    }

    // Seed from primary entry
    uint32_t rootId = network.addNode(entries[0].position);
    network.nodes[rootId].depth = 0;

    SDL_Log("Skeleton: Entry at (%.1f, %.1f), direction (%.2f, %.2f), center (%.1f, %.1f), radius %.1f",
            entries[0].position.x, entries[0].position.y,
            entries[0].direction.x, entries[0].direction.y,
            center.x, center.y, radius);
    SDL_Log("Skeleton: %zu attractors, attractionRadius=%.1f, killRadius=%.1f, segmentLength=%.1f",
            attractors.size(), config.attractionRadius, config.killRadius, config.segmentLength);

    // Track active growth nodes
    struct GrowthNode {
        uint32_t nodeId;
        glm::vec2 direction;
        bool active;
    };

    std::vector<GrowthNode> growthNodes;
    growthNodes.push_back({rootId, entries[0].direction, true});

    // Main colonization loop
    for (int iter = 0; iter < config.maxIterations; iter++) {
        // Check if all attractors reached
        bool allReached = true;
        for (const auto& attr : attractors) {
            if (!attr.reached) { allReached = false; break; }
        }
        if (allReached) break;

        // Find active growth nodes
        std::vector<size_t> activeIndices;
        for (size_t i = 0; i < growthNodes.size(); i++) {
            if (growthNodes[i].active) activeIndices.push_back(i);
        }

        if (activeIndices.empty()) break;

        // For each active node, compute growth
        std::vector<std::tuple<size_t, glm::vec2, glm::vec2>> candidates;

        for (size_t idx : activeIndices) {
            const auto& gNode = growthNodes[idx];
            const auto& node = network.nodes[gNode.nodeId];

            // Find influencing attractors
            glm::vec2 growthDir(0.0f);
            int influenceCount = 0;

            for (const auto& attr : attractors) {
                if (attr.reached) continue;

                float dist = glm::distance(node.position, attr.position);
                if (dist < config.attractionRadius && dist > 0.001f) {
                    float weight = attr.weight / dist;
                    glm::vec2 toAttr = glm::normalize(attr.position - node.position);
                    growthDir += toAttr * weight;
                    influenceCount++;
                }
            }

            if (influenceCount == 0) {
                // No attractors in range - continue in current direction
                growthDir = gNode.direction;
            } else {
                growthDir = glm::normalize(growthDir);
            }

            // Clamp growth direction to max branch angle (but allow straight-ahead)
            if (node.parentId != UINT32_MAX) {
                const auto& parent = network.nodes[node.parentId];
                glm::vec2 parentDir = glm::normalize(node.position - parent.position);
                float angle = angleBetween(parentDir, growthDir) * 180.0f / 3.14159f;

                // Only clamp if angle exceeds max - allow straight ahead and gentle curves
                if (angle > config.maxBranchAngle) {
                    float maxRad = config.maxBranchAngle * 3.14159f / 180.0f;
                    float currentAngle = std::atan2(growthDir.y, growthDir.x);
                    float parentAngle = std::atan2(parentDir.y, parentDir.x);
                    float angleDiff = currentAngle - parentAngle;

                    // Normalize angle diff to [-pi, pi]
                    while (angleDiff > 3.14159f) angleDiff -= 2.0f * 3.14159f;
                    while (angleDiff < -3.14159f) angleDiff += 2.0f * 3.14159f;

                    if (angleDiff > maxRad) {
                        float newAngle = parentAngle + maxRad;
                        growthDir = glm::vec2(std::cos(newAngle), std::sin(newAngle));
                    } else if (angleDiff < -maxRad) {
                        float newAngle = parentAngle - maxRad;
                        growthDir = glm::vec2(std::cos(newAngle), std::sin(newAngle));
                    }
                }
            }

            // Compute new position
            glm::vec2 newPos = node.position + growthDir * config.segmentLength;

            // Check within settlement
            float distFromCenter = glm::distance(newPos, center);
            if (distFromCenter > radius) {
                SDL_Log("  Skeleton iter %d: node %u rejected - outside settlement (%.1f > %.1f)",
                        iter, gNode.nodeId, distFromCenter, radius);
                continue;
            }

            // Check terrain slope
            float slope = terrain.sampleSlope(newPos.x, newPos.y, terrainSize);
            if (slope > config.maxSlope) {
                SDL_Log("  Skeleton iter %d: node %u rejected - slope too steep (%.2f > %.2f)",
                        iter, gNode.nodeId, slope, config.maxSlope);
                continue;
            }

            SDL_Log("  Skeleton iter %d: candidate from node %u at (%.1f,%.1f) -> (%.1f,%.1f), dir=(%.2f,%.2f), influencers=%d",
                    iter, gNode.nodeId, node.position.x, node.position.y, newPos.x, newPos.y,
                    growthDir.x, growthDir.y, influenceCount);

            candidates.push_back({idx, newPos, growthDir});
        }

        // Limit branches per iteration
        int branchCount = 0;
        for (const auto& [idx, newPos, growthDir] : candidates) {
            if (branchCount >= config.maxBranches) break;

            // Get parent node ID before any modifications (avoid reference invalidation)
            uint32_t parentNodeId = growthNodes[idx].nodeId;
            const auto& parentNode = network.nodes[parentNodeId];

            // Create new node
            uint32_t newId = network.addNode(newPos);
            network.nodes[newId].parentId = parentNodeId;
            network.nodes[newId].depth = parentNode.depth + 1;
            network.nodes[parentNodeId].children.push_back(newId);

            // Create segment
            network.addSegment(parentNodeId, newId, StreetType::Street, false);

            // Check if we reached an attractor
            for (auto& attr : attractors) {
                if (attr.reached) continue;
                if (glm::distance(newPos, attr.position) < config.killRadius) {
                    attr.reached = true;
                    network.nodes[newId].isKeyBuilding = true;
                }
            }

            // Deactivate old growth node BEFORE push_back (to avoid iterator invalidation)
            growthNodes[idx].active = false;

            // Add new growth node
            growthNodes.push_back({newId, growthDir, true});

            branchCount++;
        }
    }

    return !network.segments.empty();
}

// ============================================================================
// Phase 4: Block Identification
// ============================================================================

std::vector<Block> StreetGenerator::identifyBlocks(
    const StreetNetwork& network,
    glm::vec2 center,
    float radius
) {
    std::vector<Block> blocks;

    // Simple approach: create blocks from the convex areas between streets
    // For now, create blocks based on the skeleton tree structure

    // Collect all street segments as edges
    std::vector<std::pair<glm::vec2, glm::vec2>> edges;
    for (const auto& seg : network.segments) {
        if (seg.deleted) continue;
        edges.push_back({
            network.nodes[seg.fromNode].position,
            network.nodes[seg.toNode].position
        });
    }

    // Add settlement boundary
    auto boundary = createCirclePolygon(center, radius, 24);
    for (size_t i = 0; i < boundary.size(); i++) {
        edges.push_back({boundary[i], boundary[(i + 1) % boundary.size()]});
    }

    // For a simple implementation, create blocks by sampling areas
    // and finding which are enclosed by streets

    // Grid-based block detection
    float gridSize = 20.0f;
    int gridCount = static_cast<int>(radius * 2.0f / gridSize);

    std::vector<glm::vec2> blockCenters;

    for (int y = 0; y < gridCount; y++) {
        for (int x = 0; x < gridCount; x++) {
            glm::vec2 pos = center + glm::vec2(
                (x - gridCount / 2) * gridSize + gridSize * 0.5f,
                (y - gridCount / 2) * gridSize + gridSize * 0.5f
            );

            // Check if within settlement
            if (glm::distance(pos, center) > radius * 0.9f) continue;

            // Check if not too close to a street
            bool nearStreet = false;
            for (const auto& seg : network.segments) {
                if (seg.deleted) continue;
                glm::vec2 a = network.nodes[seg.fromNode].position;
                glm::vec2 b = network.nodes[seg.toNode].position;

                // Point-to-line-segment distance
                glm::vec2 ab = b - a;
                float t = glm::clamp(glm::dot(pos - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
                glm::vec2 closest = a + ab * t;
                float dist = glm::distance(pos, closest);

                if (dist < gridSize * 0.5f) {
                    nearStreet = true;
                    break;
                }
            }

            if (!nearStreet) {
                blockCenters.push_back(pos);
            }
        }
    }

    // Cluster nearby block centers and create rectangular blocks
    std::vector<bool> used(blockCenters.size(), false);
    uint32_t blockId = 0;

    for (size_t i = 0; i < blockCenters.size(); i++) {
        if (used[i]) continue;

        // Find connected block cells
        std::vector<glm::vec2> cluster;
        std::queue<size_t> queue;
        queue.push(i);
        used[i] = true;

        while (!queue.empty()) {
            size_t idx = queue.front();
            queue.pop();
            cluster.push_back(blockCenters[idx]);

            for (size_t j = 0; j < blockCenters.size(); j++) {
                if (used[j]) continue;
                if (glm::distance(blockCenters[idx], blockCenters[j]) < gridSize * 1.5f) {
                    used[j] = true;
                    queue.push(j);
                }
            }
        }

        if (cluster.size() < 2) continue;

        // Create block from cluster bounding box
        glm::vec2 minP(FLT_MAX), maxP(-FLT_MAX);
        for (const auto& p : cluster) {
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        // Expand slightly
        float padding = gridSize * 0.5f;
        minP -= glm::vec2(padding);
        maxP += glm::vec2(padding);

        Block block;
        block.id = blockId++;
        block.boundary = {
            minP,
            glm::vec2(maxP.x, minP.y),
            maxP,
            glm::vec2(minP.x, maxP.y)
        };
        block.area = computePolygonArea(block.boundary);
        block.perimeter = computePolygonPerimeter(block.boundary);
        block.isExterior = (glm::distance((minP + maxP) * 0.5f, center) > radius * 0.6f);

        blocks.push_back(block);
    }

    return blocks;
}

// ============================================================================
// Phase 5: Block Subdivision
// ============================================================================

void StreetGenerator::subdivideBlocks(
    std::vector<Block>& blocks,
    StreetNetwork& network,
    const InfillConfig& config
) {
    std::vector<Block> newBlocks;

    for (auto& block : blocks) {
        // Check if block needs subdivision
        bool needsSplit = (block.perimeter > config.maxBlockPerimeter) ||
                         (block.area > config.maxBlockArea);

        if (!needsSplit) {
            newBlocks.push_back(block);
            continue;
        }

        // Find longest edge
        float maxEdgeLen = 0.0f;
        size_t maxEdgeIdx = 0;

        for (size_t i = 0; i < block.boundary.size(); i++) {
            size_t j = (i + 1) % block.boundary.size();
            float len = glm::distance(block.boundary[i], block.boundary[j]);
            if (len > maxEdgeLen) {
                maxEdgeLen = len;
                maxEdgeIdx = i;
            }
        }

        // Split perpendicular to longest edge
        glm::vec2 edgeStart = block.boundary[maxEdgeIdx];
        glm::vec2 edgeEnd = block.boundary[(maxEdgeIdx + 1) % block.boundary.size()];
        glm::vec2 edgeDir = glm::normalize(edgeEnd - edgeStart);
        glm::vec2 perpDir(-edgeDir.y, edgeDir.x);

        // Find midpoint with randomization
        float variation = std::uniform_real_distribution<float>(
            -config.blockSizeVariation, config.blockSizeVariation)(rng);
        glm::vec2 midpoint = (edgeStart + edgeEnd) * 0.5f;
        midpoint += edgeDir * (maxEdgeLen * variation * 0.5f);

        // Find split line endpoints
        glm::vec2 splitStart = midpoint;
        glm::vec2 splitEnd = midpoint + perpDir * 200.0f;  // Long enough to cross block

        // Create nodes and segment for new street
        uint32_t startNode = network.addNode(splitStart);
        uint32_t endNode = network.addNode(splitEnd);
        network.addSegment(startNode, endNode, StreetType::Lane, true);

        // Split the block into two
        auto [blockA, blockB] = splitPolygon(block.boundary, splitStart, splitEnd);

        if (blockA.size() >= 3) {
            Block newBlock;
            newBlock.id = static_cast<uint32_t>(newBlocks.size() + blocks.size());
            newBlock.boundary = blockA;
            newBlock.area = computePolygonArea(blockA);
            newBlock.perimeter = computePolygonPerimeter(blockA);
            newBlock.isExterior = block.isExterior;
            newBlocks.push_back(newBlock);
        }

        if (blockB.size() >= 3) {
            Block newBlock;
            newBlock.id = static_cast<uint32_t>(newBlocks.size() + blocks.size());
            newBlock.boundary = blockB;
            newBlock.area = computePolygonArea(blockB);
            newBlock.perimeter = computePolygonPerimeter(blockB);
            newBlock.isExterior = block.isExterior;
            newBlocks.push_back(newBlock);
        }
    }

    blocks = std::move(newBlocks);

    // Merge nearby nodes
    std::vector<std::vector<uint32_t>> clusters;
    std::vector<bool> assigned(network.nodes.size(), false);

    for (size_t i = 0; i < network.nodes.size(); i++) {
        if (assigned[i] || network.nodes[i].deleted) continue;

        std::vector<uint32_t> cluster = {static_cast<uint32_t>(i)};
        assigned[i] = true;

        for (size_t j = i + 1; j < network.nodes.size(); j++) {
            if (assigned[j] || network.nodes[j].deleted) continue;

            float dist = glm::distance(
                network.nodes[i].position,
                network.nodes[j].position
            );

            if (dist < config.intersectionMergeRadius) {
                cluster.push_back(static_cast<uint32_t>(j));
                assigned[j] = true;
            }
        }

        if (cluster.size() > 1) {
            clusters.push_back(cluster);
        }
    }

    // Merge clusters
    for (const auto& cluster : clusters) {
        glm::vec2 centroid(0.0f);
        for (uint32_t nodeId : cluster) {
            centroid += network.nodes[nodeId].position;
        }
        centroid /= static_cast<float>(cluster.size());

        uint32_t keepId = cluster[0];
        network.nodes[keepId].position = centroid;

        for (size_t i = 1; i < cluster.size(); i++) {
            network.redirectConnections(cluster[i], keepId);
            network.nodes[cluster[i]].deleted = true;
        }
    }
}

// ============================================================================
// Phase 6: Hierarchy Assignment
// ============================================================================

void StreetGenerator::assignHierarchy(
    StreetNetwork& network,
    const std::vector<SettlementEntry>& entries,
    const std::vector<KeyBuilding>& keyBuildings
) {
    if (entries.empty() || keyBuildings.empty()) return;

    // Find church position
    glm::vec2 churchPos = network.center;
    for (const auto& kb : keyBuildings) {
        if (kb.type == KeyBuilding::Type::Church) {
            churchPos = kb.position;
            break;
        }
    }

    // Find node nearest to entry
    uint32_t entryNode = 0;
    float minEntryDist = FLT_MAX;
    for (const auto& node : network.nodes) {
        if (node.deleted) continue;
        float dist = glm::distance(node.position, entries[0].position);
        if (dist < minEntryDist) {
            minEntryDist = dist;
            entryNode = node.id;
        }
    }

    // Find node nearest to church
    uint32_t churchNode = 0;
    float minChurchDist = FLT_MAX;
    for (const auto& node : network.nodes) {
        if (node.deleted) continue;
        float dist = glm::distance(node.position, churchPos);
        if (dist < minChurchDist) {
            minChurchDist = dist;
            churchNode = node.id;
        }
    }

    // BFS to find path from entry to church
    std::unordered_map<uint32_t, uint32_t> parent;
    std::queue<uint32_t> queue;
    queue.push(entryNode);
    parent[entryNode] = UINT32_MAX;

    while (!queue.empty()) {
        uint32_t current = queue.front();
        queue.pop();

        if (current == churchNode) break;

        // Find connected nodes via segments
        for (const auto& seg : network.segments) {
            if (seg.deleted) continue;

            uint32_t neighbor = UINT32_MAX;
            if (seg.fromNode == current) neighbor = seg.toNode;
            else if (seg.toNode == current) neighbor = seg.fromNode;

            if (neighbor != UINT32_MAX && parent.find(neighbor) == parent.end()) {
                parent[neighbor] = current;
                queue.push(neighbor);
            }
        }
    }

    // Mark main street path
    std::unordered_set<uint32_t> mainStreetNodes;
    uint32_t current = churchNode;
    while (current != UINT32_MAX && parent.find(current) != parent.end()) {
        mainStreetNodes.insert(current);
        current = parent[current];
    }

    // Assign types
    for (auto& seg : network.segments) {
        if (seg.deleted) continue;

        bool onMainStreet = mainStreetNodes.count(seg.fromNode) &&
                           mainStreetNodes.count(seg.toNode);

        if (onMainStreet) {
            seg.type = StreetType::MainStreet;
        } else if (seg.isInfill) {
            seg.type = StreetType::Lane;
        } else {
            // Check depth
            int depth = std::max(
                network.nodes[seg.fromNode].depth,
                network.nodes[seg.toNode].depth
            );
            seg.type = (depth <= 2) ? StreetType::Street : StreetType::Lane;
        }
    }
}

// ============================================================================
// Phase 7: Lot Subdivision
// ============================================================================

std::vector<Lot> StreetGenerator::subdivideLots(
    const std::vector<Block>& blocks,
    const StreetNetwork& network,
    const LotConfig& config
) {
    std::vector<Lot> lots;
    uint32_t lotId = 0;

    for (const auto& block : blocks) {
        if (block.boundary.size() < 3) continue;

        // Find frontage (longest edge, assumed to face street)
        float maxEdgeLen = 0.0f;
        size_t frontageIdx = 0;

        for (size_t i = 0; i < block.boundary.size(); i++) {
            size_t j = (i + 1) % block.boundary.size();
            float len = glm::distance(block.boundary[i], block.boundary[j]);
            if (len > maxEdgeLen) {
                maxEdgeLen = len;
                frontageIdx = i;
            }
        }

        glm::vec2 frontageStart = block.boundary[frontageIdx];
        glm::vec2 frontageEnd = block.boundary[(frontageIdx + 1) % block.boundary.size()];
        glm::vec2 streetDir = glm::normalize(frontageEnd - frontageStart);
        glm::vec2 inward(-streetDir.y, streetDir.x);

        // Ensure inward points into block
        glm::vec2 blockCenter = computeCentroid(block.boundary);
        glm::vec2 frontageCenter = (frontageStart + frontageEnd) * 0.5f;
        if (glm::dot(inward, blockCenter - frontageCenter) < 0) {
            inward = -inward;
        }

        // Divide frontage into lots
        float frontageLength = glm::distance(frontageStart, frontageEnd);
        float accumulated = 0.0f;

        while (accumulated < frontageLength - config.minFrontage) {
            // Random lot width
            float width = std::uniform_real_distribution<float>(
                config.minFrontage, config.maxFrontage)(rng);

            // Don't create tiny remainder
            float remaining = frontageLength - accumulated;
            if (remaining < config.minFrontage * 1.5f) {
                width = remaining;
            } else if (remaining - width < config.minFrontage) {
                width = remaining * 0.5f;
            }

            // Corner lot bonus
            bool isCorner = (accumulated < 0.1f) ||
                           (accumulated + width > frontageLength - 0.1f);
            if (isCorner) {
                width = std::min(width * config.cornerBonus, remaining);
            }

            // Compute lot corners
            glm::vec2 frontLeft = frontageStart + streetDir * accumulated;
            glm::vec2 frontRight = frontageStart + streetDir * (accumulated + width);

            float depth = std::min(config.targetDepth,
                glm::distance(frontageCenter, blockCenter) * 2.0f);

            if (depth < config.minDepth) {
                accumulated += width;
                continue;
            }

            glm::vec2 rearLeft = frontLeft + inward * depth;
            glm::vec2 rearRight = frontRight + inward * depth;

            Lot lot;
            lot.id = lotId++;
            lot.boundary = {frontLeft, frontRight, rearRight, rearLeft};
            lot.frontageStart = frontLeft;
            lot.frontageEnd = frontRight;
            lot.frontageWidth = width;
            lot.depth = depth;
            lot.isCorner = isCorner;
            lot.adjacentStreetId = 0;  // Would need segment lookup
            lot.zone = LotZone::Residential;

            lots.push_back(lot);
            accumulated += width;
        }
    }

    return lots;
}

// ============================================================================
// Helper Methods
// ============================================================================

glm::vec2 StreetGenerator::findHighPoint(glm::vec2 center, float searchRadius) {
    glm::vec2 best = center;
    float bestHeight = -FLT_MAX;

    int samples = 16;
    for (int i = 0; i < samples; i++) {
        float angle = static_cast<float>(i) * 2.0f * 3.14159f / samples;
        for (float r = 0.0f; r <= searchRadius; r += searchRadius / 4.0f) {
            glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * r;
            float h = terrain.sampleHeight(pos.x, pos.y, terrainSize);
            if (h > bestHeight) {
                bestHeight = h;
                best = pos;
            }
        }
    }

    return best;
}

glm::vec2 StreetGenerator::avoidCollision(glm::vec2 pos, const std::vector<KeyBuilding>& existing, float minDist) {
    for (int attempt = 0; attempt < 10; attempt++) {
        bool collision = false;
        for (const auto& kb : existing) {
            if (glm::distance(pos, kb.position) < minDist + kb.radius) {
                collision = true;
                // Push away
                glm::vec2 away = glm::normalize(pos - kb.position);
                pos += away * 5.0f;
                break;
            }
        }
        if (!collision) break;
    }
    return pos;
}

float StreetGenerator::angleBetween(glm::vec2 a, glm::vec2 b) {
    float dot = glm::dot(glm::normalize(a), glm::normalize(b));
    dot = glm::clamp(dot, -1.0f, 1.0f);
    return std::acos(dot);
}

std::vector<glm::vec2> StreetGenerator::createCirclePolygon(glm::vec2 center, float radius, int segments) {
    std::vector<glm::vec2> poly;
    for (int i = 0; i < segments; i++) {
        float angle = static_cast<float>(i) * 2.0f * 3.14159f / segments;
        poly.push_back(center + glm::vec2(std::cos(angle), std::sin(angle)) * radius);
    }
    return poly;
}

float StreetGenerator::computePolygonArea(const std::vector<glm::vec2>& polygon) {
    float area = 0.0f;
    size_t n = polygon.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        area += polygon[i].x * polygon[j].y;
        area -= polygon[j].x * polygon[i].y;
    }
    return std::abs(area) * 0.5f;
}

float StreetGenerator::computePolygonPerimeter(const std::vector<glm::vec2>& polygon) {
    float perimeter = 0.0f;
    for (size_t i = 0; i < polygon.size(); i++) {
        size_t j = (i + 1) % polygon.size();
        perimeter += glm::distance(polygon[i], polygon[j]);
    }
    return perimeter;
}

glm::vec2 StreetGenerator::computeCentroid(const std::vector<glm::vec2>& polygon) {
    glm::vec2 centroid(0.0f);
    for (const auto& p : polygon) {
        centroid += p;
    }
    return centroid / static_cast<float>(polygon.size());
}

bool StreetGenerator::pointInPolygon(glm::vec2 point, const std::vector<glm::vec2>& polygon) {
    bool inside = false;
    size_t n = polygon.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((polygon[i].y > point.y) != (polygon[j].y > point.y)) &&
            (point.x < (polygon[j].x - polygon[i].x) * (point.y - polygon[i].y) /
             (polygon[j].y - polygon[i].y) + polygon[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

std::pair<std::vector<glm::vec2>, std::vector<glm::vec2>> StreetGenerator::splitPolygon(
    const std::vector<glm::vec2>& polygon,
    glm::vec2 lineStart,
    glm::vec2 lineEnd
) {
    // Simple split: divide points based on which side of line they're on
    std::vector<glm::vec2> sideA, sideB;

    glm::vec2 lineDir = lineEnd - lineStart;

    for (const auto& p : polygon) {
        glm::vec2 toPoint = p - lineStart;
        float cross = lineDir.x * toPoint.y - lineDir.y * toPoint.x;

        if (cross >= 0) {
            sideA.push_back(p);
        } else {
            sideB.push_back(p);
        }
    }

    // Add intersection points
    glm::vec2 center = (lineStart + lineEnd) * 0.5f;
    if (!sideA.empty()) sideA.push_back(center);
    if (!sideB.empty()) sideB.push_back(center);

    return {sideA, sideB};
}

// ============================================================================
// Output Functions
// ============================================================================

bool saveStreetNetworkGeoJson(const std::string& path, const StreetNetwork& network) {
    using json = nlohmann::json;

    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create streets GeoJSON: %s", path.c_str());
        return false;
    }

    json featureCollection;
    featureCollection["type"] = "FeatureCollection";
    featureCollection["properties"] = {
        {"center", {network.center.x, network.center.y}},
        {"radius", network.radius},
        {"terrain_size", network.terrainSize},
        {"total_length_m", network.getTotalStreetLength()}
    };

    json features = json::array();

    // Add street segments
    for (const auto& seg : network.segments) {
        if (seg.deleted) continue;

        const auto& fromNode = network.nodes[seg.fromNode];
        const auto& toNode = network.nodes[seg.toNode];

        json coordinates = json::array();
        coordinates.push_back({fromNode.position.x, fromNode.position.y});
        coordinates.push_back({toNode.position.x, toNode.position.y});

        json feature;
        feature["type"] = "Feature";
        feature["geometry"] = {
            {"type", "LineString"},
            {"coordinates", coordinates}
        };
        feature["properties"] = {
            {"id", seg.id},
            {"type", getStreetTypeName(seg.type)},
            {"width", getStreetWidth(seg.type)},
            {"length", seg.length},
            {"is_infill", seg.isInfill}
        };

        features.push_back(feature);
    }

    // Add key buildings as points
    for (const auto& kb : network.keyBuildings) {
        json feature;
        feature["type"] = "Feature";
        feature["geometry"] = {
            {"type", "Point"},
            {"coordinates", {kb.position.x, kb.position.y}}
        };
        feature["properties"] = {
            {"type", "key_building"},
            {"building_type", getKeyBuildingTypeName(kb.type)},
            {"radius", kb.radius}
        };

        features.push_back(feature);
    }

    featureCollection["features"] = features;

    file << featureCollection.dump(2);
    SDL_Log("Saved streets GeoJSON: %s", path.c_str());
    return true;
}

bool saveLotsGeoJson(const std::string& path, const StreetNetwork& network) {
    using json = nlohmann::json;

    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create lots GeoJSON: %s", path.c_str());
        return false;
    }

    json featureCollection;
    featureCollection["type"] = "FeatureCollection";
    featureCollection["properties"] = {
        {"total_lots", network.lots.size()}
    };

    json features = json::array();

    for (const auto& lot : network.lots) {
        json coordinates = json::array();
        json ring = json::array();
        for (const auto& p : lot.boundary) {
            ring.push_back({p.x, p.y});
        }
        // Close the ring
        if (!lot.boundary.empty()) {
            ring.push_back({lot.boundary[0].x, lot.boundary[0].y});
        }
        coordinates.push_back(ring);

        json feature;
        feature["type"] = "Feature";
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", coordinates}
        };
        feature["properties"] = {
            {"id", lot.id},
            {"frontage_width", lot.frontageWidth},
            {"depth", lot.depth},
            {"is_corner", lot.isCorner},
            {"zone", getLotZoneName(lot.zone)}
        };

        features.push_back(feature);
    }

    featureCollection["features"] = features;

    file << featureCollection.dump(2);
    SDL_Log("Saved lots GeoJSON: %s (%zu lots)", path.c_str(), network.lots.size());
    return true;
}

bool saveStreetsSVG(const std::string& path, const StreetNetwork& network) {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create streets SVG: %s", path.c_str());
        return false;
    }

    float margin = 50.0f;
    float scale = 2.0f;
    float size = (network.radius * 2.0f + margin * 2.0f) * scale;

    auto toSvg = [&](glm::vec2 p) -> glm::vec2 {
        return (p - network.center + glm::vec2(network.radius + margin)) * scale;
    };

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << size << "\" height=\"" << size << "\">\n";

    // Background
    file << "<rect width=\"100%\" height=\"100%\" fill=\"#f5f5dc\"/>\n";

    // Settlement boundary
    file << "<circle cx=\"" << size / 2 << "\" cy=\"" << size / 2
         << "\" r=\"" << network.radius * scale
         << "\" fill=\"none\" stroke=\"#888\" stroke-width=\"2\" stroke-dasharray=\"5,5\"/>\n";

    // Blocks
    file << "<g id=\"blocks\">\n";
    for (const auto& block : network.blocks) {
        if (block.boundary.size() < 3) continue;

        file << "<polygon points=\"";
        for (const auto& p : block.boundary) {
            auto sp = toSvg(p);
            file << sp.x << "," << sp.y << " ";
        }
        file << "\" fill=\"#ddd\" stroke=\"#999\" stroke-width=\"0.5\"/>\n";
    }
    file << "</g>\n";

    // Lots
    file << "<g id=\"lots\">\n";
    for (const auto& lot : network.lots) {
        if (lot.boundary.size() < 3) continue;

        std::string fill = lot.isCorner ? "#c9e4c9" : "#d9ead9";

        file << "<polygon points=\"";
        for (const auto& p : lot.boundary) {
            auto sp = toSvg(p);
            file << sp.x << "," << sp.y << " ";
        }
        file << "\" fill=\"" << fill << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";
    }
    file << "</g>\n";

    // Streets
    file << "<g id=\"streets\">\n";
    for (const auto& seg : network.segments) {
        if (seg.deleted) continue;

        const auto& fromNode = network.nodes[seg.fromNode];
        const auto& toNode = network.nodes[seg.toNode];
        auto p1 = toSvg(fromNode.position);
        auto p2 = toSvg(toNode.position);

        float width = getStreetWidth(seg.type) * scale * 0.5f;
        std::string color;
        switch (seg.type) {
            case StreetType::MainStreet: color = "#8B4513"; break;
            case StreetType::Street:     color = "#A0522D"; break;
            case StreetType::Lane:       color = "#CD853F"; break;
            case StreetType::Alley:      color = "#DEB887"; break;
        }

        file << "<line x1=\"" << p1.x << "\" y1=\"" << p1.y
             << "\" x2=\"" << p2.x << "\" y2=\"" << p2.y
             << "\" stroke=\"" << color << "\" stroke-width=\"" << width
             << "\" stroke-linecap=\"round\"/>\n";
    }
    file << "</g>\n";

    // Key buildings
    file << "<g id=\"key_buildings\">\n";
    for (const auto& kb : network.keyBuildings) {
        auto p = toSvg(kb.position);
        std::string color;
        std::string label;

        switch (kb.type) {
            case KeyBuilding::Type::Church:
                color = "#4169E1"; label = "⛪"; break;
            case KeyBuilding::Type::Market:
                color = "#228B22"; label = "🏪"; break;
            case KeyBuilding::Type::Inn:
                color = "#B8860B"; label = "🏨"; break;
            case KeyBuilding::Type::Well:
                color = "#4682B4"; label = "💧"; break;
            case KeyBuilding::Type::Green:
                color = "#32CD32"; label = "🌳"; break;
        }

        file << "<circle cx=\"" << p.x << "\" cy=\"" << p.y
             << "\" r=\"" << kb.radius * scale * 0.3f
             << "\" fill=\"" << color << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";

        file << "<text x=\"" << p.x << "\" y=\"" << p.y + 4
             << "\" text-anchor=\"middle\" font-size=\"12\">" << label << "</text>\n";
    }
    file << "</g>\n";

    // Entry points
    file << "<g id=\"entries\">\n";
    for (const auto& entry : network.entries) {
        auto p = toSvg(entry.position);
        file << "<circle cx=\"" << p.x << "\" cy=\"" << p.y
             << "\" r=\"8\" fill=\"#FF6347\" stroke=\"#333\" stroke-width=\"2\"/>\n";

        auto dir = entry.direction * 20.0f * scale;
        file << "<line x1=\"" << p.x << "\" y1=\"" << p.y
             << "\" x2=\"" << p.x + dir.x << "\" y2=\"" << p.y + dir.y
             << "\" stroke=\"#FF6347\" stroke-width=\"3\" marker-end=\"url(#arrow)\"/>\n";
    }
    file << "</g>\n";

    // Arrow marker definition
    file << "<defs>\n";
    file << "<marker id=\"arrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"9\" refY=\"3\" orient=\"auto\">\n";
    file << "<path d=\"M0,0 L0,6 L9,3 z\" fill=\"#FF6347\"/>\n";
    file << "</marker>\n";
    file << "</defs>\n";

    file << "</svg>\n";

    SDL_Log("Saved streets SVG: %s", path.c_str());
    return true;
}

} // namespace RoadGen
