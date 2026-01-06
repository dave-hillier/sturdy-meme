#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Topology.h"
#include "town_generator/utils/Noise.h"
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
#include "town_generator/wards/Harbour.h"
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
    coastNeeded = utils::Random::boolVal(0.5);  // 50% chance of coastal city (faithful to mfcg.js)
    riverNeeded = coastNeeded && utils::Random::boolVal(0.67);  // 67% when coast is present
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

    // Build canals/rivers if needed (faithful to mfcg.js buildCanals)
    if (riverNeeded && coastNeeded) {
        auto canal = Canal::createRiver(this);
        if (canal) {
            canals.push_back(std::move(canal));
        }
    }

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

    // Track max radius b during spiral generation (faithful to mfcg.js line 10341)
    double b = 0;
    for (int i = 0; i < totalPoints; ++i) {
        double a = sa + std::sqrt(static_cast<double>(i)) * 5.0;
        double r = (i == 0) ? 0.0 : 10.0 + i * (2.0 + utils::Random::floatVal());
        seeds.emplace_back(std::cos(a) * r, std::sin(a) * r);
        if (r > b) b = r;  // Track max radius
    }

    // Add 6 boundary points at radius 2*b to create outer cells that extend to the edge
    // (faithful to mfcg.js line 10344: Qd.regular(6, 2 * b))
    for (int i = 0; i < 6; ++i) {
        double a = i * M_PI / 3.0;  // 60 degrees apart
        seeds.emplace_back(std::cos(a) * 2 * b, std::sin(a) * 2 * b);
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

    // Save for later use
    maxRadius_ = b;
    offsetX_ = offsetX;
    offsetY_ = offsetY;

    // Offset seeds to positive coordinates
    for (auto& p : seeds) {
        p.x += offsetX;
        p.y += offsetY;
    }

    // Apply Lloyd relaxation to all city patches (3 iterations)
    // Faithful to mfcg.js line 10346: relaxing all nPatches*8 seeds
    // But exclude the 6 boundary points we added at the end
    int relaxCount = totalPoints;  // All spiral-generated points
    std::vector<geom::Point> citySeeds(seeds.begin(), seeds.begin() + std::min(relaxCount, static_cast<int>(seeds.size())));

    for (int iteration = 0; iteration < 3; ++iteration) {
        citySeeds = geom::Voronoi::relax(citySeeds, width, height);
    }

    // Replace city seeds with relaxed versions
    for (int i = 0; i < static_cast<int>(citySeeds.size()) && i < static_cast<int>(seeds.size()); ++i) {
        seeds[i] = citySeeds[i];
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

        // Filter out cells whose seed is at the boundary (the 6 points we added at 2*b)
        // These are only used to create Voronoi cells that extend to the edge
        // The actual cell filter in mfcg.js (line 10346-10348) removes cells with vertices > b
        // But we need to be careful not to remove useful edge cells
        // Check if this cell's seed is near the boundary (radius ~2*b)
        double seedX = region->seed.x - offsetX;
        double seedY = region->seed.y - offsetY;
        double seedDist = std::sqrt(seedX*seedX + seedY*seedY);
        if (seedDist > b * 1.5) continue;  // Skip boundary helper cells

        auto patch = std::make_unique<Patch>(geom::Polygon(sharedVertices));
        patchesCreated++;

        regionToPatch[region] = patch.get();
        patches.push_back(patch.get());
        ownedPatches_.push_back(std::move(patch));
    }

    // b was calculated during spiral generation (max radius from origin)
    // Store centroids for each patch (mfcg.js stores them in a map keyed by cell)
    // In mfcg.js, centroids are relative to origin (0,0)
    // Since we offset seeds by (offsetX, offsetY), we need to subtract that to get back to origin
    std::map<Patch*, geom::Point> patchCentroids;
    for (auto* patch : patches) {
        geom::Point c = patch->shape.centroid();
        // Convert back to origin-relative coordinates (undo the offset we applied to seeds)
        geom::Point relC(c.x - offsetX, c.y - offsetY);
        patchCentroids[patch] = relC;
    }

    SDL_Log("Coast: b=%.1f (max spiral radius), offsetX=%.1f, offsetY=%.1f", b, offsetX, offsetY);

    // Apply coast mask if needed (faithful to mfcg.js buildPatches coastNeeded block)
    if (coastNeeded) {
        // Generate coast direction if not set (mfcg.js line 10376)
        if (coastDir == 0.0) {
            coastDir = std::floor(utils::Random::floatVal() * 20) / 10.0;  // 0-2 in 0.1 steps
        }

        double angle = coastDir * M_PI;
        double cosA = std::cos(angle);
        double sinA = std::sin(angle);

        // Coast parameters (faithful to mfcg.js lines 10371-10374)
        // f = 20-60 random offset from coast center
        double f = 20.0 + utils::Random::floatVal() * 40.0;

        // k = lateral offset using normal3 distribution (±30% of b)
        // mfcg.js: k = .3 * b * (normal3 * 2 - 1)
        double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                         utils::Random::floatVal()) / 3.0;
        double k = 0.3 * b * (normal3 * 2.0 - 1.0);

        // n = coastline radius using normal4 distribution (20-70% of b)
        // mfcg.js: n = b * (.2 + abs(normal4 - 1)) where normal4 is sum of 4 randoms / 2
        double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                         utils::Random::floatVal() + utils::Random::floatVal()) / 2.0;
        double n = b * (0.2 + std::abs(normal4 - 1.0));

        // Coast center position (mfcg.js line 10381: g = new I(n + f, k))
        geom::Point coastCenter(n + f, k);

        SDL_Log("Coast params: b=%.1f f=%.1f k=%.1f n=%.1f coastCenter=(%.1f,%.1f) angle=%.2f",
                b, f, k, n, coastCenter.x, coastCenter.y, coastDir);

        // Mark patches as water based on distance from coast center
        int waterCount = 0;
        for (auto* patch : patches) {
            geom::Point c = patchCentroids[patch];

            // Rotate centroid by coast angle (mfcg.js line 10387)
            // r = new I(u.x * q - u.y * m, u.y * q + u.x * m)
            geom::Point rotated(c.x * cosA - c.y * sinA, c.y * cosA + c.x * sinA);

            // Distance from coast center minus radius (mfcg.js line 10388)
            // u = I.distance(g, r) - n
            double u = geom::Point::distance(coastCenter, rotated) - n;

            // If patch is "beyond" the coast center (towards the sea), use lateral distance
            // mfcg.js line 10389: r.x > g.x && (u = Math.min(u, Math.abs(r.y - k) - n))
            // We use a wider lateral band (1.5*n instead of n) to ensure water reaches the edge
            if (rotated.x > coastCenter.x) {
                u = std::min(u, std::abs(rotated.y - k) - n * 1.5);
            }

            // Additionally, only allow circular water region for patches that are
            // actually near the coast direction (rotated.x > 0), not in the opposite direction
            // This prevents water from appearing in the middle of the map on the city side
            if (rotated.x < coastCenter.x * 0.5) {
                // Patch is far from the sea direction - don't use circular check
                u = std::max(u, 1.0);  // Force positive (land)
            }

            // Fractal noise for coastline variation (faithful to mfcg.js)
            // mfcg.js line 10378: d = pg.fractal(6)
            // mfcg.js line 10390: r = d.get((r.x + b) / (2 * b), (r.y + b) / (2 * b)) * n * sqrt(r.length / b)
            double nx = (rotated.x + b) / (2.0 * b);
            double ny = (rotated.y + b) / (2.0 * b);
            // Use proper fractal noise (6 octaves, faithful to mfcg.js)
            static utils::FractalNoise coastNoise = utils::FractalNoise::create(6, 1.0, 0.5);
            double noise = coastNoise.get(nx, ny);
            double r = noise * n * std::sqrt(rotated.length() / b);

            // Mark as water if inside the coastline (u + r < 0)
            // mfcg.js line 10391: 0 > u + r && (c.waterbody = !0)
            if (u + r < 0) {
                patch->waterbody = true;
                waterCount++;
            }
        }
        SDL_Log("Coast: marked %d patches as water out of %zu total", waterCount, patches.size());
    }

    // Assign withinCity based on waterbody status
    int cityPatchCount = 0;
    for (auto* patch : patches) {
        if (!patch->waterbody && cityPatchCount < nPatches_) {
            patch->withinCity = true;
            patch->withinWalls = wallsNeeded;
            cityPatchCount++;
        } else {
            patch->withinCity = false;
            patch->withinWalls = false;
        }
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

    // Compute water edge and shore from water patches
    if (coastNeeded) {
        std::vector<Patch*> waterPatches;
        for (auto* patch : patches) {
            if (patch->waterbody) {
                waterPatches.push_back(patch);
            }
        }

        if (!waterPatches.empty()) {
            // Split water patches into connected components and take the largest
            // (faithful to mfcg.js lines 10505-10507: Z.max(Ic.split(a), a => a.length))
            auto waterComponents = splitIntoConnectedComponents(waterPatches);
            if (!waterComponents.empty()) {
                auto largestWater = std::max_element(waterComponents.begin(), waterComponents.end(),
                    [](const auto& a, const auto& b) { return a.size() < b.size(); });
                waterPatches = *largestWater;
                SDL_Log("Coast: %zu water components, using largest with %zu patches",
                        waterComponents.size(), waterPatches.size());
            }

            // Compute circumference of water patches (boundary polygon)
            waterEdge = findCircumference(waterPatches);

            // Smooth the water edge (faithful to mfcg.js line 10515)
            // Uses 1-3 random iterations like the original
            int smoothIterations = 1 + static_cast<int>(utils::Random::floatVal() * 3);
            waterEdge = geom::Polygon::smooth(waterEdge, nullptr, smoothIterations);

            // Also compute earth edge (boundary of land patches)
            std::vector<Patch*> landPatches;
            for (auto* patch : patches) {
                if (!patch->waterbody) {
                    landPatches.push_back(patch);
                }
            }

            // Split land patches into connected components and take the largest
            auto landComponents = splitIntoConnectedComponents(landPatches);
            if (!landComponents.empty()) {
                auto largestLand = std::max_element(landComponents.begin(), landComponents.end(),
                    [](const auto& a, const auto& b) { return a.size() < b.size(); });
                landPatches = *largestLand;
                SDL_Log("Coast: %zu land components, using largest with %zu patches",
                        landComponents.size(), landPatches.size());
            }

            earthEdge = findCircumference(landPatches);

            // Shore is the shared boundary between water and land
            // For now, use water edge as shore (simplified)
            shore = waterEdge;

            SDL_Log("Coast: waterEdge has %zu vertices (smoothed %d iterations), earthEdge has %zu vertices",
                    waterEdge.length(), smoothIterations, earthEdge.length());
        }
    }

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

        // Disable wall segments that border water or citadel
        // (faithful to mfcg.js line 10830: if (d.data == Tc.COAST || d.twin.face.data == this.citadel))
        for (size_t i = 0; i < wall->shape.length(); ++i) {
            geom::PointPtr v0 = wall->shape.ptr(i);
            geom::PointPtr v1 = wall->shape.ptr((i + 1) % wall->shape.length());

            // Check if this edge borders water (any patch sharing this edge is waterbody)
            bool bordersWater = false;
            for (auto* patch : patches) {
                if (patch->waterbody) {
                    // Check if water patch shares this edge (in either direction)
                    if ((patch->shape.containsPtr(v0) && patch->shape.containsPtr(v1))) {
                        // Verify it's actually an adjacent edge
                        int idx = patch->shape.indexOfPtr(v0);
                        if (idx != -1) {
                            size_t nextIdx = (idx + 1) % patch->shape.length();
                            size_t prevIdx = (idx + patch->shape.length() - 1) % patch->shape.length();
                            if (patch->shape.ptr(nextIdx) == v1 || patch->shape.ptr(prevIdx) == v1) {
                                bordersWater = true;
                                break;
                            }
                        }
                    }
                }
            }

            // Check if this edge borders citadel
            bool bordersCitadel = false;
            if (citadel) {
                if (citadel->shape.containsPtr(v0) && citadel->shape.containsPtr(v1)) {
                    int idx = citadel->shape.indexOfPtr(v0);
                    if (idx != -1) {
                        size_t nextIdx = (idx + 1) % citadel->shape.length();
                        size_t prevIdx = (idx + citadel->shape.length() - 1) % citadel->shape.length();
                        if (citadel->shape.ptr(nextIdx) == v1 || citadel->shape.ptr(prevIdx) == v1) {
                            bordersCitadel = true;
                        }
                    }
                }
            }

            if (bordersWater || bordersCitadel) {
                wall->segments[i] = false;
            }
        }

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

    // Smoothing function: mutates shared Point objects in place (like Haxe)
    // This moves both the artery AND the patch boundaries since they share Points
    auto smoothStreet = [](Street& street) {
        if (street.size() < 3) return;

        // Calculate smoothed positions (f=3 like Haxe smoothVertexEq(3))
        const double f = 3.0;
        std::vector<geom::Point> smoothed;
        smoothed.reserve(street.size());

        for (size_t i = 0; i < street.size(); ++i) {
            size_t prev = (i == 0) ? street.size() - 1 : i - 1;
            size_t next = (i + 1) % street.size();

            geom::Point avg;
            avg.x = (street[prev]->x + street[i]->x * f + street[next]->x) / (2.0 + f);
            avg.y = (street[prev]->y + street[i]->y * f + street[next]->y) / (2.0 + f);
            smoothed.push_back(avg);
        }

        // Mutate middle points in place (keep endpoints fixed, like Haxe)
        for (size_t i = 1; i + 1 < street.size(); ++i) {
            street[i]->x = smoothed[i].x;
            street[i]->y = smoothed[i].y;
        }
    };

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

    // Build streets from each gate to plaza, and roads outside for border gates
    for (const auto& gatePtr : gates) {
        // Find the vertex of the plaza closest to this gate using PointPtr
        geom::PointPtr endPtr = plaza->shape.ptr(0);
        double minDist = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < plaza->shape.length(); ++i) {
            double d = geom::Point::distance(*plaza->shape.ptr(i), *gatePtr);
            if (d < minDist) {
                minDist = d;
                endPtr = plaza->shape.ptr(i);
            }
        }

        // Build street from gate to plaza using PointPtr for mutable references
        auto path = topology_->buildPathPtrs(gatePtr, endPtr, &topology_->outer);
        if (!path.empty()) {
            streets.push_back(path);

            // Check if this is a border gate (not a citadel gate)
            bool isBorderGate = false;
            for (const auto& bg : border->gates) {
                if (bg == gatePtr) {
                    isBorderGate = true;
                    break;
                }
            }

            // Build road outside city for border gates
            if (isBorderGate) {
                // Find point at map edge in direction of gate from center
                // Direction: from town center through the gate, extended far out
                geom::Point gateDir = gatePtr->subtract(center);
                geom::Point dir = center.add(gateDir.norm(1000));  // Point 1000 units from center in gate direction

                geom::PointPtr startPtr = nullptr;
                double dist = std::numeric_limits<double>::infinity();

                for (const auto& [ptPtr, node] : topology_->pt2node) {
                    double d = geom::Point::distance(*ptPtr, dir);
                    if (d < dist) {
                        dist = d;
                        startPtr = ptPtr;
                    }
                }

                if (startPtr) {
                    // Build path from start to gate, excluding inner nodes
                    auto road = topology_->buildPathPtrs(startPtr, gatePtr, &topology_->inner);
                    if (!road.empty()) {
                        roads.push_back(road);
                    }
                }
            }
        }
    }

    // Consolidate streets and roads into unified arteries
    tidyUpRoads();

    // Smooth arteries - this MUTATES the shared Point objects,
    // so patch boundaries move with the streets (like Haxe)
    for (auto& artery : arteries) {
        smoothStreet(artery);
    }
}

void Model::tidyUpRoads() {
    // Segment structure for edge tracking (uses PointPtr for reference identity)
    struct Segment {
        geom::PointPtr start;
        geom::PointPtr end;
    };
    std::vector<Segment> segments;

    // Cut a street/road into segments (streets now use PointPtr directly)
    auto cut2segments = [&](const Street& street) {
        for (size_t i = 1; i < street.size(); ++i) {
            geom::PointPtr v0 = street[i - 1];
            geom::PointPtr v1 = street[i];

            // Skip segments along plaza edges
            if (plaza && plaza->shape.containsPtr(v0) && plaza->shape.containsPtr(v1)) {
                continue;
            }

            // Check if segment already exists (by pointer identity, like Haxe)
            bool exists = false;
            for (const auto& seg : segments) {
                if (seg.start == v0 && seg.end == v1) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                segments.push_back({v0, v1});
            }
        }
    };

    // Process all streets and roads
    for (const auto& street : streets) {
        cut2segments(street);
    }
    for (const auto& road : roads) {
        cut2segments(road);
    }

    // Chain segments together into arteries (like Haxe tidyUpRoads)
    arteries.clear();
    while (!segments.empty()) {
        auto seg = segments.back();
        segments.pop_back();

        bool attached = false;
        for (auto& artery : arteries) {
            // Compare by pointer identity (like Haxe: a[0] == seg.end)
            if (artery.front() == seg.end) {
                // Prepend to artery
                artery.insert(artery.begin(), seg.start);
                attached = true;
                break;
            } else if (artery.back() == seg.start) {
                // Append to artery
                artery.push_back(seg.end);
                attached = true;
                break;
            }
        }

        if (!attached) {
            // Create new artery with PointPtrs
            Street newArtery;
            newArtery.push_back(seg.start);
            newArtery.push_back(seg.end);
            arteries.push_back(newArtery);
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

std::vector<std::vector<Patch*>> Model::splitIntoConnectedComponents(const std::vector<Patch*>& patchList) {
    std::vector<std::vector<Patch*>> components;
    if (patchList.empty()) return components;

    // Create a set of patches we need to process
    std::set<Patch*> remaining(patchList.begin(), patchList.end());

    while (!remaining.empty()) {
        // Start a new component with any remaining patch
        std::vector<Patch*> component;
        std::vector<Patch*> queue;
        queue.push_back(*remaining.begin());

        // Flood fill through neighbors
        while (!queue.empty()) {
            Patch* current = queue.back();
            queue.pop_back();

            // Skip if already processed
            if (remaining.find(current) == remaining.end()) continue;

            remaining.erase(current);
            component.push_back(current);

            // Add neighbors that are in our original patch list
            for (Patch* neighbor : current->neighbors) {
                if (remaining.find(neighbor) != remaining.end()) {
                    queue.push_back(neighbor);
                }
            }
        }

        if (!component.empty()) {
            components.push_back(std::move(component));
        }
    }

    return components;
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
            if (patch->waterbody) {
                // Water patches don't get wards - skip
                continue;
            } else if (patch->withinCity) {
                // Check if this patch borders water - could be harbour
                bool bordersWater = false;
                for (auto* neighbor : patch->neighbors) {
                    if (neighbor->waterbody) {
                        bordersWater = true;
                        break;
                    }
                }

                if (bordersWater && coastNeeded && utils::Random::boolVal(0.5)) {
                    // Harbour ward for waterfront patches
                    ward = new wards::Harbour();
                    patch->landing = true;
                } else {
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
                }
            } else {
                // Outer patches - most are empty (no ward), some are farms
                // Reference mfcg.js only assigns farms to specific outer patches
                if (utils::Random::boolVal(0.15)) {
                    ward = new wards::Farm();
                } else if (utils::Random::boolVal(0.1)) {
                    ward = new wards::Park();
                }
                // Otherwise leave as nullptr - no buildings generated
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
    // First, set edge data on all patches
    setEdgeData();

    // Create WardGroups for unified geometry generation
    createWardGroups();

    // Generate geometry for each ward
    for (size_t i = 0; i < wards_.size(); ++i) {
        wards_[i]->createGeometry();
    }
}

void Model::setEdgeData() {
    // Set edge types on all patches (faithful to mfcg.js edge data assignment)
    // Edge types: COAST, ROAD, WALL, CANAL, HORIZON, NONE

    for (auto* patch : patches) {
        size_t len = patch->shape.length();

        for (size_t i = 0; i < len; ++i) {
            const geom::Point& v0 = patch->shape[i];
            const geom::Point& v1 = patch->shape[(i + 1) % len];

            EdgeType edgeType = EdgeType::NONE;

            // Check if edge borders water (COAST)
            for (auto* neighbor : patch->neighbors) {
                if (neighbor->waterbody) {
                    int edgeIdx = neighbor->findEdgeIndex(v1, v0);  // Reverse direction for neighbor
                    if (edgeIdx >= 0) {
                        edgeType = EdgeType::COAST;
                        break;
                    }
                }
            }

            // Check if edge is on wall (WALL)
            if (edgeType == EdgeType::NONE && wall) {
                if (wall->bordersBy(patch, v0, v1)) {
                    edgeType = EdgeType::WALL;
                }
            }

            // Check if edge is on a canal (CANAL)
            if (edgeType == EdgeType::NONE) {
                for (const auto& canal : canals) {
                    if (canal->containsEdge(v0, v1)) {
                        edgeType = EdgeType::CANAL;
                        break;
                    }
                }
            }

            // Check if edge is on a road (ROAD)
            if (edgeType == EdgeType::NONE) {
                auto isOnRoad = [&](const std::vector<Street>& roads) {
                    for (const auto& road : roads) {
                        if (road.size() < 2) continue;
                        for (size_t j = 0; j + 1 < road.size(); ++j) {
                            if ((*road[j] == v0 && *road[j + 1] == v1) ||
                                (*road[j] == v1 && *road[j + 1] == v0)) {
                                return true;
                            }
                        }
                    }
                    return false;
                };

                if (isOnRoad(arteries) || isOnRoad(streets) || isOnRoad(roads)) {
                    edgeType = EdgeType::ROAD;
                }
            }

            // Check if edge is on map boundary (HORIZON)
            // This is simplified - check if both vertices are near the border
            if (edgeType == EdgeType::NONE && !patch->withinCity) {
                auto bounds = borderPatch.shape.getBounds();
                double margin = 10.0;
                bool v0OnBorder = (v0.x < bounds.left + margin || v0.x > bounds.right - margin ||
                                   v0.y < bounds.top + margin || v0.y > bounds.bottom - margin);
                bool v1OnBorder = (v1.x < bounds.left + margin || v1.x > bounds.right - margin ||
                                   v1.y < bounds.top + margin || v1.y > bounds.bottom - margin);
                if (v0OnBorder && v1OnBorder) {
                    edgeType = EdgeType::HORIZON;
                }
            }

            patch->setEdgeType(i, edgeType);
        }
    }

    SDL_Log("Model: Set edge data on %zu patches", patches.size());
}

void Model::createWardGroups() {
    // Create WardGroups from adjacent patches with the same ward type
    WardGroupBuilder builder(this);
    wardGroups_ = builder.build();

    SDL_Log("Model: Created %zu ward groups", wardGroups_.size());
}

double Model::getCanalWidth(const geom::Point& v) const {
    // Get the canal width at a vertex (faithful to mfcg.js getCanalWidth)
    for (const auto& canal : canals) {
        double width = canal->getWidthAtVertex(v);
        if (width > 0) {
            return width;
        }
    }
    return 0.0;
}

} // namespace building
} // namespace town_generator
