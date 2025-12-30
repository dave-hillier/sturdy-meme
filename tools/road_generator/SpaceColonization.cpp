#include "SpaceColonization.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace RoadGen {

SpaceColonization::GrowthNode* SpaceColonization::findClosestGrowthNode(
    std::vector<GrowthNode>& growthNodes,
    const glm::vec2& target,
    float maxDist
) {
    GrowthNode* closest = nullptr;
    float minDist = maxDist;

    for (auto& node : growthNodes) {
        if (!node.active) continue;

        float dist = glm::distance(node.position, target);
        if (dist < minDist) {
            minDist = dist;
            closest = &node;
        }
    }

    return closest;
}

glm::vec2 SpaceColonization::calculateGrowthDirection(
    const GrowthNode& node,
    const std::vector<AttractionPoint>& attractions,
    float attractionRadius
) {
    glm::vec2 direction(0.0f);
    int influenceCount = 0;

    for (const auto& attr : attractions) {
        if (attr.reached) continue;

        float dist = glm::distance(node.position, attr.position);
        if (dist < attractionRadius && dist > 0.001f) {
            // Weight by inverse distance (closer = stronger pull)
            float weight = 1.0f / dist;
            glm::vec2 toAttr = glm::normalize(attr.position - node.position);
            direction += toAttr * weight;
            influenceCount++;
        }
    }

    if (influenceCount > 0 && glm::length(direction) > 0.001f) {
        return glm::normalize(direction);
    }

    // No influence - continue in current direction or random
    if (glm::length(node.growthDirection) > 0.001f) {
        return node.growthDirection;
    }

    return glm::vec2(1.0f, 0.0f);  // Default direction
}

bool SpaceColonization::buildNetwork(
    const std::vector<glm::vec2>& attractionPoints,
    const std::vector<glm::vec2>& rootPoints,
    const std::vector<uint32_t>& settlementIds,
    const std::vector<uint32_t>& rootSettlementIds,
    const ColonizationConfig& config,
    ColonizationResult& outResult,
    ProgressCallback callback
) {
    outResult.nodes.clear();
    outResult.edges.clear();

    if (attractionPoints.empty() || rootPoints.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No attraction or root points for colonization");
        return false;
    }

    if (callback) callback(0.0f, "Initializing space colonization...");

    // Initialize attraction points
    std::vector<AttractionPoint> attractions;
    for (size_t i = 0; i < attractionPoints.size(); i++) {
        AttractionPoint attr;
        attr.position = attractionPoints[i];
        attr.settlementId = settlementIds[i];
        attr.reached = false;

        // Check if this is also a root point (already connected)
        for (size_t j = 0; j < rootPoints.size(); j++) {
            if (glm::distance(attractionPoints[i], rootPoints[j]) < 1.0f) {
                attr.reached = true;
                break;
            }
        }

        attractions.push_back(attr);
    }

    // Create initial nodes for root points (towns)
    std::vector<GrowthNode> growthNodes;
    uint32_t nextNodeId = 0;

    for (size_t i = 0; i < rootPoints.size(); i++) {
        // Create network node
        NetworkNode netNode;
        netNode.id = nextNodeId;
        netNode.position = rootPoints[i];
        netNode.isSettlement = true;
        netNode.settlementId = rootSettlementIds[i];
        outResult.nodes.push_back(netNode);

        // Create growth node
        GrowthNode gNode;
        gNode.nodeId = nextNodeId;
        gNode.position = rootPoints[i];
        gNode.growthDirection = glm::vec2(0.0f);
        gNode.depth = 0;
        gNode.active = true;
        growthNodes.push_back(gNode);

        nextNodeId++;
    }

    SDL_Log("Space colonization: %zu root nodes, %zu attraction points",
            rootPoints.size(), attractions.size());

    // Main colonization loop
    int iteration = 0;
    int settlementsReached = 0;
    int totalSettlements = 0;

    for (const auto& attr : attractions) {
        if (!attr.reached) totalSettlements++;
    }

    while (iteration < config.maxIterations) {
        iteration++;

        if (callback && iteration % 10 == 0) {
            float progress = static_cast<float>(settlementsReached) / (totalSettlements + 1);
            callback(progress, "Growing network... (" + std::to_string(settlementsReached) +
                    "/" + std::to_string(totalSettlements) + " settlements)");
        }

        // Check if all attractions are reached
        bool allReached = true;
        for (const auto& attr : attractions) {
            if (!attr.reached) {
                allReached = false;
                break;
            }
        }
        if (allReached) break;

        // For each active growth node, calculate growth direction
        std::vector<std::pair<size_t, glm::vec2>> growthDirections;

        for (size_t i = 0; i < growthNodes.size(); i++) {
            if (!growthNodes[i].active) continue;

            glm::vec2 dir = calculateGrowthDirection(
                growthNodes[i], attractions, config.attractionRadius
            );

            if (glm::length(dir) > 0.001f) {
                growthDirections.push_back({i, dir});
            }
        }

        if (growthDirections.empty()) {
            // No growth possible - might need to activate more nodes or we're done
            break;
        }

        // Grow each node
        for (const auto& [nodeIdx, direction] : growthDirections) {
            GrowthNode& gNode = growthNodes[nodeIdx];

            // Calculate new position
            glm::vec2 newPos = gNode.position + direction * config.branchLength;

            // Check if we've reached any attraction point
            bool reachedAttraction = false;
            uint32_t reachedSettlementId = 0;

            for (auto& attr : attractions) {
                if (attr.reached) continue;

                float dist = glm::distance(newPos, attr.position);
                if (dist < config.killRadius) {
                    // Snap to the attraction point
                    newPos = attr.position;
                    attr.reached = true;
                    reachedAttraction = true;
                    reachedSettlementId = attr.settlementId;
                    settlementsReached++;
                    break;
                }
            }

            // Create new network node
            NetworkNode newNetNode;
            newNetNode.id = nextNodeId;
            newNetNode.position = newPos;
            newNetNode.isSettlement = reachedAttraction;
            newNetNode.settlementId = reachedSettlementId;

            // Connect to parent
            newNetNode.connections.push_back(gNode.nodeId);
            outResult.nodes[gNode.nodeId].connections.push_back(nextNodeId);

            outResult.nodes.push_back(newNetNode);

            // Create edge
            NetworkEdge edge;
            edge.fromNode = gNode.nodeId;
            edge.toNode = nextNodeId;
            edge.length = glm::distance(gNode.position, newPos);
            edge.depth = gNode.depth;
            outResult.edges.push_back(edge);

            // Create new growth node (or update existing)
            if (reachedAttraction) {
                // Settlement reached - this becomes a new growth point
                GrowthNode newGNode;
                newGNode.nodeId = nextNodeId;
                newGNode.position = newPos;
                newGNode.growthDirection = direction;
                newGNode.depth = gNode.depth + 1;
                newGNode.active = true;
                growthNodes.push_back(newGNode);

                // Deactivate parent if it was heading primarily to this settlement
                // (simple heuristic: if growth direction was very aligned)
                float alignment = glm::dot(direction, glm::normalize(newPos - gNode.position));
                if (alignment > 0.9f) {
                    gNode.active = false;
                }
            } else {
                // Continue growing from new position
                GrowthNode newGNode;
                newGNode.nodeId = nextNodeId;
                newGNode.position = newPos;
                newGNode.growthDirection = direction;
                newGNode.depth = gNode.depth;
                newGNode.active = true;
                growthNodes.push_back(newGNode);

                // Deactivate old position
                gNode.active = false;
            }

            nextNodeId++;
        }
    }

    // Log results
    int junctionCount = 0;
    int settlementNodeCount = 0;
    for (const auto& node : outResult.nodes) {
        if (node.isSettlement) settlementNodeCount++;
        else junctionCount++;
    }

    SDL_Log("Space colonization complete: %d iterations", iteration);
    SDL_Log("  Nodes: %zu (%d settlements, %d junctions)",
            outResult.nodes.size(), settlementNodeCount, junctionCount);
    SDL_Log("  Edges: %zu", outResult.edges.size());
    SDL_Log("  Settlements reached: %d/%d", settlementsReached, totalSettlements);

    if (callback) callback(1.0f, "Space colonization complete");

    return true;
}

} // namespace RoadGen
