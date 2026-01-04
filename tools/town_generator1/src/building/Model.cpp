#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Topology.h"
#include <iostream>
#include <SDL3/SDL_log.h>
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
    if (wall != border) {
        delete wall;
    }
    delete border;
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
    // Generate nPatches * 8 points (faithful to Haxe) - inner patches plus outer
    // sa = random starting angle
    // for each point: a = sa + sqrt(i) * 5, r = (i == 0) ? 0 : 10 + i * (2 + random())

    double sa = utils::Random::floatVal() * M_PI * 2;  // Starting angle
    std::vector<geom::Point> seeds;
    int totalPoints = nPatches_ * 8;  // 8x more points for outer regions (faithful to Haxe)
    seeds.reserve(totalPoints);

    for (int i = 0; i < totalPoints; ++i) {
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
    geom::Voronoi voronoi(0, 0, width, height);
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

    // Create all patches from Voronoi regions
    // First nPatches_ are considered inner/withinCity, rest are outer (faithful to Haxe)
    for (size_t i = 0; i < sortedRegions.size(); ++i) {
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

        // First nPatches_ are inner, rest are outer (faithful to Haxe)
        bool isInner = (patchesCreated < nPatches_);
        patchesCreated++;

        patch->withinCity = isInner;
        patch->withinWalls = isInner && wallsNeeded;

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
    // Polygon::rect creates centered at origin, so offset to match content
    borderPatch.shape = geom::Polygon::rect(width, height);
    borderPatch.shape.offset(geom::Point(width / 2, height / 2));

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
    std::vector<geom::PointPtr> reservedPoints;  // Points that shouldn't be modified (by pointer identity)

    if (citadelNeeded && wallsNeeded && !inner.empty()) {
        // Use the first inner patch (closest to center) for citadel
        citadelPatch = inner[0];
        citadelPatches.push_back(citadelPatch);
        // Reserve all vertices of the citadel (as PointPtr for identity comparison)
        for (size_t i = 0; i < citadelPatch->shape.length(); ++i) {
            reservedPoints.push_back(citadelPatch->shape.ptr(i));
        }
        citadel = new CurtainWall(false, this, citadelPatches, {});
    }

    // ALWAYS create a border CurtainWall - this provides gates even for unwalled cities
    // The wallsNeeded flag controls whether it's a real wall with towers
    border = new CurtainWall(wallsNeeded, this, inner, reservedPoints);

    // Set wall reference only if walls are needed (wall == border for walled cities)
    if (wallsNeeded) {
        wall = border;
        wall->buildTowers();
    }

    // Collect gates from border (always, even if unwalled) - these are already PointPtr
    for (const auto& gatePtr : border->gates) {
        gates.push_back(gatePtr);
    }
    if (citadel) {
        for (const auto& gatePtr : citadel->gates) {
            gates.push_back(gatePtr);
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

    auto bounds = borderPatch.shape.getBounds();
    geom::Point center((bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2);

    // Build arteries from each gate to plaza
    for (const auto& gatePtr : gates) {
        // Find the vertex of the plaza closest to this gate (faithful to Haxe)
        // This ensures we path to an actual node in the topology graph
        geom::Point end = center;
        if (plaza) {
            double minDist = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < plaza->shape.length(); ++i) {
                double d = geom::Point::distance(*plaza->shape.ptr(i), *gatePtr);
                if (d < minDist) {
                    minDist = d;
                    end = *plaza->shape.ptr(i);
                }
            }
        }
        // Exclude outer nodes so path stays inside the city (faithful to Haxe)
        auto path = topology_->buildPath(*gatePtr, end, &topology_->outer);
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

        // Use the patch vertex closest to the center as the start point
        // (this ensures we path from an actual node in the topology graph)
        geom::Point patchStart = patch->shape.length() > 0 ? *patch->shape.ptr(0) : patch->shape.centroid();
        double minDist = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            double d = geom::Point::distance(*patch->shape.ptr(i), center);
            if (d < minDist) {
                minDist = d;
                patchStart = *patch->shape.ptr(i);
            }
        }

        // Try to connect to nearest artery or plaza
        bool connected = false;

        // Check if already near an artery
        for (const auto& artery : arteries) {
            for (const auto& pt : artery) {
                if (geom::Point::distance(patchStart, pt) < 5.0) {
                    connected = true;
                    break;
                }
            }
            if (connected) break;
        }

        if (!connected) {
            // Build street to plaza - find nearest plaza vertex
            geom::Point plazaEnd = center;
            if (plaza) {
                double minD = std::numeric_limits<double>::infinity();
                for (size_t i = 0; i < plaza->shape.length(); ++i) {
                    double d = geom::Point::distance(*plaza->shape.ptr(i), patchStart);
                    if (d < minD) {
                        minD = d;
                        plazaEnd = *plaza->shape.ptr(i);
                    }
                }
            }
            // Exclude outer nodes so path stays inside the city
            auto path = topology_->buildPath(patchStart, plazaEnd, &topology_->outer);
            if (!path.empty() && path.size() > 1) {
                streets.push_back(path);
            }
        }
    }

}

void Model::buildRoads() {
    // Build roads from outer edge TO border gates (not citadel gates)
    // Roads stay OUTSIDE the city walls (exclude inner nodes)
    // Faithful to Haxe: only border.gates get roads, not citadel gates
    if (!topology_ || !border) return;

    for (const auto& gatePtr : border->gates) {
        const geom::Point& gate = *gatePtr;

        // Haxe: dir = gate.norm(1000) - point 1000 units from origin in gate direction
        geom::Point dir = gate.norm(1000);

        // Find the topology node closest to that distant point
        geom::PointPtr startPtr = nullptr;
        double minDist = std::numeric_limits<double>::infinity();

        for (const auto& [ptPtr, node] : topology_->pt2node) {
            double d = geom::Point::distance(*ptPtr, dir);
            if (d < minDist) {
                minDist = d;
                startPtr = ptPtr;
            }
        }

        if (startPtr) {
            // Build path from outer start point TO gate, excluding inner nodes
            auto path = topology_->buildPath(*startPtr, gate, &topology_->inner);
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

std::vector<Patch*> Model::patchByVertexPtr(const geom::PointPtr& v) {
    std::vector<Patch*> result;
    for (auto* p : patches) {
        if (p->shape.containsPtr(v)) {
            result.push_back(p);
        }
    }
    return result;
}

geom::Polygon Model::findCircumference(const std::vector<Patch*>& patchList) {
    if (patchList.empty()) return geom::Polygon();
    if (patchList.size() == 1) return patchList[0]->shape.copy();

    // Find all edges that belong to exactly one patch in the set
    // Store edges as pairs of shared pointers to preserve vertex identity
    // Edge format: (start_ptr, end_ptr)
    std::vector<std::pair<geom::PointPtr, geom::PointPtr>> boundaryEdges;

    for (auto* patch : patchList) {
        size_t len = patch->shape.length();
        for (size_t i = 0; i < len; ++i) {
            geom::PointPtr v0Ptr = patch->shape.ptr(i);
            geom::PointPtr v1Ptr = patch->shape.ptr((i + 1) % len);

            // Check if this edge is shared with another patch in the set
            // Edge is shared if another patch has the reverse edge (v1 -> v0)
            bool isShared = false;
            for (auto* other : patchList) {
                if (other == patch) continue;
                // Look for reverse edge by pointer identity
                if (other->shape.findEdgePtr(v1Ptr, v0Ptr) != -1) {
                    isShared = true;
                    break;
                }
            }

            if (!isShared) {
                boundaryEdges.push_back({v0Ptr, v1Ptr});
            }
        }
    }

    if (boundaryEdges.empty()) return geom::Polygon();

    // Chain edges together using pointer identity
    geom::Polygon result;
    result.pushShared(boundaryEdges[0].first);

    geom::PointPtr current = boundaryEdges[0].second;
    boundaryEdges.erase(boundaryEdges.begin());

    int maxIter = static_cast<int>(boundaryEdges.size()) + 10;
    int iter = 0;
    while (!boundaryEdges.empty() && iter++ < maxIter) {
        result.pushShared(current);

        bool found = false;
        for (auto it = boundaryEdges.begin(); it != boundaryEdges.end(); ++it) {
            // Compare by pointer identity
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
                result.pushShared(boundaryEdges[0].first);
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
                for (const auto& gatePtr : gates) {
                    if (patch->shape.containsPtr(gatePtr) ||
                        geom::Point::distance(patch->shape.centroid(), *gatePtr) < 10.0) {
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
    for (size_t i = 0; i < wards_.size(); ++i) {
        wards_[i]->createGeometry();
    }
}

} // namespace building
} // namespace town_generator
