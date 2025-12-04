// Standalone erosion and sea level preprocessing tool
// Generates flow accumulation, river/lake detection, and water placement data

#include "../src/ErosionSimulator.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <memory>
#include <glm/glm.hpp>

// ============================================================================
// Space Colonization Algorithm for River Generation
// ============================================================================

struct BranchNode {
    glm::vec2 position;
    BranchNode* parent = nullptr;
    std::vector<BranchNode*> children;
    float accumulatedFlow = 1.0f;  // For line thickness (leaf = 1, accumulates upstream)

    BranchNode(glm::vec2 pos, BranchNode* p = nullptr) : position(pos), parent(p) {}
};

struct Attractor {
    glm::vec2 position;
    float flowWeight;
    bool alive = true;

    Attractor(glm::vec2 pos, float weight) : position(pos), flowWeight(weight) {}
};

// Collect all branch paths from leaves to root for SVG output (iterative)
// Returns per-point flow values for variable width rendering
void collectBranchPaths(BranchNode* root, std::vector<std::vector<glm::vec2>>& paths,
                        std::vector<std::vector<float>>& pathFlows) {
    if (!root) return;

    // Use a stack for iterative traversal
    std::vector<BranchNode*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        BranchNode* node = stack.back();
        stack.pop_back();

        // If this is a leaf (no children), trace back to root
        if (node->children.empty()) {
            std::vector<glm::vec2> path;
            std::vector<float> flows;
            BranchNode* current = node;
            while (current) {
                path.push_back(current->position);
                flows.push_back(current->accumulatedFlow);
                current = current->parent;
            }
            if (path.size() >= 2) {
                paths.push_back(path);
                pathFlows.push_back(flows);
            }
        } else {
            // Add children to stack
            for (auto* child : node->children) {
                stack.push_back(child);
            }
        }
    }
}

// Accumulate flow from leaves toward root (canalization) - iterative post-order
void accumulateFlow(BranchNode* root) {
    if (!root) return;

    // Build post-order traversal list using two stacks
    std::vector<BranchNode*> stack1;
    std::vector<BranchNode*> postOrder;

    stack1.push_back(root);
    while (!stack1.empty()) {
        BranchNode* node = stack1.back();
        stack1.pop_back();
        postOrder.push_back(node);

        for (auto* child : node->children) {
            stack1.push_back(child);
        }
    }

    // Process in reverse (leaves first, then parents)
    for (auto it = postOrder.rbegin(); it != postOrder.rend(); ++it) {
        BranchNode* node = *it;
        node->accumulatedFlow = 1.0f;
        for (auto* child : node->children) {
            node->accumulatedFlow += child->accumulatedFlow;
        }
    }
}

// Delete branch tree recursively
void deleteBranchTree(BranchNode* node) {
    if (!node) return;
    for (auto* child : node->children) {
        deleteBranchTree(child);
    }
    delete node;
}

// Generate rivers using space colonization algorithm
void saveRiversSvg(const std::string& path, const WaterPlacementData& waterData,
                   float terrainSize, float seaLevelNorm, float svgSize = 1024.0f) {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SVG file: %s", path.c_str());
        return;
    }

    uint32_t w = waterData.flowMapWidth;
    uint32_t h = waterData.flowMapHeight;

    if (w == 0 || h == 0 || waterData.flowAccumulation.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No flow accumulation data available");
        return;
    }

    // Space colonization parameters - fixed values regardless of map size
    // For smaller maps, we still want reasonable segment lengths
    float stepSize = std::max(8.0f, static_cast<float>(w) / 128.0f);  // ~8 for 1024, ~32 for 4096
    float influenceDistance = stepSize * 10.0f;  // 10x step size - large range to reach attractors
    float killDistance = stepSize * 3.0f;        // 3x step size - kill when close enough
    float flowThreshold = 0.4f;   // Higher threshold = fewer attractors, faster runtime
    int coastSampleRate = std::max(32, static_cast<int>(w / 16));  // ~64 roots for 1024
    int attractorSampleRate = std::max(16, static_cast<int>(w / 256));  // Sparser attractors for performance
    int maxIterations = 5000;
    size_t maxNodes = 100000;  // Safety limit

    // Terrain-following weight: how much to bias toward uphill direction
    // Only applied when uphill roughly aligns with attractor direction
    float terrainFollowWeight = 0.25f;  // 0 = pure attractor, 1 = pure uphill

    SDL_Log("Space colonization params: step=%.1f, influence=%.1f, kill=%.1f, threshold=%.2f, terrain=%.2f",
            stepSize, influenceDistance, killDistance, flowThreshold, terrainFollowWeight);

    // D8 direction vectors (opposite direction = uphill from flow)
    // D8: 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE
    // Uphill is opposite of flow direction
    const glm::vec2 d8Uphill[8] = {
        glm::vec2(-1,  0),  // 0: E flows, uphill is W
        glm::vec2(-1, -1),  // 1: SE flows, uphill is NW
        glm::vec2( 0, -1),  // 2: S flows, uphill is N
        glm::vec2( 1, -1),  // 3: SW flows, uphill is NE
        glm::vec2( 1,  0),  // 4: W flows, uphill is E
        glm::vec2( 1,  1),  // 5: NW flows, uphill is SE
        glm::vec2( 0,  1),  // 6: N flows, uphill is S
        glm::vec2(-1,  1),  // 7: NE flows, uphill is SW
    };

    // Helper to get uphill direction at a position
    auto getUphillDir = [&](const glm::vec2& pos) -> glm::vec2 {
        int px = static_cast<int>(pos.x);
        int py = static_cast<int>(pos.y);
        if (px < 0 || px >= static_cast<int>(w) || py < 0 || py >= static_cast<int>(h)) {
            return glm::vec2(0.0f);
        }
        int8_t dir = waterData.flowDirection[py * w + px];
        if (dir < 0 || dir > 7) {
            return glm::vec2(0.0f);  // At outlet or invalid
        }
        return glm::normalize(d8Uphill[dir]);
    };

    // -------------------------------------------------------------------------
    // Step 1: Find coastal cells and create root nodes
    // -------------------------------------------------------------------------
    std::vector<std::unique_ptr<BranchNode>> rootNodes;
    std::vector<BranchNode*> activeNodes;  // Current branch tips

    // Coastal cells: only cells below sea level (flow direction = -1 indicates outlet)
    // We exclude map edges to avoid rivers starting at arbitrary terrain boundaries
    for (uint32_t y = 1; y < h - 1; y += coastSampleRate) {
        for (uint32_t x = 1; x < w - 1; x += coastSampleRate) {
            if (waterData.flowDirection[y * w + x] < 0) {
                glm::vec2 pos(static_cast<float>(x), static_cast<float>(y));
                auto node = std::make_unique<BranchNode>(pos);
                activeNodes.push_back(node.get());
                rootNodes.push_back(std::move(node));
            }
        }
    }

    SDL_Log("Created %zu coastal root nodes", rootNodes.size());

    // -------------------------------------------------------------------------
    // Step 2: Create attractors from high-flow cells
    // -------------------------------------------------------------------------
    std::vector<Attractor> attractors;

    // Sample attractors (not every cell, for performance)
    for (uint32_t y = 1; y < h - 1; y += attractorSampleRate) {
        for (uint32_t x = 1; x < w - 1; x += attractorSampleRate) {
            float flow = waterData.flowAccumulation[y * w + x];
            if (flow >= flowThreshold) {
                glm::vec2 pos(static_cast<float>(x), static_cast<float>(y));
                attractors.emplace_back(pos, flow);
            }
        }
    }

    SDL_Log("Created %zu attractors from high-flow cells", attractors.size());

    // -------------------------------------------------------------------------
    // Step 3: Space colonization main loop
    // -------------------------------------------------------------------------
    std::vector<BranchNode*> allNodes;  // For cleanup and spatial queries
    allNodes.reserve(maxNodes);  // Pre-allocate to avoid reallocation issues
    for (auto& root : rootNodes) {
        allNodes.push_back(root.get());
    }

    int iteration = 0;
    size_t aliveAttractors = attractors.size();

    while (aliveAttractors > 0 && iteration < maxIterations && !activeNodes.empty() && allNodes.size() < maxNodes) {
        iteration++;

        // Track which nodes have attractors associated with them
        std::vector<std::vector<size_t>> nodeAttractors(allNodes.size());

        // Associate each attractor with its nearest branch node within influence distance
        for (size_t ai = 0; ai < attractors.size(); ai++) {
            if (!attractors[ai].alive) continue;

            float minDist = influenceDistance;
            size_t nearestNode = SIZE_MAX;

            for (size_t ni = 0; ni < allNodes.size(); ni++) {
                float dist = glm::distance(attractors[ai].position, allNodes[ni]->position);
                if (dist < minDist) {
                    minDist = dist;
                    nearestNode = ni;
                }
            }

            if (nearestNode != SIZE_MAX && nearestNode < nodeAttractors.size()) {
                nodeAttractors[nearestNode].push_back(ai);
            }
        }

        // Grow new nodes from nodes that have associated attractors
        std::vector<BranchNode*> newActiveNodes;

        for (size_t ni = 0; ni < allNodes.size(); ni++) {
            if (ni >= nodeAttractors.size() || nodeAttractors[ni].empty()) continue;

            // Calculate average direction toward all associated attractors (weighted by flow)
            glm::vec2 avgDir(0.0f);
            float totalWeight = 0.0f;

            for (size_t ai : nodeAttractors[ni]) {
                glm::vec2 dir = attractors[ai].position - allNodes[ni]->position;
                float dist = glm::length(dir);
                if (dist > 0.001f) {
                    dir /= dist;  // Normalize
                    float weight = attractors[ai].flowWeight;
                    avgDir += dir * weight;
                    totalWeight += weight;
                }
            }

            if (totalWeight > 0.0f) {
                avgDir /= totalWeight;
                float len = glm::length(avgDir);
                if (len > 0.001f) {
                    avgDir /= len;  // Normalize

                    // Blend attractor direction with uphill (terrain-following) direction
                    // Only apply terrain bias when directions roughly align (to avoid fighting)
                    glm::vec2 uphillDir = getUphillDir(allNodes[ni]->position);
                    glm::vec2 finalDir = avgDir;  // Default to attractor direction

                    float uphillLen = glm::length(uphillDir);
                    if (uphillLen > 0.001f) {
                        uphillDir /= uphillLen;
                        float alignment = glm::dot(avgDir, uphillDir);
                        // Only blend if uphill direction is roughly aligned (within ~90 degrees)
                        if (alignment > 0.0f) {
                            // Scale weight by alignment (stronger when well-aligned)
                            float effectiveWeight = terrainFollowWeight * alignment;
                            finalDir = avgDir * (1.0f - effectiveWeight) + uphillDir * effectiveWeight;
                            float finalLen = glm::length(finalDir);
                            if (finalLen > 0.001f) {
                                finalDir /= finalLen;
                            }
                        }
                    }

                    // Create new node
                    glm::vec2 newPos = allNodes[ni]->position + finalDir * stepSize;

                    // Bounds check
                    if (newPos.x >= 0 && newPos.x < w && newPos.y >= 0 && newPos.y < h) {
                        auto* newNode = new BranchNode(newPos, allNodes[ni]);
                        allNodes[ni]->children.push_back(newNode);
                        allNodes.push_back(newNode);
                        newActiveNodes.push_back(newNode);
                    }
                }
            }
        }

        activeNodes = newActiveNodes;

        // Kill attractors within kill distance of any branch node
        for (auto& attractor : attractors) {
            if (!attractor.alive) continue;

            for (auto* node : allNodes) {
                float dist = glm::distance(attractor.position, node->position);
                if (dist < killDistance) {
                    attractor.alive = false;
                    aliveAttractors--;
                    break;
                }
            }
        }

        // Progress logging
        if (iteration % 100 == 0 || iteration < 10) {
            SDL_Log("  Iteration %d: %zu nodes, %zu active, %zu attractors remaining",
                    iteration, allNodes.size(), activeNodes.size(), aliveAttractors);
        }
    }

    SDL_Log("Space colonization completed: %d iterations, %zu total nodes", iteration, allNodes.size());

    // Early exit if no nodes were created
    if (allNodes.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No nodes created during space colonization");
        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svgSize << "\" height=\"" << svgSize << "\"/>\n";
        return;
    }

    // -------------------------------------------------------------------------
    // Step 4: Canalization - accumulate flow from leaves toward roots
    // -------------------------------------------------------------------------
    for (auto& root : rootNodes) {
        accumulateFlow(root.get());
    }

    // Find max flow for normalization
    float maxAccumFlow = 1.0f;
    for (auto* node : allNodes) {
        maxAccumFlow = std::max(maxAccumFlow, node->accumulatedFlow);
    }

    // -------------------------------------------------------------------------
    // Step 5: Collect branch paths and write SVG
    // -------------------------------------------------------------------------
    std::vector<std::vector<glm::vec2>> paths;
    std::vector<std::vector<float>> pathFlows;  // Per-point flow values

    for (auto& root : rootNodes) {
        collectBranchPaths(root.get(), paths, pathFlows);
    }

    SDL_Log("Collected %zu river paths for SVG", paths.size());

    // SVG header
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << svgSize << "\" height=\"" << svgSize << "\" "
         << "viewBox=\"0 0 " << svgSize << " " << svgSize << "\">\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"#1a1a2e\"/>\n";

    // Draw sea/ocean areas (cells below sea level, flowDirection < 0)
    file << "  <g fill=\"#2a5a7a\" opacity=\"0.9\">\n";
    int seaSampleRate = std::max(1, static_cast<int>(w / 512));  // Sample rate for sea rendering
    float cellSize = svgSize / w * seaSampleRate * 1.1f;  // Slightly overlap to avoid gaps
    for (uint32_t y = 0; y < h; y += seaSampleRate) {
        for (uint32_t x = 0; x < w; x += seaSampleRate) {
            if (waterData.flowDirection[y * w + x] < 0) {
                float svgX = static_cast<float>(x) / w * svgSize;
                float svgY = static_cast<float>(y) / h * svgSize;
                file << "    <rect x=\"" << svgX << "\" y=\"" << svgY
                     << "\" width=\"" << cellSize << "\" height=\"" << cellSize << "\"/>\n";
            }
        }
    }
    file << "  </g>\n";

    // Helper to get max flow in a path (for sorting)
    auto getMaxFlow = [&](size_t pathIdx) {
        float maxFlow = 0.0f;
        for (float f : pathFlows[pathIdx]) {
            maxFlow = std::max(maxFlow, f);
        }
        return maxFlow;
    };

    // Sort paths by max width (thinnest first so thicker rivers render on top)
    std::vector<size_t> pathOrder(paths.size());
    for (size_t i = 0; i < pathOrder.size(); i++) pathOrder[i] = i;
    std::sort(pathOrder.begin(), pathOrder.end(), [&](size_t a, size_t b) {
        return getMaxFlow(a) < getMaxFlow(b);
    });

    // Convert to SVG coordinates
    auto toSvg = [&](const glm::vec2& p) {
        return glm::vec2(p.x / w * svgSize, p.y / h * svgSize);
    };

    // River width parameters
    float minRiverWidth = 0.3f;   // Width at headwaters (leaf nodes)
    float maxRiverWidth = 4.0f;   // Width at coast (root nodes)
    float logMaxFlow = std::log(maxAccumFlow + 1.0f);

    // Draw each river as a filled polygon with variable width
    for (size_t idx : pathOrder) {
        const auto& riverPath = paths[idx];
        const auto& flows = pathFlows[idx];
        if (riverPath.size() < 2) continue;

        // Build left and right edges of the river polygon
        std::vector<glm::vec2> leftEdge;
        std::vector<glm::vec2> rightEdge;

        for (size_t i = 0; i < riverPath.size(); i++) {
            glm::vec2 pos = toSvg(riverPath[i]);

            // Calculate direction (perpendicular for width)
            glm::vec2 dir;
            if (i == 0) {
                dir = toSvg(riverPath[1]) - pos;
            } else if (i == riverPath.size() - 1) {
                dir = pos - toSvg(riverPath[i - 1]);
            } else {
                dir = toSvg(riverPath[i + 1]) - toSvg(riverPath[i - 1]);
            }

            float len = glm::length(dir);
            if (len < 0.001f) {
                dir = glm::vec2(1.0f, 0.0f);
            } else {
                dir /= len;
            }

            // Perpendicular (90 degrees)
            glm::vec2 perp(-dir.y, dir.x);

            // Width based on accumulated flow at this point
            float flowNorm = std::log(flows[i] + 1.0f) / logMaxFlow;
            float halfWidth = (minRiverWidth + flowNorm * (maxRiverWidth - minRiverWidth)) * 0.5f;

            leftEdge.push_back(pos + perp * halfWidth);
            rightEdge.push_back(pos - perp * halfWidth);
        }

        // Color based on average flow (darker at headwaters, brighter at coast)
        float avgFlowNorm = 0.0f;
        for (float f : flows) {
            avgFlowNorm += std::log(f + 1.0f) / logMaxFlow;
        }
        avgFlowNorm /= flows.size();
        int blue = 150 + static_cast<int>(105 * avgFlowNorm);
        int green = 100 + static_cast<int>(80 * avgFlowNorm);

        // Build SVG path: left edge forward, then right edge backward
        file << "  <path d=\"";
        file << "M " << leftEdge[0].x << " " << leftEdge[0].y;

        // Left edge (forward)
        for (size_t i = 1; i < leftEdge.size(); i++) {
            file << " L " << leftEdge[i].x << " " << leftEdge[i].y;
        }

        // Right edge (backward)
        for (size_t i = rightEdge.size(); i > 0; i--) {
            file << " L " << rightEdge[i - 1].x << " " << rightEdge[i - 1].y;
        }

        file << " Z\" fill=\"rgb(50," << green << "," << blue << ")\" "
             << "opacity=\"0.85\"/>\n";
    }

    // SVG footer
    file << "</svg>\n";

    // Cleanup dynamically allocated nodes (roots are in unique_ptrs, but children are raw)
    for (auto& root : rootNodes) {
        for (auto* child : root->children) {
            deleteBranchTree(child);
        }
        root->children.clear();
    }

    SDL_Log("Rivers SVG saved: %s (%zu paths)", path.c_str(), paths.size());
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <cache_directory> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --num-droplets <value>        Number of water droplets to simulate (default: 500000)\n"
              << "  --max-lifetime <value>        Max steps per droplet (default: 512)\n"
              << "  --output-resolution <value>   Flow map resolution (default: 4096)\n"
              << "  --river-threshold <value>     Min normalized flow to be river [0-1] (default: 0.15)\n"
              << "  --river-min-width <value>     Minimum river width in world units (default: 5.0)\n"
              << "  --river-max-width <value>     Maximum river width in world units (default: 80.0)\n"
              << "  --lake-min-area <value>       Minimum lake area in world units squared (default: 500.0)\n"
              << "  --lake-min-depth <value>      Minimum depression depth for lakes (default: 2.0)\n"
              << "  --sea-level <value>           Height below which is sea (default: 0.0)\n"
              << "  --terrain-size <value>        World size of terrain (default: 16384.0)\n"
              << "  --min-altitude <value>        Min altitude in heightmap (default: 0.0)\n"
              << "  --max-altitude <value>        Max altitude in heightmap (default: 200.0)\n"
              << "  --help                        Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./terrain_cache --sea-level 23 --terrain-size 16384\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    ErosionConfig config{};
    config.sourceHeightmapPath = argv[1];
    config.cacheDirectory = argv[2];
    config.numDroplets = 500000;
    config.maxDropletLifetime = 512;
    config.inertia = 0.3f;
    config.gravity = 10.0f;
    config.evaporationRate = 0.02f;
    config.minWater = 0.001f;
    config.outputResolution = 4096;
    config.riverFlowThreshold = 0.15f;
    config.riverMinWidth = 5.0f;
    config.riverMaxWidth = 80.0f;
    config.splineSimplifyTolerance = 5.0f;
    config.lakeMinArea = 500.0f;
    config.lakeMinDepth = 2.0f;
    config.seaLevel = 0.0f;
    config.terrainSize = 16384.0f;
    config.minAltitude = 0.0f;
    config.maxAltitude = 200.0f;

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--num-droplets" && i + 1 < argc) {
            config.numDroplets = std::stoul(argv[++i]);
        } else if (arg == "--max-lifetime" && i + 1 < argc) {
            config.maxDropletLifetime = std::stoul(argv[++i]);
        } else if (arg == "--output-resolution" && i + 1 < argc) {
            config.outputResolution = std::stoul(argv[++i]);
        } else if (arg == "--river-threshold" && i + 1 < argc) {
            config.riverFlowThreshold = std::stof(argv[++i]);
        } else if (arg == "--river-min-width" && i + 1 < argc) {
            config.riverMinWidth = std::stof(argv[++i]);
        } else if (arg == "--river-max-width" && i + 1 < argc) {
            config.riverMaxWidth = std::stof(argv[++i]);
        } else if (arg == "--lake-min-area" && i + 1 < argc) {
            config.lakeMinArea = std::stof(argv[++i]);
        } else if (arg == "--lake-min-depth" && i + 1 < argc) {
            config.lakeMinDepth = std::stof(argv[++i]);
        } else if (arg == "--sea-level" && i + 1 < argc) {
            config.seaLevel = std::stof(argv[++i]);
        } else if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Erosion & Water Placement Preprocessor");
    SDL_Log("======================================");
    SDL_Log("Source: %s", config.sourceHeightmapPath.c_str());
    SDL_Log("Cache: %s", config.cacheDirectory.c_str());
    SDL_Log("Droplets: %u (max lifetime: %u)", config.numDroplets, config.maxDropletLifetime);
    SDL_Log("Output resolution: %u", config.outputResolution);
    SDL_Log("River flow threshold: %.2f", config.riverFlowThreshold);
    SDL_Log("River width: %.1f - %.1f", config.riverMinWidth, config.riverMaxWidth);
    SDL_Log("Lake min area: %.1f, min depth: %.1f", config.lakeMinArea, config.lakeMinDepth);
    SDL_Log("Sea level: %.1f", config.seaLevel);
    SDL_Log("Terrain size: %.1f", config.terrainSize);
    SDL_Log("Altitude range: %.1f to %.1f", config.minAltitude, config.maxAltitude);

    ErosionSimulator simulator;

    SDL_Log("Running erosion simulation...");

    bool success = simulator.simulate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (success) {
        const auto& waterData = simulator.getWaterData();
        SDL_Log("Simulation complete!");
        SDL_Log("Results:");
        SDL_Log("  Rivers detected: %zu", waterData.rivers.size());
        SDL_Log("  Lakes detected: %zu", waterData.lakes.size());
        SDL_Log("  Sea level: %.1f", waterData.seaLevel);
        SDL_Log("  Flow map: %ux%u", waterData.flowMapWidth, waterData.flowMapHeight);
        SDL_Log("  Max flow value: %.4f", waterData.maxFlowValue);
        SDL_Log("Preview image saved to: %s/erosion_preview.png", config.cacheDirectory.c_str());

        // Export rivers as SVG using space colonization
        std::string svgPath = config.cacheDirectory + "/rivers.svg";
        float heightRange = config.maxAltitude - config.minAltitude;
        float seaLevelNorm = (config.seaLevel - config.minAltitude) / heightRange;
        saveRiversSvg(svgPath, waterData, config.terrainSize, seaLevelNorm);

        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Simulation failed!");
        return 1;
    }
}
