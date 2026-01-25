#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/EdgeData.h"
#include "town_generator/utils/Random.h"
#include "town_generator/utils/Bisector.h"
#include "town_generator/wards/Ward.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

// Forward declarations
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

    // Build blockM map and triangulation after inner vertices are computed
    buildBlockM();
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
        return;
    }

    createParams();

    // Get available area after street/wall insets
    // Calculate per-edge insets based on what's adjacent (roads, walls, etc.)
    std::vector<double> insets = getAvailable();

    double borderArea = std::abs(border.square());

    // Faithful to MFCG: use shrink() for convex polygons, buffer() for concave
    // Reference: Ward.hx getCityBlock() - patch.shape.isConvex() ? shrink() : buffer()
    geom::Polygon available;
    bool isConvex = border.isConvex();
    if (isConvex) {
        available = border.shrink(insets);
    } else {
        available = border.buffer(insets);
    }
    double availableArea = (available.length() >= 3) ? std::abs(available.square()) : 0.0;

    if (available.length() < 3 || availableArea < alleys.minSq / 4) {
        return;
    }

    // Use Bisector to recursively subdivide into BLOCKS (not individual buildings)
    // Faithful to mfcg.js WardGroup.createAlleys (line 825):
    //   new Bisector(a, b.minSq * b.blockSize, 16 * this.district.alleys.gridChaos)
    //
    // Key insight: Bisector creates BLOCKS with gaps (alleys) between them.
    // Each block is then subdivided into LOTS (individual buildings) via Block.subdivideLots()
    // which uses frontage-based subdivision WITHOUT gaps.
    double bisectorMinArea = alleys.minSq * alleys.blockSize;
    double bisectorVariance = 16.0 * alleys.gridChaos;

    utils::Bisector bisector(available.vertexValues(), bisectorMinArea, bisectorVariance);

    // Set getGap callback - faithful to mfcg.js (lines 13127-13129)
    // Returns constant 1.2 unit gap between building blocks
    bisector.getGap = [](const std::vector<geom::Point>&) -> double {
        return 1.2;
    };

    // Set processCut callback - faithful to mfcg.js (line 13130)
    // Calls semiSmooth to create rounded alley corners
    double minFront = alleys.minFront;
    bisector.processCut = [minFront](const std::vector<geom::Point>& cut) -> std::vector<geom::Point> {
        if (cut.size() != 3) {
            return cut;  // Only process 3-point cuts
        }
        return wards::Ward::semiSmooth(cut[0], cut[1], cut[2], minFront);
    };

    // Set isAtomic callback for non-urban areas - faithful to mfcg.js (line 13131)
    // Non-urban uses isBlockSized which allows larger lots at city fringe
    if (!urban) {
        bisector.isAtomic = [this](const std::vector<geom::Point>& poly) -> bool {
            return this->isBlockSized(poly);
        };
    }

    // Partition into building-sized lots
    auto buildingShapes = bisector.partition();

    // Store cuts as alleys
    alleyPaths = bisector.cuts;

    // Create Block objects from bisector output
    // Faithful to mfcg.js createAlleys (lines 831-839):
    //   for each part from bisector.partition():
    //     if area < threshold: createBlock(part, small=true)
    //     else: createBlock(part)
    //
    // Each Block then subdivides itself into lots via subdivideLots() in constructor
    blocks.clear();
    for (const auto& shape : buildingShapes) {
        if (shape.size() < 3) continue;

        geom::Polygon blockPoly(shape);
        double blockArea = std::abs(blockPoly.square());

        // Skip very small shapes
        if (blockArea < alleys.minSq / 4) continue;

        // Filter out blocks that don't touch the available boundary
        // A block must touch the ward boundary (street edge) to have street access
        bool touchesBoundary = false;
        for (const auto& blockVertex : shape) {
            size_t availLen = available.length();
            for (size_t i = 0; i < availLen; ++i) {
                const geom::Point& e0 = available[i];
                const geom::Point& e1 = available[(i + 1) % availLen];

                double edgeLen = geom::Point::distance(e0, e1);
                if (edgeLen < 0.001) continue;

                double d0 = geom::Point::distance(blockVertex, e0);
                double d1 = geom::Point::distance(blockVertex, e1);
                double dEdge = d0 + d1;

                if (std::abs(dEdge - edgeLen) < 0.1) {
                    touchesBoundary = true;
                    break;
                }
            }
            if (touchesBoundary) break;
        }

        if (!touchesBoundary) {
            continue;
        }

        // Determine if this is a small block (faithful to mfcg.js line 837-838)
        double sizeThreshold = alleys.minSq * std::pow(2.0, alleys.sizeChaos * (2.0 * utils::Random::floatVal() - 1.0));
        bool isSmall = blockArea < sizeThreshold;

        // Create block - Block constructor calls subdivideLots() to create lots
        auto block = std::make_unique<Block>(blockPoly, this);

        // Subdivide block into lots (individual building footprints)
        // This uses frontage-based subdivision WITHOUT gaps between lots
        block->createLots();

        // Filter out inner lots that don't touch block perimeter
        block->filterInner();

        // Create building shapes from lots
        block->createBuildings();

        if (!block->buildings.empty()) {
            blocks.push_back(std::move(block));
        }
    }

    // Filter buildings at city fringe for non-urban (slum) wards
    // Faithful to mfcg.js: this.urban || this.filter()
    if (!urban) {
        filter();
    }
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
    // Compute which border vertices are "inner" (surrounded by city cells)
    // This affects density: inner vertices get blockM=1 (dense), outer get blockM=9 (sparse)
    inner.clear();

    if (border.length() < 3) return;

    // Check if this WardGroup is entirely within walls (true city interior)
    // vs containing slum cells (outside walls but within city)
    bool allWithinWalls = true;
    for (const auto* cell : cells) {
        if (!cell->withinWalls) {
            allWithinWalls = false;
            break;
        }
    }

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v = border[i];

        // For wards INSIDE walls: all vertices are inner (dense buildings throughout)
        // This matches reference behavior where interior city wards are fully dense
        //
        // For slum wards (outside walls): use isInnerVertex to get sparse outer edges
        // Vertices touching non-city cells (farms/wilderness) will be sparse

        bool vertexIsInner;
        if (allWithinWalls) {
            // Interior city ward - all vertices are inner (dense)
            vertexIsInner = true;
        } else {
            // Slum or mixed ward - check if vertex is surrounded by city cells
            vertexIsInner = isInnerVertex(v);
        }

        if (vertexIsInner) {
            inner.push_back(v);
        }
    }

    // A WardGroup is "urban" if all border vertices are inner (fully surrounded by city)
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
    // - CANAL: canalWidth/2 + 1.2
    // - Default: 0.6
    constexpr double INSET_ARTERY_LANDING = 2.0;
    constexpr double INSET_ARTERY = 1.2;
    constexpr double INSET_STREET = 1.0;
    constexpr double INSET_WALL = 2.15;   // THICKNESS/2 + 1.2
    constexpr double INSET_DEFAULT = 0.6; // ALLEY / 2

    // Helper to check if edge borders a landing cell
    auto hasLandingNeighbor = [this](const geom::Point& v0, const geom::Point& v1) -> bool {
        // Find cells that share this edge but are NOT in our group
        for (Cell* cell : cells) {
            for (Cell* neighbor : cell->neighbors) {
                // Skip cells that are in our group
                if (neighbor->group == this) continue;

                // Check if neighbor shares this edge
                if (neighbor->shape.findEdge(v0, v1) != -1 || neighbor->shape.findEdge(v1, v0) != -1) {
                    if (neighbor->landing) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    // Helper to check if edge is "outward facing" (borders non-city, non-water cell)
    // Used by slum wards to prevent buildings on edges facing away from the city
    auto isOutwardFacingEdge = [this](const geom::Point& v0, const geom::Point& v1) -> bool {
        for (Cell* cell : cells) {
            for (Cell* neighbor : cell->neighbors) {
                // Skip cells that are in our group
                if (neighbor->group == this) continue;

                // Check if neighbor shares this edge
                if (neighbor->shape.findEdge(v0, v1) != -1 || neighbor->shape.findEdge(v1, v0) != -1) {
                    // Edge is outward-facing if neighbor is NOT withinCity and NOT waterbody
                    if (!neighbor->withinCity && !neighbor->waterbody) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

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
        // Check canals - use canal width / 2 + 1.2
        else {
            bool onCanal = false;
            for (const auto& canal : model->canals) {
                if (canal->containsEdge(v0, v1)) {
                    inset = canal->width / 2.0 + 1.2;
                    onCanal = true;
                    break;
                }
            }

            if (!onCanal) {
                // Check if edge is on main artery
                if (isEdgeOnRoad(v0, v1, model->arteries)) {
                    // Faithful to mfcg.js: check if adjacent cell is a landing
                    if (hasLandingNeighbor(v0, v1)) {
                        inset = INSET_ARTERY_LANDING;
                    } else {
                        inset = INSET_ARTERY;
                    }
                }
                // Check streets and roads
                else if (isEdgeOnRoad(v0, v1, model->streets) || isEdgeOnRoad(v0, v1, model->roads)) {
                    inset = INSET_STREET;
                }
                // For non-urban (slum) wards: outward-facing edges get large inset
                // This prevents buildings from being placed on edges facing away from city
                else if (!urban && isOutwardFacingEdge(v0, v1)) {
                    inset = 1000.0;  // Large inset = no buildings on this edge
                }
            }
        }

        insets.push_back(inset);
    }

    return insets;
}

double WardGroup::getEdgeDensity(size_t edgeIdx) const {
    // Faithful to mfcg.js filter() edge type densities (lines 13203-13235)
    // ROAD = 0.3, WALL = 0.5, CANAL = 0.1, other = 0
    if (border.length() < 3 || !model || edgeIdx >= border.length()) {
        return 0.0;
    }

    const geom::Point& v0 = border[edgeIdx];
    const geom::Point& v1 = border[(edgeIdx + 1) % border.length()];

    // Check wall
    if (model->wall) {
        int wallIdx = model->wall->shape.findEdge(v0, v1);
        if (wallIdx == -1) wallIdx = model->wall->shape.findEdge(v1, v0);
        if (wallIdx != -1) {
            return 0.5;
        }
    }

    // Check citadel
    if (model->citadel) {
        int citIdx = model->citadel->shape.findEdge(v0, v1);
        if (citIdx == -1) citIdx = model->citadel->shape.findEdge(v1, v0);
        if (citIdx != -1) {
            return 0.5;
        }
    }

    // Check canals
    for (const auto& canal : model->canals) {
        if (canal->containsEdge(v0, v1)) {
            return 0.1;
        }
    }

    // Check roads (arteries and streets)
    if (isEdgeOnRoad(v0, v1, model->arteries) ||
        isEdgeOnRoad(v0, v1, model->streets) ||
        isEdgeOnRoad(v0, v1, model->roads)) {
        return 0.3;
    }

    return 0.0;
}

double WardGroup::interpolateDensity(const geom::Point& p, const std::vector<double>& vertexDensities) const {
    // Simplified interpolation using inverse distance weighting
    // (Full MFCG uses barycentric interpolation within triangulated border)
    if (vertexDensities.empty() || border.length() < 3) {
        return 1.0;
    }

    double totalWeight = 0.0;
    double weightedSum = 0.0;

    for (size_t i = 0; i < border.length(); ++i) {
        double dist = geom::Point::distance(p, border[i]);
        if (dist < 0.001) {
            // Very close to vertex, return its density
            return vertexDensities[i];
        }
        double weight = 1.0 / (dist * dist);  // Inverse distance squared
        weightedSum += weight * vertexDensities[i];
        totalWeight += weight;
    }

    if (totalWeight > 0) {
        return weightedSum / totalWeight;
    }
    return 1.0;
}

void WardGroup::buildBlockM() {
    // Faithful to mfcg.js WardGroup constructor (lines 13021-13024)
    // Build blockM map: inner vertices = 1, fringe vertices = 9
    blockM.clear();

    if (border.length() < 3) return;

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v = border[i];

        // Check if this vertex is in the inner set
        bool isInner = false;
        for (const geom::Point& innerV : inner) {
            if (std::abs(innerV.x - v.x) < 0.001 && std::abs(innerV.y - v.y) < 0.001) {
                isInner = true;
                break;
            }
        }

        // Use integer key for map (multiply by 1000 for precision)
        std::pair<int, int> key{static_cast<int>(v.x * 1000), static_cast<int>(v.y * 1000)};
        blockM[key] = isInner ? 1.0 : 9.0;
    }

    // Build triangulation using ear clipping
    triangulation.clear();
    if (border.length() < 3) return;

    // Simple ear clipping triangulation
    std::vector<size_t> indices;
    for (size_t i = 0; i < border.length(); ++i) {
        indices.push_back(i);
    }

    while (indices.size() > 3) {
        bool earFound = false;

        for (size_t i = 0; i < indices.size(); ++i) {
            size_t prev = (i + indices.size() - 1) % indices.size();
            size_t next = (i + 1) % indices.size();

            const geom::Point& p0 = border[indices[prev]];
            const geom::Point& p1 = border[indices[i]];
            const geom::Point& p2 = border[indices[next]];

            // Check if this is a convex vertex (ear candidate)
            double cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
            if (cross <= 0) continue;  // Not convex (assuming CCW winding)

            // Check if any other vertex is inside this triangle
            bool isEar = true;
            for (size_t j = 0; j < indices.size(); ++j) {
                if (j == prev || j == i || j == next) continue;

                const geom::Point& p = border[indices[j]];

                // Point-in-triangle test using barycentric coordinates
                double denom = (p1.y - p2.y) * (p0.x - p2.x) + (p2.x - p1.x) * (p0.y - p2.y);
                if (std::abs(denom) < 1e-10) continue;

                double b0 = ((p1.y - p2.y) * (p.x - p2.x) + (p2.x - p1.x) * (p.y - p2.y)) / denom;
                double b1 = ((p2.y - p0.y) * (p.x - p2.x) + (p0.x - p2.x) * (p.y - p2.y)) / denom;
                double b2 = 1.0 - b0 - b1;

                if (b0 >= 0 && b1 >= 0 && b2 >= 0) {
                    isEar = false;
                    break;
                }
            }

            if (isEar) {
                // Add triangle
                triangulation.push_back({indices[prev], indices[i], indices[next]});

                // Remove the ear vertex
                indices.erase(indices.begin() + static_cast<long>(i));
                earFound = true;
                break;
            }
        }

        if (!earFound) {
            // No ear found - polygon may be degenerate
            break;
        }
    }

    // Add the final triangle
    if (indices.size() == 3) {
        triangulation.push_back({indices[0], indices[1], indices[2]});
    }
}

double WardGroup::interpolate(const geom::Point& p, const std::map<std::pair<int, int>, double>& values) const {
    // Faithful to mfcg.js WardGroup.interpolate (lines 13271-13278)
    // Uses barycentric interpolation within triangulated border

    for (const auto& tri : triangulation) {
        const geom::Point& a = border[tri[0]];
        const geom::Point& b = border[tri[1]];
        const geom::Point& c = border[tri[2]];

        // Compute barycentric coordinates
        double denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(denom) < 1e-10) continue;

        double baryA = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / denom;
        double baryB = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / denom;
        double baryC = 1.0 - baryA - baryB;

        // Check if point is inside this triangle
        if (baryA >= -0.001 && baryB >= -0.001 && baryC >= -0.001) {
            // Get values for each vertex
            std::pair<int, int> keyA{static_cast<int>(a.x * 1000), static_cast<int>(a.y * 1000)};
            std::pair<int, int> keyB{static_cast<int>(b.x * 1000), static_cast<int>(b.y * 1000)};
            std::pair<int, int> keyC{static_cast<int>(c.x * 1000), static_cast<int>(c.y * 1000)};

            double valA = 0.0, valB = 0.0, valC = 0.0;
            auto itA = values.find(keyA);
            auto itB = values.find(keyB);
            auto itC = values.find(keyC);
            if (itA != values.end()) valA = itA->second;
            if (itB != values.end()) valB = itB->second;
            if (itC != values.end()) valC = itC->second;

            return baryA * valA + baryB * valB + baryC * valC;
        }
    }

    return std::numeric_limits<double>::quiet_NaN();
}

bool WardGroup::isBlockSized(const std::vector<geom::Point>& poly) const {
    // Faithful to mfcg.js WardGroup.isBlockSized (lines 13287-13292)
    // Returns true if polygon is small enough to stop subdivision
    // Uses blockM interpolation to allow larger lots at city fringe

    if (poly.size() < 3) return true;

    // Calculate area
    double area = 0.0;
    size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const geom::Point& p1 = poly[i];
        const geom::Point& p2 = poly[(i + 1) % n];
        area += (p1.x * p2.y - p2.x * p1.y);
    }
    area = std::abs(area) / 2.0;

    // Calculate centroid
    double cx = 0.0, cy = 0.0;
    for (const auto& p : poly) {
        cx += p.x;
        cy += p.y;
    }
    cx /= static_cast<double>(n);
    cy /= static_cast<double>(n);
    geom::Point center(cx, cy);

    // Interpolate blockM value at center
    double blockMultiplier = interpolate(center, blockM);
    if (std::isnan(blockMultiplier)) {
        blockMultiplier = 1.0;  // Default to 1 if interpolation fails
    }

    // Calculate threshold: minSq * blockSize * blockMultiplier
    double threshold = alleys.minSq * alleys.blockSize * blockMultiplier;

    return area < threshold;
}

void WardGroup::filter() {
    // Faithful to mfcg.js WardGroup.filter (lines 13190-13259)
    // Filters buildings at city fringe based on edge-type density

    if (border.length() < 3 || blocks.empty()) {
        return;
    }

    // Step 1: Compute density per vertex (mfcg.js lines 13191-13240)
    std::vector<double> vertexDensities;
    vertexDensities.reserve(border.length());

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v = border[i];

        // Check if vertex is "inner" (all adjacent patches are withinCity or waterbody)
        bool isInner = false;
        for (const geom::Point& innerV : inner) {
            if (innerV == v) {
                isInner = true;
                break;
            }
        }

        if (isInner) {
            vertexDensities.push_back(1.0);
        } else {
            // Get density from adjacent edges (previous and current)
            size_t prevEdge = (i + border.length() - 1) % border.length();
            double prevDensity = getEdgeDensity(prevEdge);
            double currDensity = getEdgeDensity(i);
            vertexDensities.push_back(std::max(prevDensity, currDensity));
        }
    }

    // Step 2: Calculate threshold parameters (mfcg.js lines 13241-13243)
    // f = sqrt(faces.length), k = 0.5 * f - 0.5
    double f = std::sqrt(static_cast<double>(cells.size()));
    double k = 0.5 * f - 0.5;

    // Step 3: Filter lots in each block based on density threshold
    for (auto& block : blocks) {
        std::vector<geom::Polygon> filteredBuildings;

        for (const auto& building : block->buildings) {
            // Get building center
            geom::Point center = building.centroid();

            // Interpolate density at center
            double density = interpolateDensity(center, vertexDensities);

            if (std::isnan(density)) {
                continue;  // Skip if density couldn't be calculated
            }

            // Calculate probability threshold (mfcg.js line 13248)
            // n = density * f - k, then random() < n to keep
            double threshold = density * f - k;

            // Keep building if random value is below threshold
            if (utils::Random::floatVal() < threshold) {
                filteredBuildings.push_back(building);
            }
        }

        block->buildings = std::move(filteredBuildings);
    }

    // Step 4: Remove empty blocks (mfcg.js lines 13251-13258)
    std::vector<std::unique_ptr<Block>> filteredBlocks;
    for (auto& block : blocks) {
        if (!block->buildings.empty()) {
            filteredBlocks.push_back(std::move(block));
        }
    }
    blocks = std::move(filteredBlocks);
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

    // Get all cells with Alleys wards that support grouping
    // Faithful to MFCG: Alleys wards (both urban and slum) are grouped into WardGroups
    // Slum towns use Alleys wards placed outside city walls (withinCity=false)
    std::vector<Cell*> unassigned;
    size_t alleysCount = 0;
    size_t slumCount = 0;
    for (auto* patch : model_->cells) {
        if (!patch->waterbody && patch->ward) {
            std::string wardName = patch->ward->getName();
            if (wardName == "Alleys") {
                unassigned.push_back(patch);
                ++alleysCount;
                if (patch->withinCity && !patch->withinWalls) {
                    ++slumCount;
                }
            }
        }
    }
    SDL_Log("WardGroupBuilder: Found %zu Alleys wards to group (%zu urban, %zu slum)",
            alleysCount, alleysCount - slumCount, slumCount);

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

// Helper to check if two cells share an edge without a road/wall/canal between them
// Faithful to mfcg.js pickFaces: null == n.data check (line 11376)
static bool cellsShareInternalEdge(Cell* cell1, Cell* cell2) {
    // Find the shared edge between the two cells
    size_t len1 = cell1->shape.length();
    for (size_t i = 0; i < len1; ++i) {
        const geom::Point& a = cell1->shape[i];
        const geom::Point& b = cell1->shape[(i + 1) % len1];

        // Check if cell2 has this edge
        int edgeIdx2 = cell2->findEdgeIndex(a, b);
        if (edgeIdx2 >= 0) {
            // Found shared edge - check if it has road/wall/canal data
            EdgeType type1 = cell1->getEdgeType(i);
            EdgeType type2 = cell2->getEdgeType(static_cast<size_t>(edgeIdx2));

            // Only allow grouping if BOTH cells see this edge as NONE (no road/wall/canal)
            // mfcg.js: null == n.data means no road/wall/canal between cells
            if (type1 == EdgeType::NONE && type2 == EdgeType::NONE) {
                return true;  // Internal edge - can group
            }
            // Edge has road/wall/canal - cannot group across it
            return false;
        }
    }
    return false;  // No shared edge found
}

void WardGroupBuilder::growGroup(WardGroup* group, std::vector<Cell*>& unassigned) {
    if (!group || unassigned.empty()) return;

    std::string typeName = group->getTypeName();
    bool keepGrowing = true;

    while (keepGrowing && !unassigned.empty()) {
        // Find candidates: neighbors of current group cells that are in unassigned
        // Faithful to mfcg.js pickFaces (lines 11364-11387)
        std::vector<Cell*> candidates;
        for (Cell* patch : group->cells) {
            for (Cell* neighbor : patch->neighbors) {
                // Check if neighbor is in unassigned
                auto it = std::find(unassigned.begin(), unassigned.end(), neighbor);
                if (it != unassigned.end()) {
                    // Check if same ward type
                    if (neighbor->ward && neighbor->ward->getName() == typeName) {
                        // CRITICAL: Check that shared edge has no road/wall/canal
                        // Faithful to mfcg.js: null == n.data (line 11376)
                        if (cellsShareInternalEdge(patch, neighbor)) {
                            // Not already a candidate
                            if (std::find(candidates.begin(), candidates.end(), neighbor) == candidates.end()) {
                                candidates.push_back(neighbor);
                            }
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
