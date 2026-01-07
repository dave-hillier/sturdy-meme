#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Bisector.h"
#include "town_generator/utils/Random.h"
#include "town_generator/wards/Ward.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

static bool isEdgeOnRoad(const geom::Point& v0, const geom::Point& v1,
                         const std::vector<City::Street>& roads);

WardGroup::WardGroup(City* model) : model(model) {}

void WardGroup::addPatch(Cell* patch) {
    if (!patch) return;

    cells.push_back(patch);
    patch->group = this;

    if (!core) {
        core = patch;
        urban = patch->withinWalls;
    }
}

void WardGroup::buildBorder() {
    if (cells.empty()) return;

    if (cells.size() == 1) {
        border = cells[0]->shape;
    } else {
        // Use City::findCircumference to get combined border
        border = City::findCircumference(cells);
    }

    // Compute inner vertices after building border (faithful to mfcg.js)
    computeInnerVertices();
}

void WardGroup::createParams() {
    // Faithful to mfcg.js District.createParams()
    // Uses normal3 and normal4 distributions for natural variation

    // minSq: 15 + 40 * abs(normal4 - 1) where normal4 is sum of 4 randoms / 2
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.minSq = 15.0 + 40.0 * std::abs(normal4);

    // gridChaos: 0.2 + normal3 * 0.8
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal()) / 3.0;
    alleys.gridChaos = 0.2 + normal3 * 0.8;

    // sizeChaos: 0.4 + normal3 * 0.6
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.sizeChaos = 0.4 + normal3 * 0.6;

    // shapeFactor: 0.25 + normal3 * 2
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.shapeFactor = 0.25 + normal3 * 2.0;

    // inset: 0.6 * (1 - abs(normal4))
    normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.inset = 0.6 * (1.0 - std::abs(normal4));

    // blockSize: 4 + 10 * normal3
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.blockSize = 4.0 + 10.0 * normal3;

    // Compute derived values
    alleys.computeDerived();

    // greenery: normal3^2 (or ^1 for parks)
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    std::string typeName = getTypeName();
    greenery = (typeName == "Park") ? normal3 : normal3 * normal3;

    // Adjust for sprawl (outer areas)
    if (!urban) {
        alleys.gridChaos *= 0.5;
        alleys.blockSize *= 2.0;
        greenery = (1.0 + greenery) / 2.0;
    }
}

void WardGroup::createGeometry() {
    if (border.length() < 3) {
        buildBorder();
    }

    if (border.length() < 3) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WardGroup: Cannot create geometry with invalid border");
        return;
    }

    createParams();

    // Get available area after street/wall insets
    // Calculate per-edge insets based on what's adjacent (roads, walls, etc.)
    std::vector<double> insets = getAvailable();
    geom::Polygon available = border.shrink(insets);

    if (available.length() < 3) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WardGroup: Available area too small after inset");
        return;
    }

    // Create blocks using Bisector (faithful to mfcg.js createAlleys)
    // MFCG: new Bisector(shape, alleys.minSq * alleys.blockSize, 16 * alleys.gridChaos)
    double minArea = alleys.minSq * alleys.blockSize;
    double variance = 16.0 * alleys.gridChaos;
    Bisector bisector(available, minArea, variance);

    // Set gap callback (returns Ward::ALLEY = 1.2)
    bisector.getGap = [](const std::vector<geom::Point>&) {
        return wards::Ward::ALLEY;
    };

    // For non-urban areas, use isBlockSized as atomic check
    if (!urban) {
        double blockSizeThreshold = alleys.minSq * alleys.blockSize;
        bisector.isAtomic = [blockSizeThreshold](const geom::Polygon& shape) {
            return std::abs(shape.square()) < blockSizeThreshold;
        };
    }

    // Partition the area into blocks
    std::vector<geom::Polygon> blockShapes = bisector.partition();

    // Store alley cuts for SVG rendering
    alleyCuts = bisector.cuts;

    // Create Block objects from shapes
    blocks.clear();
    size_t totalBuildings = 0;
    for (const auto& shape : blockShapes) {
        auto block = std::make_unique<Block>(shape, this);
        block->createLots();
        block->createRects();
        block->createBuildings();
        totalBuildings += block->buildings.size();
        blocks.push_back(std::move(block));
    }

    SDL_Log("WardGroup: Created %zu blocks with %zu buildings, %zu alley cuts from %zu cells",
            blocks.size(), totalBuildings, alleyCuts.size(), cells.size());
}

std::vector<geom::Point> WardGroup::spawnTrees() {
    std::vector<geom::Point> trees;

    for (const auto& block : blocks) {
        auto blockTrees = block->spawnTrees();
        trees.insert(trees.end(), blockTrees.begin(), blockTrees.end());
    }

    return trees;
}

bool WardGroup::canAddPatch(Cell* patch) const {
    if (!patch || !patch->ward) return false;
    if (cells.empty()) return true;

    // Must be same ward type
    std::string typeName = getTypeName();
    if (patch->ward->getName() != typeName) return false;

    // Must be adjacent to at least one patch in the group
    for (Cell* existing : cells) {
        for (Cell* neighbor : existing->neighbors) {
            if (neighbor == patch) return true;
        }
    }

    return false;
}

std::string WardGroup::getTypeName() const {
    if (cells.empty() || !cells[0]->ward) return "";
    return cells[0]->ward->getName();
}

bool WardGroup::isInnerVertex(const geom::Point& v) const {
    // Faithful to mfcg.js District.isInnerVertex (lines 979-984)
    // A vertex is "inner" if ALL adjacent cells are withinCity OR waterbody
    if (!model) return false;

    auto adjacentPatches = model->cellsByVertex(v);
    for (auto* p : adjacentPatches) {
        if (!p->withinCity && !p->waterbody) {
            return false;
        }
    }
    return true;
}

void WardGroup::computeInnerVertices() {
    // Faithful to mfcg.js District constructor (lines 719-724)
    // For each border vertex, check if withinWalls OR isInnerVertex
    inner.clear();

    if (border.length() < 3) return;

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v = border[i];

        // Check if any patch at this vertex is withinWalls
        bool withinWalls = false;
        if (model) {
            auto adjacentPatches = model->cellsByVertex(v);
            for (auto* p : adjacentPatches) {
                if (p->withinWalls) {
                    withinWalls = true;
                    break;
                }
            }
        }

        // A vertex is "inner" if withinWalls OR isInnerVertex
        if (withinWalls || isInnerVertex(v)) {
            inner.push_back(v);
        }
    }

    // District is "urban" if all border vertices are inner
    urban = (inner.size() == border.length());
}

std::vector<double> WardGroup::getAvailable() const {
    // Calculate per-edge inset distances based on what's adjacent
    // Faithful to MFCG WardGroup.getAvailable (lines 778-821 in 07-wards.js)
    if (border.length() < 3 || !model) {
        return std::vector<double>(border.length(), wards::Ward::ALLEY / 2);
    }

    std::vector<double> insets;
    insets.reserve(border.length());

    // MFCG inset values (from 07-wards.js lines 799-818):
    // - ARTERY with landing: 2.0
    // - ARTERY without landing: 1.2
    // - STREET: 1.0
    // - WALL: THICKNESS/2 + 1.2 ≈ 2.15
    // - Plaza edge: 1.0
    // - Default: 0.6
    constexpr double INSET_ARTERY = 1.2;  // 2.0 for landing, 1.2 otherwise
    constexpr double INSET_STREET = 1.0;
    constexpr double INSET_WALL = 2.15;   // THICKNESS/2 + 1.2
    constexpr double INSET_DEFAULT = 0.6; // ALLEY / 2

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v0 = border[i];
        const geom::Point& v1 = border[(i + 1) % border.length()];

        double inset = INSET_DEFAULT;

        // Check if edge borders wall
        bool onWall = false;
        if (model->wall) {
            int wallIdx = model->wall->shape.findEdge(v0, v1);
            if (wallIdx == -1) wallIdx = model->wall->shape.findEdge(v1, v0);
            onWall = (wallIdx != -1);
        }
        if (onWall) {
            inset = INSET_WALL;
        }
        // Check citadel
        else if (model->citadel) {
            int citIdx = model->citadel->shape.findEdge(v0, v1);
            if (citIdx == -1) citIdx = model->citadel->shape.findEdge(v1, v0);
            if (citIdx != -1) {
                inset = INSET_WALL;
            }
        }
        // Check if edge is on main artery
        else if (isEdgeOnRoad(v0, v1, model->arteries)) {
            inset = INSET_ARTERY;
        }
        // Check streets and roads
        else if (isEdgeOnRoad(v0, v1, model->streets) || isEdgeOnRoad(v0, v1, model->roads)) {
            inset = INSET_STREET;
        }
        // Default: INSET_DEFAULT (0.6)
        // Note: We don't check for internal edges here because the border is already
        // the circumference of all cells in the group, so it doesn't contain internal edges.

        insets.push_back(inset);
    }

    return insets;
}

// Helper to check if an edge lies on any road in a set
static bool isEdgeOnRoad(const geom::Point& v0, const geom::Point& v1,
                         const std::vector<City::Street>& roads) {
    for (const auto& road : roads) {
        if (road.size() < 2) continue;

        for (size_t i = 0; i < road.size() - 1; ++i) {
            const geom::Point& r0 = *road[i];
            const geom::Point& r1 = *road[i + 1];

            // Check if edge endpoints match road segment endpoints
            if ((v0 == r0 && v1 == r1) || (v0 == r1 && v1 == r0)) {
                return true;
            }

            // Also check if edge lies along the road segment
            geom::Point roadDir = r1.subtract(r0);
            double roadLen = roadDir.length();
            if (roadLen < 0.001) continue;
            roadDir = roadDir.scale(1.0 / roadLen);

            geom::Point edgeDir = v1.subtract(v0);
            double edgeLen = edgeDir.length();
            if (edgeLen < 0.001) continue;
            edgeDir = edgeDir.scale(1.0 / edgeLen);

            // Check if parallel
            double dot = roadDir.x * edgeDir.x + roadDir.y * edgeDir.y;
            if (std::abs(dot) < 0.99) continue;

            // Check if v0 is on the road line
            double dist = std::abs((v0.x - r0.x) * roadDir.y - (v0.y - r0.y) * roadDir.x);
            if (dist < 0.5) {
                return true;
            }
        }
    }
    return false;
}

// WardGroupBuilder implementation

std::vector<std::unique_ptr<WardGroup>> WardGroupBuilder::build() {
    std::vector<std::unique_ptr<WardGroup>> groups;

    // Get all city cells with wards that support grouping
    // Faithful to MFCG: only Alleys and Slum wards are grouped
    std::vector<Cell*> unassigned;
    size_t alleysCount = 0;
    size_t slumCount = 0;
    for (auto* patch : model_->cells) {
        if (patch->withinCity && !patch->waterbody && patch->ward) {
            std::string wardName = patch->ward->getName();
            if (wardName == "Alleys") {
                unassigned.push_back(patch);
                ++alleysCount;
            } else if (wardName == "Slum") {
                unassigned.push_back(patch);
                ++slumCount;
            }
        }
    }
    SDL_Log("WardGroupBuilder: Found %zu Alleys wards, %zu Slum wards to group",
            alleysCount, slumCount);

    // Group cells into WardGroups
    while (!unassigned.empty()) {
        Cell* seed = unassigned.front();
        auto group = std::make_unique<WardGroup>(model_);
        group->addPatch(seed);

        // Remove seed from unassigned
        unassigned.erase(unassigned.begin());

        // Grow the group by adding adjacent cells of the same type
        growGroup(group.get(), unassigned);

        // Build the group border
        group->buildBorder();

        groups.push_back(std::move(group));
    }

    return groups;
}

void WardGroupBuilder::growGroup(WardGroup* group, std::vector<Cell*>& unassigned) {
    if (!group || unassigned.empty()) return;

    std::string typeName = group->getTypeName();
    bool keepGrowing = true;

    while (keepGrowing && !unassigned.empty()) {
        // Find candidates: neighbors of current group cells that are in unassigned
        std::vector<Cell*> candidates;
        for (Cell* patch : group->cells) {
            for (Cell* neighbor : patch->neighbors) {
                // Check if neighbor is in unassigned
                auto it = std::find(unassigned.begin(), unassigned.end(), neighbor);
                if (it != unassigned.end()) {
                    // Check if same ward type
                    if (neighbor->ward && neighbor->ward->getName() == typeName) {
                        // Not already a candidate
                        if (std::find(candidates.begin(), candidates.end(), neighbor) == candidates.end()) {
                            candidates.push_back(neighbor);
                        }
                    }
                }
            }
        }

        if (candidates.empty()) {
            break;
        }

        // Probability to stop growing increases with size (faithful to mfcg.js)
        double stopProb = static_cast<double>(group->cells.size() - 3) / group->cells.size();
        if (stopProb < 0) stopProb = 0;
        if (group->cells.size() > 1 && unassigned.size() > 1 && utils::Random::floatVal() < stopProb) {
            break;
        }

        // Add a random candidate
        size_t idx = static_cast<size_t>(utils::Random::floatVal() * candidates.size());
        if (idx >= candidates.size()) idx = candidates.size() - 1;

        Cell* chosen = candidates[idx];
        group->addPatch(chosen);

        // Remove from unassigned
        auto it = std::find(unassigned.begin(), unassigned.end(), chosen);
        if (it != unassigned.end()) {
            unassigned.erase(it);
        }
    }
}

} // namespace building
} // namespace town_generator
