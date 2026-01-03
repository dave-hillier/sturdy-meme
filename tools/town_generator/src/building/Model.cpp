#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Topology.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/wards/Castle.h"
#include "town_generator/wards/Cathedral.h"
#include "town_generator/wards/Market.h"
#include "town_generator/wards/CraftsmenWard.h"
#include "town_generator/wards/MerchantWard.h"
#include "town_generator/wards/PatriciateWard.h"
#include "town_generator/wards/CommonWard.h"
#include "town_generator/wards/AdministrationWard.h"
#include "town_generator/wards/MilitaryWard.h"
#include "town_generator/wards/GateWard.h"
#include "town_generator/wards/Slum.h"
#include "town_generator/wards/Farm.h"
#include "town_generator/wards/Park.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace town_generator {
namespace building {

Model::Model(int nPatches, int seed) : nPatches_(nPatches) {
    utils::Random::reset(seed);

    // Random city features based on original algorithm
    plazaNeeded = utils::Random::boolVal(0.8);
    citadelNeeded = utils::Random::boolVal(0.5);
    wallsNeeded = nPatches > 15;
}

Model::~Model() {
    delete citadel;
    delete wall;
}

void Model::build() {
    buildPatches();
    optimizeJunctions();
    buildWalls();
    buildStreets();
    buildRoads();
    createWards();
    buildGeometry();
}

std::vector<geom::Point> Model::generateRandomPoints(int count, double width, double height) {
    std::vector<geom::Point> points;
    points.reserve(count);

    for (int i = 0; i < count; ++i) {
        points.emplace_back(
            utils::Random::floatVal() * width,
            utils::Random::floatVal() * height
        );
    }

    return points;
}

void Model::buildPatches() {
    // Generate seed points using spiral algorithm (faithful to original)
    // sa = random starting angle
    // for each point: a = sa + sqrt(i) * 5, r = (i == 0) ? 0 : 10 + i * (2 + random())

    double sa = utils::Random::floatVal() * M_PI * 2;  // Starting angle
    std::vector<geom::Point> seeds;
    seeds.reserve(nPatches_ + 3);  // Extra for frame

    for (int i = 0; i < nPatches_; ++i) {
        double a = sa + std::sqrt(static_cast<double>(i)) * 5.0;
        double r = (i == 0) ? 0.0 : 10.0 + i * (2.0 + utils::Random::floatVal());
        seeds.emplace_back(std::cos(a) * r, std::sin(a) * r);
    }

    // Calculate bounds for Voronoi
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& p : seeds) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    double width = maxX - minX + 40;
    double height = maxY - minY + 40;
    double offsetX = -minX + 20;
    double offsetY = -minY + 20;

    // Offset seeds to positive coordinates
    for (auto& p : seeds) {
        p.x += offsetX;
        p.y += offsetY;
    }

    // Apply Lloyd relaxation to inner patches (3 iterations, faithful to original)
    // Only relax the first ~60% of patches (inner city)
    int innerCount = static_cast<int>(nPatches_ * 0.6);
    std::vector<geom::Point> innerSeeds(seeds.begin(), seeds.begin() + std::min(innerCount, static_cast<int>(seeds.size())));

    for (int iteration = 0; iteration < 3; ++iteration) {
        innerSeeds = geom::Voronoi::relax(innerSeeds, width, height);
    }

    // Replace inner seeds with relaxed versions
    for (int i = 0; i < static_cast<int>(innerSeeds.size()) && i < static_cast<int>(seeds.size()); ++i) {
        seeds[i] = innerSeeds[i];
    }

    // Build Voronoi diagram
    geom::Voronoi voronoi(width, height);
    for (const auto& seed : seeds) {
        voronoi.addPoint(seed);
    }

    // Convert regions to patches
    auto regions = voronoi.partitioning();

    // Create patches from regions, sorted by distance from center
    geom::Point center(width / 2, height / 2);
    std::vector<std::pair<double, geom::Region*>> sortedRegions;

    for (auto* region : regions) {
        double dist = geom::Point::distance(region->seed, center);
        sortedRegions.push_back({dist, region});
    }

    std::sort(sortedRegions.begin(), sortedRegions.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build a map from Triangle* to shared PointPtr (circumcenter)
    // This ensures adjacent patches share the exact same Point object
    // so mutations propagate automatically (matching Haxe reference semantics)
    std::map<geom::Triangle*, geom::PointPtr> triangleToVertex;
    for (const auto& tr : voronoi.triangles) {
        triangleToVertex[tr.get()] = geom::makePoint(tr->c);
    }

    // Create patches using shared vertex pointers
    std::map<geom::Region*, Patch*> regionToPatch;
    int patchesCreated = 0;

    for (size_t i = 0; i < sortedRegions.size() && patchesCreated < nPatches_; ++i) {
        auto* region = sortedRegions[i].second;

        // Skip regions with no triangles (boundary regions)
        if (region->vertices.empty()) continue;

        // Create polygon from region's triangles using shared vertex pointers
        std::vector<geom::PointPtr> sharedVertices;
        for (auto* tr : region->vertices) {
            auto it = triangleToVertex.find(tr);
            if (it != triangleToVertex.end()) {
                sharedVertices.push_back(it->second);
            }
        }

        // Skip if we couldn't create a valid polygon
        if (sharedVertices.size() < 3) continue;

        auto patch = std::make_unique<Patch>(geom::Polygon(sharedVertices));
        patchesCreated++;

        // Determine if within city (inner patches)
        double distFromCenter = sortedRegions[i].first;
        double cityRadius = (nPatches_ > 15) ?
            sortedRegions[std::min(static_cast<size_t>(innerCount), sortedRegions.size() - 1)].first * 1.1 :
            sortedRegions.back().first * 1.1;

        patch->withinCity = (distFromCenter < cityRadius);
        patch->withinWalls = patch->withinCity && wallsNeeded;

        regionToPatch[region] = patch.get();
        patches.push_back(patch.get());
        ownedPatches_.push_back(std::move(patch));
    }

    // Establish neighbor relationships
    for (auto* region : regions) {
        Patch* patch = regionToPatch[region];
        if (!patch) continue;

        auto neighborRegions = region->neighbors(voronoi.regions);
        for (auto* neighborRegion : neighborRegions) {
            Patch* neighborPatch = regionToPatch[neighborRegion];
            if (neighborPatch && neighborPatch != patch) {
                // Check if not already in neighbors
                if (std::find(patch->neighbors.begin(), patch->neighbors.end(), neighborPatch) == patch->neighbors.end()) {
                    patch->neighbors.push_back(neighborPatch);
                }
            }
        }
    }

    // Create border patch (the bounding area)
    border.shape = geom::Polygon::rect(width, height);

    // Mark special patches
    // First patch (closest to center) can be plaza
    // A patch near the edge can be citadel location
    if (!patches.empty() && citadelNeeded) {
        // Find a patch that's within city but near the edge
        for (int i = static_cast<int>(patches.size()) - 1; i >= 0; --i) {
            if (patches[i]->withinCity) {
                // This will be marked for citadel during wall building
                break;
            }
        }
    }
}

void Model::optimizeJunctions() {
    // Merge vertices that are too close together (< 8 units)
    // With shared_ptr<Point>, mutations automatically propagate to all
    // patches sharing the same vertex (matching Haxe reference semantics)

    std::vector<Patch*> patchesToOptimize = inner;
    std::set<Patch*> patchesToClean;

    for (auto* patch : patchesToOptimize) {
        size_t index = 0;
        while (index < patch->shape.length()) {
            // Get shared pointers to adjacent vertices
            geom::PointPtr v0Ptr = patch->shape.ptr(index);
            geom::PointPtr v1Ptr = patch->shape.ptr((index + 1) % patch->shape.length());

            if (v0Ptr != v1Ptr && geom::Point::distance(*v0Ptr, *v1Ptr) < 8.0) {
                // Move v0 to midpoint (mutates the shared point!)
                // All patches sharing this vertex see the change automatically
                v0Ptr->addEq(*v1Ptr);
                v0Ptr->scaleEq(0.5);

                // Replace v1 references with v0 in all patches
                for (auto* otherPatch : patches) {
                    if (otherPatch == patch) continue;

                    // Find v1 by pointer identity and replace with v0
                    int v1Index = otherPatch->shape.indexOfPtr(v1Ptr);
                    if (v1Index != -1) {
                        otherPatch->shape.vertices()[v1Index] = v0Ptr;
                        patchesToClean.insert(otherPatch);
                    }
                }

                // Remove v1 from current patch
                patch->shape.removePtr(v1Ptr);
                patchesToClean.insert(patch);

                // Don't increment index since we removed an element
            } else {
                index++;
            }
        }
    }

    // Remove duplicate vertices (by pointer identity) from affected patches
    for (auto* patch : patchesToClean) {
        std::vector<geom::PointPtr> cleaned;
        for (const auto& vPtr : patch->shape) {
            bool isDuplicate = std::find(cleaned.begin(), cleaned.end(), vPtr) != cleaned.end();
            if (!isDuplicate) {
                cleaned.push_back(vPtr);
            }
        }
        patch->shape = geom::Polygon(cleaned);
    }
}

void Model::buildWalls() {
    // Separate inner and outer patches based on withinWalls flag
    inner.clear();
    std::vector<Patch*> outer;

    if (!wallsNeeded) {
        // No walls needed - all patches are inner
        inner = patches;
        for (auto* p : patches) {
            p->withinCity = true;
            p->withinWalls = true;  // For border calculation
        }
    } else {
        for (auto* patch : patches) {
            if (patch->withinWalls) {
                inner.push_back(patch);
            } else {
                outer.push_back(patch);
            }
        }

        if (inner.empty()) {
            inner = patches;
            for (auto* p : patches) {
                p->withinWalls = true;
            }
        }
    }

    // Find citadel patch (innermost patch suitable for fortification)
    Patch* citadelPatch = nullptr;
    std::vector<Patch*> citadelPatches;
    std::vector<geom::Point> reservedPoints;  // Points that shouldn't be modified

    if (citadelNeeded && wallsNeeded && !inner.empty()) {
        // Use the first inner patch (closest to center) for citadel
        citadelPatch = inner[0];
        citadelPatches.push_back(citadelPatch);
        // Reserve all vertices of the citadel
        for (size_t i = 0; i < citadelPatch->shape.length(); ++i) {
            reservedPoints.push_back(citadelPatch->shape[i]);
        }
        citadel = new CurtainWall(false, this, citadelPatches, {});
    }

    // ALWAYS create a border CurtainWall - this provides gates even for unwalled cities
    // The wallsNeeded flag controls whether it's a real wall with towers
    auto* borderWall = new CurtainWall(wallsNeeded, this, inner, reservedPoints);

    // Set wall reference only if walls are needed
    if (wallsNeeded) {
        wall = borderWall;
        wall->buildTowers();
    }

    // Collect gates from border (always, even if unwalled)
    for (const auto& gate : borderWall->gates) {
        gates.push_back(gate);
    }
    if (citadel) {
        for (const auto& gate : citadel->gates) {
            gates.push_back(gate);
        }
    }
}

void Model::buildStreets() {
    if (inner.empty()) return;

    // Create topology for pathfinding
    topology_ = std::make_unique<Topology>(this);

    // Find plaza (central patch for street destinations)
    if (!plaza && !inner.empty()) {
        // Use innermost patch as plaza destination
        plaza = inner[0];
    }

    if (!plaza) return;

    geom::Point plazaCenter = plaza->shape.centroid();

    // Build arteries from each gate to plaza
    for (const auto& gate : gates) {
        auto path = topology_->buildPath(gate, plazaCenter);
        if (!path.empty()) {
            // Smooth the path
            if (path.size() > 2) {
                std::vector<geom::Point> smoothed;
                smoothed.push_back(path[0]);
                for (size_t i = 1; i < path.size() - 1; ++i) {
                    // Simple smoothing: average with neighbors
                    geom::Point avg;
                    avg.x = (path[i-1].x + path[i].x + path[i+1].x) / 3.0;
                    avg.y = (path[i-1].y + path[i].y + path[i+1].y) / 3.0;
                    smoothed.push_back(avg);
                }
                smoothed.push_back(path.back());
                path = smoothed;
            }
            arteries.push_back(path);
        }
    }

    // Build secondary streets between patches
    for (auto* patch : inner) {
        if (patch == plaza) continue;

        geom::Point patchCenter = patch->shape.centroid();

        // Try to connect to nearest artery or plaza
        bool connected = false;

        // Check if already near an artery
        for (const auto& artery : arteries) {
            for (const auto& pt : artery) {
                if (geom::Point::distance(patchCenter, pt) < 5.0) {
                    connected = true;
                    break;
                }
            }
            if (connected) break;
        }

        if (!connected) {
            // Build street to plaza
            auto path = topology_->buildPath(patchCenter, plazaCenter);
            if (!path.empty() && path.size() > 1) {
                streets.push_back(path);
            }
        }
    }
}

void Model::buildRoads() {
    // Build roads from gates outward
    if (!topology_ || gates.empty()) return;

    for (const auto& gate : gates) {
        // Find outer node nearest to gate in outward direction
        geom::Point center = plaza ? plaza->shape.centroid() : geom::Point(0, 0);
        geom::Point dir = gate.subtract(center).norm();

        // Find best outer node
        geom::Node* bestOuter = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();

        for (auto* node : topology_->outer) {
            auto it = topology_->node2pt.find(node);
            if (it == topology_->node2pt.end()) continue;

            geom::Point nodePos = it->second;
            geom::Point toNode = nodePos.subtract(gate);
            double dist = toNode.length();
            if (dist < 0.01) continue;

            // Score by alignment with outward direction
            double alignment = toNode.dot(dir) / dist;
            if (alignment > bestScore) {
                bestScore = alignment;
                bestOuter = node;
            }
        }

        if (bestOuter) {
            geom::Point roadEnd = topology_->node2pt[bestOuter];
            auto path = topology_->buildPath(gate, roadEnd, &topology_->inner);
            if (!path.empty()) {
                roads.push_back(path);
            }
        }
    }
}

std::vector<Patch*> Model::patchByVertex(const geom::Point& v) {
    std::vector<Patch*> result;
    for (auto* p : patches) {
        if (p->shape.contains(v)) {
            result.push_back(p);
        }
    }
    return result;
}

geom::Polygon Model::findCircumference(const std::vector<Patch*>& patchList) {
    if (patchList.empty()) return geom::Polygon();
    if (patchList.size() == 1) return patchList[0]->shape;

    // Find all edges that belong to exactly one patch in the set
    // An edge is "internal" if BOTH vertices are shared with another patch in the set
    std::vector<std::pair<geom::Point, geom::Point>> boundaryEdges;

    for (auto* patch : patchList) {
        patch->shape.forEdge([&patchList, patch, &boundaryEdges](
            const geom::Point& v0, const geom::Point& v1
        ) {
            // Check if this edge is shared with another patch in the set
            // Edge is shared if another patch contains both v0 and v1
            bool isShared = false;
            for (auto* other : patchList) {
                if (other == patch) continue;
                if (other->shape.contains(v0) && other->shape.contains(v1)) {
                    isShared = true;
                    break;
                }
            }

            if (!isShared) {
                boundaryEdges.push_back({v0, v1});
            }
        });
    }

    if (boundaryEdges.empty()) return geom::Polygon();

    // Chain edges together
    geom::Polygon result;
    result.push(boundaryEdges[0].first);

    geom::Point current = boundaryEdges[0].second;
    boundaryEdges.erase(boundaryEdges.begin());

    int maxIter = static_cast<int>(boundaryEdges.size()) + 10;
    int iter = 0;
    while (!boundaryEdges.empty() && iter++ < maxIter) {
        result.push(current);

        bool found = false;
        for (auto it = boundaryEdges.begin(); it != boundaryEdges.end(); ++it) {
            if (it->first == current) {
                current = it->second;
                boundaryEdges.erase(it);
                found = true;
                break;
            }
        }

        if (!found) {
            // Try to find any edge to continue
            if (!boundaryEdges.empty()) {
                current = boundaryEdges[0].second;
                result.push(boundaryEdges[0].first);
                boundaryEdges.erase(boundaryEdges.begin());
            } else {
                break;
            }
        }
    }

    return result;
}

void Model::createWards() {
    // Ward type distribution (faithful to original weights)
    std::vector<std::function<wards::Ward*()>> wardTypes = {
        []() { return new wards::CraftsmenWard(); },
        []() { return new wards::MerchantWard(); },
        []() { return new wards::CommonWard(); },
        []() { return new wards::Slum(); },
        []() { return new wards::PatriciateWard(); },
        []() { return new wards::AdministrationWard(); },
        []() { return new wards::MilitaryWard(); },
    };

    std::vector<double> weights = {3, 2, 4, 2, 1, 1, 1};

    // Track special ward assignments
    bool castleAssigned = false;
    bool marketAssigned = false;

    // Assign wards to patches
    for (size_t idx = 0; idx < patches.size(); ++idx) {
        auto* patch = patches[idx];
        wards::Ward* ward = nullptr;

        // Special wards
        if (patch->withinCity && !patch->ward) {
            // Castle goes in citadel patch (first inner patch if citadel needed)
            if (citadelNeeded && !castleAssigned && idx == 0) {
                ward = new wards::Castle();
                castleAssigned = true;
            }
            // Market/plaza goes in central area
            else if (plazaNeeded && !marketAssigned && patch->withinWalls && idx < 3) {
                plaza = patch;
                ward = new wards::Market();
                marketAssigned = true;
            }
            // Cathedral with small probability
            else if (utils::Random::boolVal(0.1) && patch->withinWalls) {
                ward = new wards::Cathedral();
            }
            // Gate wards near gates
            else if (!gates.empty()) {
                for (const auto& gate : gates) {
                    if (patch->shape.contains(gate) ||
                        geom::Point::distance(patch->shape.centroid(), gate) < 10.0) {
                        ward = new wards::GateWard();
                        break;
                    }
                }
            }
        }

        // Regular wards
        if (!ward) {
            if (patch->withinCity) {
                // Weighted random selection
                double total = 0;
                for (double w : weights) total += w;

                double r = utils::Random::floatVal() * total;
                double acc = 0;
                for (size_t i = 0; i < wardTypes.size(); ++i) {
                    acc += weights[i];
                    if (r <= acc) {
                        ward = wardTypes[i]();
                        break;
                    }
                }

                if (!ward) ward = new wards::CommonWard();
            } else {
                // Outer patches are farms or parks
                if (utils::Random::boolVal(0.7)) {
                    ward = new wards::Farm();
                } else {
                    ward = new wards::Park();
                }
            }
        }

        if (ward) {
            ward->patch = patch;
            ward->model = this;
            patch->ward = ward;
            wards_.emplace_back(ward);
        }
    }
}

void Model::buildGeometry() {
    for (const auto& ward : wards_) {
        ward->createGeometry();
    }
}

} // namespace building
} // namespace town_generator
