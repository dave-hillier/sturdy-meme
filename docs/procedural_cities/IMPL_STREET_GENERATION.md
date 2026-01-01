# Street Network Generation - Implementation Plan

[← Back to Index](README.md)

This document details the **Hybrid Street Generation** algorithm that combines organic space colonization for the main street skeleton with regularized block infill to ensure usable lots.

---

## Problem Statement

Pure space colonization creates organic-looking streets but produces:
- Acute angles at intersections (unusable corner lots)
- Irregular blocks that are hard to subdivide
- Dead-end streets that waste space
- Blocks that are too large or oddly shaped

We need streets that **look organic** but produce **practical, subdividable blocks**.

---

## Algorithm Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    HYBRID STREET GENERATION PIPELINE                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  PHASE 1: ENTRY POINT DETECTION                                            │
│     ├── Find where external roads meet settlement boundary                  │
│     ├── Compute entry direction (tangent of incoming road)                  │
│     └── Order by road importance (MainRoad > Road > Lane)                   │
│                                                                             │
│  PHASE 2: KEY BUILDING PLACEMENT                                            │
│     ├── Place church near center (highest ground preferred)                 │
│     ├── Place market on main street axis                                    │
│     ├── Place inn near entry point                                          │
│     └── These become primary attractors                                     │
│                                                                             │
│  PHASE 3: ORGANIC SKELETON (Space Colonization)                             │
│     ├── Seed from primary entry point                                       │
│     ├── Grow toward key building attractors                                 │
│     ├── Limited branching (max 3-4 major branches)                          │
│     ├── A* refinement for terrain                                           │
│     └── Result: 3-5 main streets defining settlement character              │
│                                                                             │
│  PHASE 4: BLOCK IDENTIFICATION                                              │
│     ├── Find enclosed regions between skeleton streets                      │
│     ├── Clip to settlement boundary                                         │
│     ├── Identify oversized blocks (perimeter > threshold)                   │
│     └── Identify exterior regions (need boundary streets)                   │
│                                                                             │
│  PHASE 5: REGULARIZED INFILL                                                │
│     ├── For oversized blocks: add cross-streets                             │
│     │   ├── Perpendicular to longest adjacent street                        │
│     │   ├── Target block size: 40m × 60m                                    │
│     │   └── Slight randomization (±15%)                                     │
│     ├── For exterior: add boundary streets parallel to edge                 │
│     └── Merge nearby intersections (< 5m)                                   │
│                                                                             │
│  PHASE 6: HIERARCHY ASSIGNMENT                                              │
│     ├── Main street: entry → center (8m wide)                               │
│     ├── Streets: skeleton branches (5m wide)                                │
│     ├── Lanes: infill cross-streets (3.5m wide)                             │
│     └── Alleys: rear access (2m wide)                                       │
│                                                                             │
│  PHASE 7: LOT SUBDIVISION                                                   │
│     ├── For each block: identify street frontage edges                      │
│     ├── Subdivide perpendicular to frontage                                 │
│     ├── Target lot: 8-12m wide × 30-40m deep                                │
│     └── Corner lots get extra width (1.5× typical)                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Entry Point Detection

### Input
- Settlement center position and radius
- External road network (`RoadNetwork` from `road_generator`)

### Algorithm

```cpp
struct SettlementEntry {
    glm::vec2 position;      // Where road crosses boundary
    glm::vec2 direction;     // Inward direction (toward center)
    RoadType roadType;       // Importance of incoming road
    uint32_t roadId;         // Reference to external road
};

std::vector<SettlementEntry> findEntryPoints(
    glm::vec2 center,
    float radius,
    const RoadNetwork& roads
) {
    std::vector<SettlementEntry> entries;

    for (const auto& road : roads.roads) {
        // Check if road connects to this settlement
        bool connectsHere =
            (road.fromSettlementId == settlementId) ||
            (road.toSettlementId == settlementId);

        if (!connectsHere) continue;

        // Find intersection with settlement boundary circle
        glm::vec2 entryPos = findCircleIntersection(
            road.controlPoints, center, radius
        );

        // Compute inward direction
        glm::vec2 inward = glm::normalize(center - entryPos);

        entries.push_back({entryPos, inward, road.type, road.id});
    }

    // Sort by road importance (MainRoad first)
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) {
            return static_cast<int>(a.roadType) > static_cast<int>(b.roadType);
        });

    return entries;
}
```

### Output
- Ordered list of entry points with directions
- Primary entry = first in list (highest importance road)

---

## Phase 2: Key Building Placement

### Input
- Settlement type (Town, Village, Hamlet, etc.)
- Settlement center and radius
- Terrain data (heights, slopes)
- Entry points from Phase 1

### Algorithm

```cpp
struct KeyBuilding {
    enum class Type { Church, Market, Inn, Well, Green };

    Type type;
    glm::vec2 position;
    float radius;           // Footprint radius for collision
    float attractorWeight;  // Importance for street growth
};

std::vector<KeyBuilding> placeKeyBuildings(
    SettlementType settlementType,
    glm::vec2 center,
    float radius,
    const std::vector<SettlementEntry>& entries,
    const TerrainData& terrain
) {
    std::vector<KeyBuilding> buildings;

    // Church: near center, prefer high ground
    glm::vec2 churchPos = findHighPoint(center, radius * 0.3f, terrain);
    buildings.push_back({
        KeyBuilding::Type::Church,
        churchPos,
        15.0f,   // ~15m radius footprint
        1.0f     // Highest weight
    });

    // Market: on axis from primary entry toward center
    if (settlementType == SettlementType::Town ||
        settlementType == SettlementType::Village) {

        glm::vec2 marketPos = entries[0].position +
            entries[0].direction * (radius * 0.4f);
        // Offset slightly from church
        marketPos = avoidCollision(marketPos, buildings, 25.0f);

        buildings.push_back({
            KeyBuilding::Type::Market,
            marketPos,
            20.0f,
            0.9f
        });
    }

    // Inn: near primary entry
    if (settlementType != SettlementType::Hamlet) {
        glm::vec2 innPos = entries[0].position +
            entries[0].direction * 30.0f;
        innPos = avoidCollision(innPos, buildings, 15.0f);

        buildings.push_back({
            KeyBuilding::Type::Inn,
            innPos,
            10.0f,
            0.7f
        });
    }

    // Village green: open space, typically near church
    if (settlementType == SettlementType::Village) {
        glm::vec2 greenPos = churchPos +
            glm::vec2(20.0f, 0.0f); // East of church (traditional)
        greenPos = avoidCollision(greenPos, buildings, 15.0f);

        buildings.push_back({
            KeyBuilding::Type::Green,
            greenPos,
            25.0f,  // Larger open space
            0.5f
        });
    }

    return buildings;
}
```

### Output
- Positioned key buildings that act as attractors
- Buildings avoid overlapping via collision checks

---

## Phase 3: Organic Skeleton (Space Colonization)

### Input
- Entry points (seeds)
- Key buildings (attractors)
- Settlement boundary
- Terrain data

### Configuration

```cpp
struct SkeletonConfig {
    // Growth parameters (smaller than inter-settlement roads)
    float segmentLength = 20.0f;        // Each growth step
    float killRadius = 12.0f;           // Attractor reached when this close
    float attractionRadius = 80.0f;     // Max influence distance

    // Branching control
    int maxBranches = 4;                // Limit skeleton complexity
    float minBranchAngle = 45.0f;       // Degrees - prevent acute angles
    float maxBranchAngle = 120.0f;      // Degrees - prevent backtracking

    // Terrain awareness
    float maxSlope = 0.15f;             // 15% grade limit for streets
    float slopeCostMultiplier = 3.0f;   // Penalize slopes in A*

    // Iteration limits
    int maxIterations = 100;
};
```

### Algorithm

Extends existing `SpaceColonization` class with constraints:

```cpp
class StreetSkeleton {
public:
    struct SkeletonNode {
        uint32_t id;
        glm::vec2 position;
        uint32_t parentId;              // For tree structure
        std::vector<uint32_t> children;
        int depth;                      // Distance from entry
        bool isKeyBuilding;
        KeyBuilding::Type buildingType;
    };

    struct SkeletonSegment {
        uint32_t fromNode;
        uint32_t toNode;
        float length;
        StreetType type;  // Assigned later in Phase 6
    };

    bool generate(
        const std::vector<SettlementEntry>& entries,
        const std::vector<KeyBuilding>& keyBuildings,
        glm::vec2 center,
        float radius,
        const TerrainData& terrain,
        const SkeletonConfig& config
    ) {
        // Initialize attractors from key buildings
        std::vector<Attractor> attractors;
        for (const auto& kb : keyBuildings) {
            attractors.push_back({
                kb.position,
                kb.attractorWeight,
                false  // not reached
            });
        }

        // Add boundary attractors for coverage
        addBoundaryAttractors(center, radius, attractors, 8);

        // Seed from primary entry
        nodes.push_back({
            0,                          // id
            entries[0].position,        // position
            UINT32_MAX,                 // no parent (root)
            {},                         // no children yet
            0,                          // depth = 0
            false,                      // not a key building
            KeyBuilding::Type::Church   // unused
        });

        activeNodes.push_back(0);

        // Main colonization loop
        for (int iter = 0; iter < config.maxIterations; iter++) {
            if (activeNodes.empty()) break;
            if (allAttractorsReached(attractors)) break;

            // For each active node, compute growth direction
            std::vector<GrowthCandidate> candidates;

            for (uint32_t nodeId : activeNodes) {
                const auto& node = nodes[nodeId];

                // Find influencing attractors
                auto influencers = findInfluencers(
                    node.position, attractors, config.attractionRadius
                );

                if (influencers.empty()) continue;

                // Compute weighted growth direction
                glm::vec2 growthDir = computeGrowthDirection(
                    node, influencers
                );

                // Check branch angle constraint
                if (node.parentId != UINT32_MAX) {
                    const auto& parent = nodes[node.parentId];
                    glm::vec2 parentDir = glm::normalize(
                        node.position - parent.position
                    );
                    float angle = angleBetween(parentDir, growthDir);

                    // Clamp to allowed angle range
                    if (angle < config.minBranchAngle) continue;
                    if (angle > config.maxBranchAngle) {
                        growthDir = clampDirection(
                            parentDir, growthDir, config.maxBranchAngle
                        );
                    }
                }

                // Compute new position
                glm::vec2 newPos = node.position +
                    growthDir * config.segmentLength;

                // Check terrain slope
                float slope = terrain.sampleSlope(
                    newPos.x, newPos.y, terrain.terrainSize
                );
                if (slope > config.maxSlope) {
                    // Try A* to find better path
                    newPos = findTerrainAwarePath(
                        node.position, newPos, terrain, config
                    );
                }

                // Check still within settlement
                if (glm::distance(newPos, center) > radius) continue;

                candidates.push_back({nodeId, newPos, growthDir});
            }

            // Execute growth (limit branching)
            int branchesThisIter = 0;
            for (const auto& cand : candidates) {
                if (branchesThisIter >= config.maxBranches) break;

                // Create new node
                uint32_t newId = nodes.size();
                nodes.push_back({
                    newId,
                    cand.position,
                    cand.parentNodeId,
                    {},
                    nodes[cand.parentNodeId].depth + 1,
                    false,
                    KeyBuilding::Type::Church
                });

                // Link to parent
                nodes[cand.parentNodeId].children.push_back(newId);

                // Create segment
                segments.push_back({
                    cand.parentNodeId,
                    newId,
                    config.segmentLength,
                    StreetType::Street  // Assigned properly in Phase 6
                });

                // Check if we reached an attractor
                for (auto& attr : attractors) {
                    if (attr.reached) continue;
                    if (glm::distance(cand.position, attr.position)
                        < config.killRadius) {
                        attr.reached = true;
                        nodes.back().isKeyBuilding = true;
                        // Could store building type here
                    }
                }

                // Update active nodes
                activeNodes.push_back(newId);
                branchesThisIter++;
            }

            // Remove nodes that can't grow further
            pruneInactiveNodes(attractors, config);
        }

        return !segments.empty();
    }

private:
    std::vector<SkeletonNode> nodes;
    std::vector<SkeletonSegment> segments;
    std::vector<uint32_t> activeNodes;
};
```

### Output
- Tree of skeleton nodes (3-5 main streets)
- Segments connecting nodes
- Key buildings marked in tree

---

## Phase 4: Block Identification

### Input
- Skeleton street network from Phase 3
- Settlement boundary

### Algorithm

```cpp
struct Block {
    std::vector<glm::vec2> boundary;  // CCW polygon
    std::vector<uint32_t> adjacentStreets;  // Segment IDs
    float area;
    float perimeter;
    bool isExterior;  // Touches settlement boundary
};

class BlockIdentifier {
public:
    std::vector<Block> identifyBlocks(
        const std::vector<SkeletonSegment>& streets,
        const std::vector<SkeletonNode>& nodes,
        glm::vec2 center,
        float radius
    ) {
        // Build a planar graph from street segments
        PlanarGraph graph;
        for (const auto& seg : streets) {
            graph.addEdge(
                nodes[seg.fromNode].position,
                nodes[seg.toNode].position
            );
        }

        // Add settlement boundary as a circular polyline
        auto boundaryPoly = createCirclePolygon(center, radius, 32);
        for (size_t i = 0; i < boundaryPoly.size(); i++) {
            graph.addEdge(
                boundaryPoly[i],
                boundaryPoly[(i + 1) % boundaryPoly.size()]
            );
        }

        // Find all faces (enclosed regions)
        auto faces = graph.findFaces();

        // Convert faces to blocks
        std::vector<Block> blocks;
        for (const auto& face : faces) {
            // Skip the unbounded outer face
            if (face.isUnbounded) continue;

            Block block;
            block.boundary = face.vertices;
            block.adjacentStreets = face.edgeIds;
            block.area = computePolygonArea(block.boundary);
            block.perimeter = computePolygonPerimeter(block.boundary);
            block.isExterior = touchesBoundary(block.boundary, boundaryPoly);

            blocks.push_back(block);
        }

        return blocks;
    }
};
```

### Identifying Oversized Blocks

```cpp
struct BlockAnalysis {
    bool needsSubdivision;
    glm::vec2 suggestedSplitStart;
    glm::vec2 suggestedSplitEnd;
    glm::vec2 splitDirection;  // Perpendicular to longest frontage
};

BlockAnalysis analyzeBlock(
    const Block& block,
    float maxPerimeter = 200.0f,  // Meters
    float maxArea = 3000.0f       // Square meters
) {
    BlockAnalysis analysis;
    analysis.needsSubdivision =
        (block.perimeter > maxPerimeter) ||
        (block.area > maxArea);

    if (!analysis.needsSubdivision) return analysis;

    // Find longest edge (likely street frontage)
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
    glm::vec2 edgeDir = glm::normalize(
        block.boundary[(maxEdgeIdx + 1) % block.boundary.size()] -
        block.boundary[maxEdgeIdx]
    );
    analysis.splitDirection = glm::vec2(-edgeDir.y, edgeDir.x);  // Perpendicular

    // Find midpoint of longest edge
    glm::vec2 midpoint = (
        block.boundary[maxEdgeIdx] +
        block.boundary[(maxEdgeIdx + 1) % block.boundary.size()]
    ) * 0.5f;

    // Cast ray to find opposite edge
    analysis.suggestedSplitStart = midpoint;
    analysis.suggestedSplitEnd = findOppositeEdgeIntersection(
        block.boundary, midpoint, analysis.splitDirection
    );

    return analysis;
}
```

### Output
- List of blocks with boundaries
- Analysis of which blocks need subdivision
- Suggested split lines

---

## Phase 5: Regularized Infill

### Input
- Blocks from Phase 4
- Block analysis (which need subdivision)
- Skeleton network

### Configuration

```cpp
struct InfillConfig {
    // Target block dimensions
    float targetBlockWidth = 40.0f;   // Meters (along street)
    float targetBlockDepth = 60.0f;   // Meters (perpendicular)
    float blockSizeVariation = 0.15f; // ±15% randomization

    // Street constraints
    float minIntersectionAngle = 70.0f;  // Degrees
    float intersectionMergeRadius = 5.0f; // Merge nearby nodes

    // Lot constraints (for validation)
    float minLotFrontage = 6.0f;
    float maxLotFrontage = 15.0f;
};
```

### Algorithm

```cpp
class BlockInfill {
public:
    void subdivideOversizedBlocks(
        std::vector<Block>& blocks,
        StreetSkeleton& skeleton,
        const InfillConfig& config,
        std::mt19937& rng
    ) {
        std::vector<Block> newBlocks;

        for (auto& block : blocks) {
            auto analysis = analyzeBlock(block);

            if (!analysis.needsSubdivision) {
                newBlocks.push_back(block);
                continue;
            }

            // Add randomization to split position
            std::uniform_real_distribution<float> offsetDist(
                -config.blockSizeVariation,
                config.blockSizeVariation
            );
            float offset = offsetDist(rng);

            glm::vec2 splitStart = analysis.suggestedSplitStart;
            glm::vec2 splitEnd = analysis.suggestedSplitEnd;

            // Offset the split line slightly
            glm::vec2 perpOffset = analysis.splitDirection *
                (config.targetBlockWidth * offset);
            splitStart += perpOffset;
            splitEnd += perpOffset;

            // Add new street segment to skeleton
            uint32_t startNodeId = skeleton.addNode(splitStart);
            uint32_t endNodeId = skeleton.addNode(splitEnd);
            skeleton.addSegment(startNodeId, endNodeId, StreetType::Lane);

            // Split the block polygon
            auto [blockA, blockB] = splitPolygon(
                block.boundary, splitStart, splitEnd
            );

            // Recursively check if sub-blocks need further splitting
            Block newBlockA = {blockA, {}, computeArea(blockA),
                               computePerimeter(blockA), block.isExterior};
            Block newBlockB = {blockB, {}, computeArea(blockB),
                               computePerimeter(blockB), block.isExterior};

            // Add to processing queue (may be split again)
            newBlocks.push_back(newBlockA);
            newBlocks.push_back(newBlockB);
        }

        blocks = std::move(newBlocks);

        // Merge nearby intersections
        mergeNearbyNodes(skeleton, config.intersectionMergeRadius);
    }

private:
    void mergeNearbyNodes(StreetSkeleton& skeleton, float radius) {
        // Find clusters of nodes within radius
        std::vector<std::vector<uint32_t>> clusters;
        std::vector<bool> assigned(skeleton.nodes.size(), false);

        for (size_t i = 0; i < skeleton.nodes.size(); i++) {
            if (assigned[i]) continue;

            std::vector<uint32_t> cluster = {static_cast<uint32_t>(i)};
            assigned[i] = true;

            for (size_t j = i + 1; j < skeleton.nodes.size(); j++) {
                if (assigned[j]) continue;

                float dist = glm::distance(
                    skeleton.nodes[i].position,
                    skeleton.nodes[j].position
                );

                if (dist < radius) {
                    cluster.push_back(static_cast<uint32_t>(j));
                    assigned[j] = true;
                }
            }

            if (cluster.size() > 1) {
                clusters.push_back(cluster);
            }
        }

        // Merge each cluster into single node at centroid
        for (const auto& cluster : clusters) {
            glm::vec2 centroid(0.0f);
            for (uint32_t nodeId : cluster) {
                centroid += skeleton.nodes[nodeId].position;
            }
            centroid /= static_cast<float>(cluster.size());

            // Keep first node, update position, redirect connections
            uint32_t keepId = cluster[0];
            skeleton.nodes[keepId].position = centroid;

            for (size_t i = 1; i < cluster.size(); i++) {
                skeleton.redirectConnections(cluster[i], keepId);
                skeleton.nodes[cluster[i]].deleted = true;
            }
        }
    }
};
```

### Output
- Refined blocks with reasonable sizes
- Updated skeleton with infill streets
- No intersections closer than merge radius

---

## Phase 6: Hierarchy Assignment

### Input
- Complete street skeleton (original + infill)
- Entry points

### Algorithm

```cpp
enum class StreetType {
    MainStreet,   // 8m wide - entry to center
    Street,       // 5m wide - major branches
    Lane,         // 3.5m wide - infill cross-streets
    Alley         // 2m wide - rear access
};

float getStreetWidth(StreetType type) {
    switch (type) {
        case StreetType::MainStreet: return 8.0f;
        case StreetType::Street:     return 5.0f;
        case StreetType::Lane:       return 3.5f;
        case StreetType::Alley:      return 2.0f;
    }
}

class HierarchyAssigner {
public:
    void assignHierarchy(
        StreetSkeleton& skeleton,
        const std::vector<SettlementEntry>& entries,
        const std::vector<KeyBuilding>& keyBuildings
    ) {
        // Find path from primary entry to church (main street)
        uint32_t entryNode = findNearestNode(skeleton, entries[0].position);
        uint32_t churchNode = findNearestNode(skeleton,
            findBuilding(keyBuildings, KeyBuilding::Type::Church).position);

        auto mainStreetPath = findPath(skeleton, entryNode, churchNode);

        // Mark main street segments
        for (size_t i = 0; i + 1 < mainStreetPath.size(); i++) {
            auto& seg = skeleton.findSegment(
                mainStreetPath[i], mainStreetPath[i + 1]
            );
            seg.type = StreetType::MainStreet;
        }

        // Skeleton branches (not on main street) are Streets
        for (auto& seg : skeleton.segments) {
            if (seg.type != StreetType::MainStreet) {
                if (seg.depth <= 2) {  // Close to root
                    seg.type = StreetType::Street;
                } else if (seg.isInfill) {
                    seg.type = StreetType::Lane;
                } else {
                    seg.type = StreetType::Street;
                }
            }
        }

        // Infill segments are Lanes (already marked during infill)
        // Keep as-is

        // Add alleys for rear access (optional, for larger settlements)
        if (skeleton.nodes.size() > 20) {
            addRearAlleys(skeleton);
        }
    }

private:
    void addRearAlleys(StreetSkeleton& skeleton) {
        // For blocks with depth > 40m, add rear alley
        for (auto& block : blocks) {
            float depth = estimateBlockDepth(block);
            if (depth > 40.0f) {
                // Add alley parallel to frontage, at rear
                auto alley = createRearAlley(block, 35.0f); // 35m from front
                skeleton.addSegments(alley, StreetType::Alley);
            }
        }
    }
};
```

### Output
- All segments have assigned `StreetType`
- Widths computed from type

---

## Phase 7: Lot Subdivision

### Input
- Final blocks with street frontages identified
- Street widths from Phase 6

### Configuration

```cpp
struct LotConfig {
    float minFrontage = 6.0f;      // Minimum lot width at street
    float maxFrontage = 15.0f;     // Maximum lot width
    float targetDepth = 35.0f;     // Target lot depth
    float minDepth = 20.0f;        // Minimum viable depth
    float cornerBonus = 1.5f;      // Corner lots are this much wider
    float streetSetback = 2.0f;    // Building line from street
    float rearSetback = 3.0f;      // Rear yard minimum
};
```

### Algorithm

```cpp
struct Lot {
    std::vector<glm::vec2> boundary;  // CCW polygon
    std::vector<glm::vec2> frontage;  // Edge(s) facing street
    float frontageWidth;
    float depth;
    bool isCorner;
    uint32_t adjacentStreetId;
    LotZone zone;  // Assigned based on location
};

class LotSubdivider {
public:
    std::vector<Lot> subdividBlock(
        const Block& block,
        const StreetSkeleton& streets,
        const LotConfig& config,
        std::mt19937& rng
    ) {
        std::vector<Lot> lots;

        // Identify frontage edges (adjacent to streets)
        auto frontages = identifyFrontages(block, streets);

        if (frontages.empty()) {
            // Interior block with no street access - skip or flag error
            return lots;
        }

        // For each frontage, subdivide perpendicular
        for (const auto& frontage : frontages) {
            // Compute perpendicular direction (into block)
            glm::vec2 streetDir = glm::normalize(frontage.end - frontage.start);
            glm::vec2 inward = glm::vec2(-streetDir.y, streetDir.x);

            // Ensure inward points into block
            glm::vec2 blockCenter = computeCentroid(block.boundary);
            glm::vec2 frontageCenter = (frontage.start + frontage.end) * 0.5f;
            if (glm::dot(inward, blockCenter - frontageCenter) < 0) {
                inward = -inward;
            }

            // Divide frontage into lot widths
            float frontageLength = glm::distance(frontage.start, frontage.end);

            // Random variation in lot widths
            std::uniform_real_distribution<float> widthDist(
                config.minFrontage, config.maxFrontage
            );

            float accumulated = 0.0f;
            std::vector<float> lotWidths;

            while (accumulated < frontageLength) {
                float width = widthDist(rng);

                // Don't create tiny remainder lots
                float remaining = frontageLength - accumulated;
                if (remaining < config.minFrontage * 1.5f) {
                    // Absorb remainder into this lot
                    width = remaining;
                } else if (remaining - width < config.minFrontage) {
                    // Split evenly
                    width = remaining * 0.5f;
                }

                lotWidths.push_back(width);
                accumulated += width;

                if (accumulated >= frontageLength) break;
            }

            // Create lot polygons
            float offset = 0.0f;
            for (size_t i = 0; i < lotWidths.size(); i++) {
                float width = lotWidths[i];

                // Corner lots at ends
                bool isCorner = (i == 0 || i == lotWidths.size() - 1);
                if (isCorner) {
                    width *= config.cornerBonus;
                    if (i == 0) {
                        // Extend backward
                        offset -= width * (config.cornerBonus - 1.0f);
                    }
                }

                // Compute lot corners
                glm::vec2 frontLeft = frontage.start + streetDir * offset;
                glm::vec2 frontRight = frontage.start + streetDir * (offset + width);

                // Find depth (to block boundary or target depth)
                float depth = findLotDepth(
                    frontLeft, frontRight, inward,
                    block.boundary, config.targetDepth
                );

                if (depth < config.minDepth) {
                    // Lot too shallow, skip
                    offset += width;
                    continue;
                }

                glm::vec2 rearLeft = frontLeft + inward * depth;
                glm::vec2 rearRight = frontRight + inward * depth;

                Lot lot;
                lot.boundary = {frontLeft, frontRight, rearRight, rearLeft};
                lot.frontage = {frontLeft, frontRight};
                lot.frontageWidth = width;
                lot.depth = depth;
                lot.isCorner = isCorner;
                lot.adjacentStreetId = frontage.streetId;
                lot.zone = LotZone::Residential;  // Default, assigned later

                lots.push_back(lot);
                offset += width;
            }
        }

        return lots;
    }
};
```

### Output
- List of lots with:
  - Boundary polygons
  - Frontage edges
  - Street adjacency
  - Corner flag
  - Dimensions

---

## Data Structures Summary

```cpp
// Output file: streets.geojson
struct StreetNetworkOutput {
    float terrainSize;
    glm::vec2 settlementCenter;
    float settlementRadius;

    struct StreetFeature {
        std::vector<glm::vec2> geometry;  // LineString coordinates
        StreetType type;
        float width;
        uint32_t fromNode;
        uint32_t toNode;
    };
    std::vector<StreetFeature> streets;

    struct LotFeature {
        std::vector<glm::vec2> geometry;  // Polygon coordinates
        float frontageWidth;
        float depth;
        bool isCorner;
        LotZone zone;
        uint32_t adjacentStreetId;
    };
    std::vector<LotFeature> lots;

    struct KeyBuildingFeature {
        glm::vec2 position;
        KeyBuilding::Type type;
        float radius;
    };
    std::vector<KeyBuildingFeature> keyBuildings;
};
```

---

## Integration with Existing Code

### Extends `tools/road_generator/`

```
tools/road_generator/
├── main.cpp                 # Add --settlement mode
├── RoadPathfinder.h/.cpp    # Reuse for terrain-aware routing
├── SpaceColonization.h/.cpp # Reuse/extend for street skeleton
├── StreetGenerator.h/.cpp   # NEW: Main pipeline
├── BlockIdentifier.h/.cpp   # NEW: Phase 4
├── BlockInfill.h/.cpp       # NEW: Phase 5
├── LotSubdivider.h/.cpp     # NEW: Phase 7
└── StreetSVG.h/.cpp         # NEW: Debug visualization
```

### Command Line Interface

```bash
# Inter-settlement roads (existing)
road_generator heightmap.png biome_map.png settlements.json ./output

# Intra-settlement streets (new mode)
road_generator --settlement \
    --center 8192,8192 \
    --radius 200 \
    --type town \
    --roads ./output/roads.geojson \
    --heightmap heightmap.png \
    --output ./output/settlement_0/
```

### Output Files

```
output/settlement_0/
├── streets.geojson      # Street network with geometry
├── lots.geojson         # Lot polygons with metadata
├── key_buildings.json   # Positioned key buildings
├── streets_debug.svg    # Visualization
└── streets_debug.png    # Raster visualization
```

---

## Testing Strategy

### Visual Verification

1. **SVG Output**: Generate SVG showing:
   - Skeleton streets (thick lines)
   - Infill streets (thin lines)
   - Block outlines (polygons)
   - Lot subdivisions (hatched)
   - Key buildings (icons)

2. **Metrics Validation**:
   - All key buildings reachable from entry
   - No block perimeter > 200m after infill
   - All lots have street frontage ≥ 6m
   - No intersection angles < 60°
   - Total street length reasonable for settlement size

### Example Command

```bash
# Generate and verify
./road_generator --settlement --center 8192,8192 --radius 150 --type village \
    --roads ./generated/roads.geojson --heightmap ./terrain.png \
    --output ./generated/test_village/

# Open SVG in browser to inspect
xdg-open ./generated/test_village/streets_debug.svg
```

---

## Visual Result

```
Expected output for a Village:

                    Entry (from external road)
                         │
                         ▼
            ┌────────────●────────────┐
            │            ║            │
            │   ┌────┬───╫───┬────┐   │
            │   │ L1 │ L2║L3 │ L4 │   │  ← Lots along main street
            │   └────┴───╫───┴────┘   │
            │            ║            │
      ──────●════════════●════════════●──────  ← Main Street (8m)
            │            ║            │
            │   ┌────┬───╫───┬────┐   │
            │   │    │   ║   │    │   │
            │   │ L5 │ L6║L7 │ L8 │   │
            │   └────┴───║───┴────┘   │
            │            ║            │
            │      ┌─────●─────┐      │  ← Church
            │      │  ⛪      │      │
            │      └───────────┘      │
            │                         │
            └─────────────────────────┘

Legend:
═══  Main Street (8m)
───  Lane (3.5m)
●    Intersection
L#   Lot
⛪   Church
```

---

## Next Steps After This Plan

1. Implement `StreetGenerator` class with Phase 1-3
2. Implement `BlockIdentifier` for Phase 4
3. Implement `BlockInfill` for Phase 5
4. Implement `LotSubdivider` for Phase 7
5. Add `--settlement` mode to `road_generator/main.cpp`
6. Create SVG debug output
7. Test with various settlement types and sizes
8. Tune parameters for good visual results
