#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Topology.h"
#include "town_generator/geom/EdgeChain.h"
#include "town_generator/utils/Noise.h"
#include <iostream>
#include <SDL3/SDL_log.h>
#include "town_generator/wards/Ward.h"
#include "town_generator/wards/Castle.h"
#include "town_generator/wards/Cathedral.h"
#include "town_generator/wards/Market.h"
#include "town_generator/wards/Alleys.h"
#include "town_generator/wards/Farm.h"
#include "town_generator/wards/Park.h"
#include "town_generator/wards/Harbour.h"
#include "town_generator/wards/Wilderness.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace town_generator {
namespace building {

City::City(int nCells, int seed) : nCells_(nCells) {
    utils::Random::reset(seed);

    // Random city features - size-dependent probabilities (faithful to MFCG Blueprint)
    // Reference: 01-blueprint-building.js lines 22-43
    plazaNeeded = utils::Random::boolVal(0.9);                                    // 90% (was 80%)
    wallsNeeded = utils::Random::boolVal((nCells + 30.0) / 80.0);                // size-dependent
    citadelNeeded = utils::Random::boolVal(0.5 + nCells / 100.0);                // size-dependent
    templeNeeded = utils::Random::boolVal(static_cast<double>(nCells) / 18.0);   // size-dependent
    slumsNeeded = wallsNeeded && utils::Random::boolVal(static_cast<double>(nCells) / 80.0);  // size-dependent
    coastNeeded = utils::Random::boolVal(0.5);
    riverNeeded = coastNeeded && utils::Random::boolVal(0.67);  // 67% when coast is present

    // maxDocks: sqrt(nCells/2) + 2 if river (faithful to mfcg.js line 10239)
    maxDocks = static_cast<int>(std::sqrt(nCells / 2.0)) + (riverNeeded ? 2 : 0);
}

City::~City() {
    delete citadel;
    if (wall != border) {
        delete wall;
    }
    delete border;
}

void City::build() {
    buildPatches();
    optimizeJunctions();
    buildWalls();
    buildDomains();  // Build horizon/shore edge classification
    disableCoastWallSegments();  // Must be after buildDomains (needs shoreE)
    buildStreets();

    // Build canals/rivers if needed (faithful to mfcg.js buildCanals)
    if (riverNeeded && coastNeeded) {
        auto canal = Canal::createRiver(this);
        if (canal) {
            canals.push_back(std::move(canal));
        }
    }

    createWards();
    if (slumsNeeded) {
        buildSlums(); // Build slums outside walls (before farms, per reference)
    }
    buildFarms();           // Apply sine-wave radial farm pattern
    buildGeometry();
}

std::vector<geom::Point> City::generateRandomPoints(int count, double width, double height) {
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

void City::buildPatches() {
    // Generate seed points using spiral algorithm (faithful to original)
    // Generate nCells * 8 points (faithful to Haxe) - inner cells plus outer
    // sa = random starting angle
    // for each point: a = sa + sqrt(i) * 5, r = (i == 0) ? 0 : 10 + i * (2 + random())

    double sa = utils::Random::floatVal() * M_PI * 2;  // Starting angle
    std::vector<geom::Point> seeds;
    int totalPoints = nCells_ * 8;  // 8x more points for outer regions (faithful to Haxe)
    seeds.reserve(totalPoints);

    // Track max radius b during spiral generation (faithful to mfcg.js line 10341)
    double b = 0;
    for (int i = 0; i < totalPoints; ++i) {
        double a = sa + std::sqrt(static_cast<double>(i)) * 5.0;
        double r = (i == 0) ? 0.0 : 10.0 + i * (2.0 + utils::Random::floatVal());
        seeds.emplace_back(std::cos(a) * r, std::sin(a) * r);
        if (r > b) b = r;  // Track max radius
    }

    // Plaza seed position override (faithful to mfcg.js line 10339-10342)
    // When plazaNeeded, override seeds 1-4 to form a cross pattern for rectangular plaza
    if (plazaNeeded && seeds.size() >= 5) {
        utils::Random::save();  // Save random state so plaza doesn't affect subsequent randoms

        double f = 8.0 + utils::Random::floatVal() * 8.0;  // 8-16 range
        double h = f * (1.0 + utils::Random::floatVal());  // f to 2f range
        b = std::max(b, h);  // Update max radius if needed

        // Override seeds 1-4 to form a cross pattern centered at (0, 0)
        // d is the starting angle (sa)
        seeds[1] = geom::Point(std::cos(sa) * f, std::sin(sa) * f);                          // right
        seeds[2] = geom::Point(std::cos(sa + M_PI/2) * h, std::sin(sa + M_PI/2) * h);       // up
        seeds[3] = geom::Point(std::cos(sa + M_PI) * f, std::sin(sa + M_PI) * f);           // left
        seeds[4] = geom::Point(std::cos(sa + 3*M_PI/2) * h, std::sin(sa + 3*M_PI/2) * h);   // down

        utils::Random::restore();  // Restore random state
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

    // Apply Lloyd relaxation to all city cells (3 iterations)
    // Faithful to mfcg.js line 10346: relaxing all nCells*8 seeds
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

    // Convert regions to cells
    auto regions = voronoi.partitioning();

    // Create cells from regions, sorted by distance from center
    geom::Point center(width / 2, height / 2);
    std::vector<std::pair<double, geom::Region*>> sortedRegions;

    for (auto* region : regions) {
        double dist = geom::Point::distance(region->seed, center);
        sortedRegions.push_back({dist, region});
    }

    std::sort(sortedRegions.begin(), sortedRegions.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build a map from Triangle* to shared PointPtr (circumcenter)
    // This ensures adjacent cells share the exact same Point object
    // so mutations propagate automatically (matching Haxe reference semantics)
    std::map<geom::Triangle*, geom::PointPtr> triangleToVertex;
    for (const auto& tr : voronoi.triangles) {
        triangleToVertex[tr.get()] = geom::makePoint(tr->c);
    }
    // Create cells using shared vertex pointers
    std::map<geom::Region*, Cell*> regionToPatch;
    int patchesCreated = 0;

    // Create all cells from Voronoi regions
    // First nCells_ are considered inner/withinCity, rest are outer (faithful to Haxe)
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

        auto patch = std::make_unique<Cell>(geom::Polygon(sharedVertices));
        patchesCreated++;

        // Faithful to MFCG: this.seed = C.seed = 48271 * C.seed % 2147483647
        // Advance the RNG and store the current seed for this cell
        utils::Random::floatVal();  // Advance the RNG
        patch->seed = utils::Random::getSeed();  // Store the seed state

        regionToPatch[region] = patch.get();
        cells.push_back(patch.get());
        ownedCells_.push_back(std::move(patch));
    }

    // b was calculated during spiral generation (max radius from origin)
    // Store centroids for each patch (mfcg.js stores them in a map keyed by cell)
    // In mfcg.js, centroids are relative to origin (0,0)
    // Since we offset seeds by (offsetX, offsetY), we need to subtract that to get back to origin
    std::map<Cell*, geom::Point> patchCentroids;
    for (auto* patch : cells) {
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

        // Mark cells as water based on distance from coast center
        int waterCount = 0;
        for (auto* patch : cells) {
            geom::Point c = patchCentroids[patch];

            // Rotate centroid by coast angle (mfcg.js line 10387)
            // r = new I(u.x * q - u.y * m, u.y * q + u.x * m)
            geom::Point rotated(c.x * cosA - c.y * sinA, c.y * cosA + c.x * sinA);

            // Distance from coast center minus radius (mfcg.js line 10388)
            // u = I.distance(g, r) - n
            double u = geom::Point::distance(coastCenter, rotated) - n;

            // If patch is "beyond" the coast center (towards the sea), use lateral distance
            // mfcg.js line 10389: r.x > g.x && (u = Math.min(u, Math.abs(r.y - k) - n))
            if (rotated.x > coastCenter.x) {
                u = std::min(u, std::abs(rotated.y - k) - n);
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
        SDL_Log("Coast: marked %d cells as water out of %zu total", waterCount, cells.size());
    }

    // Assign withinCity based on waterbody status
    int cityPatchCount = 0;
    for (auto* patch : cells) {
        if (!patch->waterbody && cityPatchCount < nCells_) {
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
        Cell* patch = regionToPatch[region];
        if (!patch) continue;

        auto neighborRegions = region->neighbors(voronoi.regions);
        for (auto* neighborRegion : neighborRegions) {
            Cell* neighborPatch = regionToPatch[neighborRegion];
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

    // Compute water edge and shore from water cells
    if (coastNeeded) {
        std::vector<Cell*> waterPatches;
        for (auto* patch : cells) {
            if (patch->waterbody) {
                waterPatches.push_back(patch);
            }
        }

        if (!waterPatches.empty()) {
            // Split water cells into connected components and take the largest
            // (faithful to mfcg.js lines 10505-10507: Z.max(Ic.split(a), a => a.length))
            auto waterComponents = splitIntoConnectedComponents(waterPatches);
            if (!waterComponents.empty()) {
                auto largestWater = std::max_element(waterComponents.begin(), waterComponents.end(),
                    [](const auto& a, const auto& b) { return a.size() < b.size(); });
                waterPatches = *largestWater;
                SDL_Log("Coast: %zu water components, using largest with %zu cells",
                        waterComponents.size(), waterPatches.size());
            }

            // Compute circumference of water cells (boundary polygon)
            // Store raw (unsmoothed) - getOcean() will apply smart smoothing later
            waterEdge = findCircumference(waterPatches);

            // Also compute earth edge (boundary of land cells)
            std::vector<Cell*> landPatches;
            for (auto* patch : cells) {
                if (!patch->waterbody) {
                    landPatches.push_back(patch);
                }
            }

            // Split land cells into connected components and take the largest
            auto landComponents = splitIntoConnectedComponents(landPatches);
            if (!landComponents.empty()) {
                auto largestLand = std::max_element(landComponents.begin(), landComponents.end(),
                    [](const auto& a, const auto& b) { return a.size() < b.size(); });
                landPatches = *largestLand;
                SDL_Log("Coast: %zu land components, using largest with %zu cells",
                        landComponents.size(), landPatches.size());
            }

            earthEdge = findCircumference(landPatches);

            // Shore is the shared boundary between water and land
            // This is the raw earthEdge (Voronoi vertices), used for alignment
            shore = earthEdge;

            SDL_Log("Coast: waterEdge has %zu vertices, earthEdge has %zu vertices",
                    waterEdge.length(), earthEdge.length());
        }
    }

    // Mark special cells
    // First patch (closest to center) can be plaza
    // A patch near the edge can be citadel location
    if (!cells.empty() && citadelNeeded) {
        // Find a patch that's within city but near the edge
        for (int i = static_cast<int>(cells.size()) - 1; i >= 0; --i) {
            if (cells[i]->withinCity) {
                // This will be marked for citadel during wall building
                break;
            }
        }
    }

    // Build DCEL from cell polygons for topological operations
    // This enables efficient circumference, edge collapse, and neighbor queries
    std::vector<geom::Polygon> cellPolygons;
    cellPolygons.reserve(cells.size());
    for (auto* cell : cells) {
        cellPolygons.push_back(cell->shape);
    }

    dcel_ = std::make_unique<geom::DCEL>(cellPolygons);

    // Link Cell <-> Face bidirectionally
    // The DCEL faces are created in the same order as the polygons
    for (size_t i = 0; i < cells.size() && i < dcel_->faces.size(); ++i) {
        cells[i]->face = dcel_->faces[i];
        dcel_->faces[i]->data = cells[i];
    }

    SDL_Log("DCEL built: %zu vertices, %zu edges, %zu faces",
            dcel_->vertices.size(), dcel_->edges.size(), dcel_->faces.size());
}

void City::optimizeJunctions() {
    // Merge vertices that are too close together (< 8 units)
    // Uses DCEL::collapseEdge for proper topological edge collapse
    // Faithful to mfcg.js optimizeJunctions which uses DCEL.collapseEdge

    if (!dcel_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "optimizeJunctions: DCEL not built");
        return;
    }

    // Collect faces to optimize (corresponding to inner cells)
    std::set<geom::Face*> facesToOptimize;
    for (auto* patch : inner) {
        if (patch->face) {
            facesToOptimize.insert(patch->face.get());
        }
    }

    // Compute dynamic threshold (faithful to MFCG optimizeJunctions)
    // Reference: 03-city.js line 181,191: threshold = max(3 * LTOWER_RADIUS, avgEdgeLength / 3)
    double totalEdgeLength = 0;
    int edgeCountForAvg = 0;
    for (const auto& edge : dcel_->edges) {
        if (!edge || !edge->origin || !edge->next || !edge->next->origin) continue;
        auto edgeFace = edge->getFace();
        if (!edgeFace || facesToOptimize.find(edgeFace.get()) == facesToOptimize.end()) continue;
        totalEdgeLength += edge->length();
        edgeCountForAvg++;
    }
    double avgEdgeLength = edgeCountForAvg > 0 ? totalEdgeLength / edgeCountForAvg : 0;
    double collapseThreshold = std::max(3.0 * CurtainWall::LTOWER_RADIUS, avgEdgeLength / 3.0);

    // Track affected cells for shape update
    std::set<Cell*> affectedCells;
    int collapseCount = 0;

    // Iterate through edges and collapse short ones
    // We need to be careful since collapseEdge modifies the structure
    bool changed = true;
    while (changed) {
        changed = false;

        for (const auto& edge : dcel_->edges) {
            if (!edge || !edge->origin || !edge->next || !edge->next->origin) continue;

            // Only process edges in faces we want to optimize
            auto edgeFace = edge->getFace();
            if (!edgeFace || facesToOptimize.find(edgeFace.get()) == facesToOptimize.end()) {
                continue;
            }

            // Check edge length against dynamic threshold
            double len = edge->length();
            if (len < collapseThreshold && len > 0.0) {
                // Collapse this edge using DCEL
                auto result = dcel_->collapseEdge(edge);

                if (result.vertex) {
                    collapseCount++;
                    changed = true;

                    // Mark affected cells for shape update
                    for (const auto& affected : result.affectedEdges) {
                        auto face = affected->getFace();
                        if (face && face->data) {
                            affectedCells.insert(static_cast<Cell*>(face->data));
                        }
                    }

                    // Also mark the edge's face
                    if (edgeFace && edgeFace->data) {
                        affectedCells.insert(static_cast<Cell*>(edgeFace->data));
                    }

                    // Twin's face too
                    auto twin = edge->getTwin();
                    if (twin) {
                        auto twinFace = twin->getFace();
                        if (twinFace && twinFace->data) {
                            affectedCells.insert(static_cast<Cell*>(twinFace->data));
                        }
                    }

                    break; // Restart iteration since edges vector changed
                }
            }
        }
    }

    // Update cell shapes from DCEL faces
    for (auto* cell : affectedCells) {
        if (cell->face) {
            auto polyPtrs = cell->face->getPolyPtrs();
            if (polyPtrs.size() >= 3) {
                cell->shape = geom::Polygon(polyPtrs);
            }
        }
    }

    SDL_Log("optimizeJunctions: collapsed %d edges, updated %zu cells",
            collapseCount, affectedCells.size());
}

void City::buildWalls() {
    // Separate inner and outer cells based on withinWalls flag
    inner.clear();
    std::vector<Cell*> outer;

    if (!wallsNeeded) {
        // No walls needed - all cells are inner
        inner = cells;
        for (auto* p : cells) {
            p->withinCity = true;
            p->withinWalls = true;  // For border calculation
        }
    } else {
        for (auto* patch : cells) {
            if (patch->withinWalls) {
                inner.push_back(patch);
            } else {
                outer.push_back(patch);
            }
        }

        if (inner.empty()) {
            inner = cells;
            for (auto* p : cells) {
                p->withinWalls = true;
            }
        }

    }

    // Find citadel patch (innermost patch suitable for fortification)
    Cell* citadelPatch = nullptr;
    std::vector<Cell*> citadelPatches;
    std::vector<geom::PointPtr> reservedPoints;  // Points that shouldn't be modified (by pointer identity)

    // Reserve water edge vertices to prevent gates on the coast
    // (faithful to mfcg.js buildWalls: excludePoints = this.waterEdge.slice())
    for (size_t i = 0; i < waterEdge.length(); ++i) {
        reservedPoints.push_back(waterEdge.ptr(i));
    }

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
        // Note: segment disabling and tower building happen in disableCoastWallSegments()
        // which is called after buildDomains() so shoreE is available
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

void City::buildDomains() {
    // Faithful to mfcg.js buildDomains (lines 795-831)
    // Build horizon edges (outer boundary) and shore edges (land-water boundary)

    horizonE.clear();
    shoreE.clear();

    // Find horizon edges: edges that have no neighboring patch on that side
    // These form the outer boundary of the entire map
    for (auto* patch : cells) {
        size_t len = patch->shape.length();
        for (size_t i = 0; i < len; ++i) {
            const geom::Point& v0 = patch->shape[i];
            const geom::Point& v1 = patch->shape[(i + 1) % len];

            // Check if this edge has a neighbor
            bool hasNeighbor = false;
            for (auto* neighbor : patch->neighbors) {
                // Check if neighbor shares this edge (reversed direction)
                for (size_t j = 0; j < neighbor->shape.length(); ++j) {
                    const geom::Point& n0 = neighbor->shape[j];
                    const geom::Point& n1 = neighbor->shape[(j + 1) % neighbor->shape.length()];
                    if ((n0 == v1 && n1 == v0) || (n0 == v0 && n1 == v1)) {
                        hasNeighbor = true;
                        break;
                    }
                }
                if (hasNeighbor) break;
            }

            if (!hasNeighbor) {
                horizonE.push_back({v0, v1});
            }
        }
    }

    // Find shore edges: edges between land and water cells
    if (coastNeeded) {
        for (auto* patch : cells) {
            if (patch->waterbody) continue;  // Only process land cells

            size_t len = patch->shape.length();
            for (size_t i = 0; i < len; ++i) {
                const geom::Point& v0 = patch->shape[i];
                const geom::Point& v1 = patch->shape[(i + 1) % len];

                // Check if this edge borders a water patch
                for (auto* neighbor : patch->neighbors) {
                    if (!neighbor->waterbody) continue;

                    // Check if neighbor shares this edge
                    for (size_t j = 0; j < neighbor->shape.length(); ++j) {
                        const geom::Point& n0 = neighbor->shape[j];
                        const geom::Point& n1 = neighbor->shape[(j + 1) % neighbor->shape.length()];
                        if ((n0 == v1 && n1 == v0) || (n0 == v0 && n1 == v1)) {
                            shoreE.push_back({v0, v1});
                            break;
                        }
                    }
                }
            }
        }
    }
}

void City::disableCoastWallSegments() {
    // Disable wall segments that border water (COAST edges) or citadel
    // (faithful to mfcg.js line 10830: if (d.data == Tc.COAST || d.twin.face.data == this.citadel))
    // This must be called after buildDomains() so shoreE is populated
    if (!wall) return;

    int disabledCount = 0;
    for (size_t i = 0; i < wall->shape.length(); ++i) {
        const geom::Point& v0 = wall->shape[i];
        const geom::Point& v1 = wall->shape[(i + 1) % wall->shape.length()];

        // Check if this wall edge is a COAST edge (land-water boundary)
        // shoreE contains all edges between land and water cells
        bool isCoastEdge = false;
        for (const auto& shore : shoreE) {
            // Check both orientations with coordinate tolerance
            bool matchForward = (std::abs(shore.first.x - v0.x) < 0.5 &&
                                 std::abs(shore.first.y - v0.y) < 0.5 &&
                                 std::abs(shore.second.x - v1.x) < 0.5 &&
                                 std::abs(shore.second.y - v1.y) < 0.5);
            bool matchReverse = (std::abs(shore.first.x - v1.x) < 0.5 &&
                                 std::abs(shore.first.y - v1.y) < 0.5 &&
                                 std::abs(shore.second.x - v0.x) < 0.5 &&
                                 std::abs(shore.second.y - v0.y) < 0.5);
            if (matchForward || matchReverse) {
                isCoastEdge = true;
                break;
            }
        }

        // Check if this edge borders citadel (coordinate comparison)
        bool bordersCitadel = false;
        if (citadel) {
            int idx0 = -1, idx1 = -1;
            for (size_t j = 0; j < citadel->shape.length(); ++j) {
                if (std::abs(citadel->shape[j].x - v0.x) < 0.1 &&
                    std::abs(citadel->shape[j].y - v0.y) < 0.1) {
                    idx0 = static_cast<int>(j);
                }
                if (std::abs(citadel->shape[j].x - v1.x) < 0.1 &&
                    std::abs(citadel->shape[j].y - v1.y) < 0.1) {
                    idx1 = static_cast<int>(j);
                }
            }
            if (idx0 != -1 && idx1 != -1) {
                int diff = std::abs(idx0 - idx1);
                int n = static_cast<int>(citadel->shape.length());
                if (diff == 1 || diff == n - 1) {
                    bordersCitadel = true;
                }
            }
        }

        if (isCoastEdge || bordersCitadel) {
            wall->segments[i] = false;
            disabledCount++;
        }
    }

    SDL_Log("City: Disabled %d wall segments (COAST edges or citadel border), shoreE has %zu edges",
            disabledCount, shoreE.size());

    // Build towers after segments are finalized
    wall->buildTowers();
}

void City::buildStreets() {
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

void City::tidyUpRoads() {
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

std::vector<Cell*> City::cellsByVertex(const geom::Point& v) {
    std::vector<Cell*> result;
    for (auto* p : cells) {
        if (p->shape.contains(v)) {
            result.push_back(p);
        }
    }
    return result;
}

std::vector<Cell*> City::cellsByVertexPtr(const geom::PointPtr& v) {
    std::vector<Cell*> result;
    for (auto* p : cells) {
        if (p->shape.containsPtr(v)) {
            result.push_back(p);
        }
    }
    return result;
}

geom::Polygon City::findCircumference(const std::vector<Cell*>& patchList) {
    if (patchList.empty()) return geom::Polygon();
    if (patchList.size() == 1) return patchList[0]->shape.copy();

    // Use DCEL::circumference if cells have DCEL faces
    // Convert Cell* list to FacePtr list
    std::vector<geom::FacePtr> faceList;
    faceList.reserve(patchList.size());
    bool allHaveFaces = true;
    for (auto* patch : patchList) {
        if (patch->face) {
            faceList.push_back(patch->face);
        } else {
            allHaveFaces = false;
            break;
        }
    }

    if (allHaveFaces && !faceList.empty()) {
        // Use DCEL circumference algorithm
        auto boundaryEdges = geom::DCEL::circumference(nullptr, faceList);
        if (!boundaryEdges.empty()) {
            // Convert edge chain to polygon with shared pointers
            auto points = geom::EdgeChain::toPolyPtrs(boundaryEdges);
            geom::Polygon result;
            for (const auto& pt : points) {
                result.pushShared(pt);
            }
            return result;
        }
    }

    // Fallback: manual boundary finding (for cases without DCEL)
    // Find all edges that belong to exactly one patch in the set
    std::vector<std::pair<geom::PointPtr, geom::PointPtr>> boundaryEdges;

    for (auto* patch : patchList) {
        size_t len = patch->shape.length();
        for (size_t i = 0; i < len; ++i) {
            geom::PointPtr v0Ptr = patch->shape.ptr(i);
            geom::PointPtr v1Ptr = patch->shape.ptr((i + 1) % len);

            // Check if this edge is shared with another patch in the set
            bool isShared = false;
            for (auto* other : patchList) {
                if (other == patch) continue;
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
            if (it->first == current) {
                current = it->second;
                boundaryEdges.erase(it);
                found = true;
                break;
            }
        }

        if (!found) {
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

std::vector<std::vector<Cell*>> City::splitIntoConnectedComponents(const std::vector<Cell*>& patchList) {
    std::vector<std::vector<Cell*>> components;
    if (patchList.empty()) return components;

    // Create a set of cells we need to process
    std::set<Cell*> remaining(patchList.begin(), patchList.end());

    while (!remaining.empty()) {
        // Start a new component with any remaining patch
        std::vector<Cell*> component;
        std::vector<Cell*> queue;
        queue.push_back(*remaining.begin());

        // Flood fill through neighbors
        while (!queue.empty()) {
            Cell* current = queue.back();
            queue.pop_back();

            // Skip if already processed
            if (remaining.find(current) == remaining.end()) continue;

            remaining.erase(current);
            component.push_back(current);

            // Add neighbors that are in our original patch list
            for (Cell* neighbor : current->neighbors) {
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

void City::createWards() {
    // Faithful to MFCG: All urban wards inside the city are Alleys type.
    // Special wards: Castle, Market, Cathedral, Park, Harbour
    // Outside walls: Farm, Wilderness, Slum (via buildSlums)

    // Track special ward assignments
    bool castleAssigned = false;
    bool marketAssigned = false;

    // Assign wards to cells
    for (size_t idx = 0; idx < cells.size(); ++idx) {
        auto* patch = cells[idx];
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
            // Cathedral placement is handled separately after other special wards (see below)
        }

        // Regular wards
        if (!ward) {
            if (patch->waterbody) {
                // Water cells don't get wards here - Harbour wards added later via addHarbour
                continue;
            } else if (patch->withinCity) {
                // Faithful to MFCG: mark land cells that border shore as landing
                // Harbour wards go on WATER cells, not land cells
                bool bordersWater = false;
                for (auto* neighbor : patch->neighbors) {
                    if (neighbor->waterbody) {
                        bordersWater = true;
                        break;
                    }
                }

                if (bordersWater && coastNeeded && maxDocks > 0) {
                    // Mark as landing - this land cell borders water
                    // The Harbour ward will be placed on the water neighbor later
                    patch->landing = true;
                    --maxDocks;
                }
                // All urban land wards are Alleys (including landing cells)
                ward = new wards::Alleys();
            } else {
                // Outer cells - handled by buildFarms() with sine-wave radial pattern
                // Leave as nullptr here - farms assigned later
            }
        }

        if (ward) {
            ward->patch = patch;
            ward->model = this;
            patch->ward = ward;
            wards_.emplace_back(ward);
        }
    }

    // Park placement (faithful to mfcg.js lines 970-993)
    // 1. Parks near citadel gate
    // 2. Additional parks in inner cells based on city size
    int parksCreated = 0;
    if (citadel && !citadel->gates.empty()) {
        auto citadelGate = citadel->gates[0];
        auto patchesAtGate = cellsByVertex(*citadelGate);
        if (patchesAtGate.size() == 3) {
            double parkProb = 1.0 - 2.0 / static_cast<double>(nCells_ - 1);
            if (utils::Random::floatVal() < parkProb) {
                for (auto* p : patchesAtGate) {
                    if (!p->ward) {
                        auto* ward = new wards::Park();
                        ward->patch = p;
                        ward->model = this;
                        p->ward = ward;
                        wards_.emplace_back(ward);
                        ++parksCreated;
                    }
                }
            }
        }
    }

    // Additional parks based on city size: (nCells - 10) / 20
    double parkCount = static_cast<double>(nCells_ - 10) / 20.0;
    int targetParks = static_cast<int>(parkCount);
    double frac = parkCount - targetParks;
    if (utils::Random::floatVal() < frac) ++targetParks;
    targetParks -= parksCreated;

    for (int i = 0; i < targetParks; ++i) {
        // Find a random inner patch without a ward
        std::vector<Cell*> candidates;
        for (auto* p : inner) {
            if (!p->ward) candidates.push_back(p);
        }
        if (candidates.empty()) break;

        Cell* p = candidates[utils::Random::intVal(0, static_cast<int>(candidates.size()))];
        auto* ward = new wards::Park();
        ward->patch = p;
        ward->model = this;
        p->ward = ward;
        wards_.emplace_back(ward);
    }

    // Cathedral placement (faithful to mfcg.js line 997)
    // Find the inner patch with no ward that is closest to center
    if (templeNeeded) {
        Cell* templePatch = nullptr;
        double minDist = std::numeric_limits<double>::max();

        for (auto* patch : inner) {
            if (!patch->ward) {
                double dist = patch->shape.centroid().length();  // Distance from origin
                if (dist < minDist) {
                    minDist = dist;
                    templePatch = patch;
                }
            }
        }

        if (templePatch) {
            auto* ward = new wards::Cathedral();
            ward->patch = templePatch;
            ward->model = this;
            templePatch->ward = ward;
            wards_.emplace_back(ward);
        }
    }

    // Add harbour wards to water cells adjacent to landing cells
    // Faithful to mfcg.js addHarbour (lines 10648-10657) and line 10715
    // Harbour wards go on WATER cells, with piers extending toward land
    for (auto* patch : cells) {
        if (!patch->landing) continue;  // Only process landing cells

        // For each neighbor of this landing cell, if it's a waterbody with no ward, add Harbour
        for (auto* neighbor : patch->neighbors) {
            if (neighbor && neighbor->waterbody && !neighbor->ward) {
                auto* ward = new wards::Harbour();
                ward->patch = neighbor;
                ward->model = this;
                neighbor->ward = ward;
                wards_.emplace_back(ward);
                SDL_Log("City: Created Harbour ward on water cell adjacent to landing");
            }
        }
    }
}

void City::buildFarms() {
    // Faithful to mfcg.js buildFarms (lines 1056-1070)
    // Uses sine-wave radial pattern for organic farm placement

    // Random parameters for sine-wave pattern
    double normal3_a = (utils::Random::floatVal() + utils::Random::floatVal() +
                        utils::Random::floatVal()) / 3.0;
    double a = normal3_a * 2.0;  // Amplitude of first sine wave

    double normal3_b = (utils::Random::floatVal() + utils::Random::floatVal() +
                        utils::Random::floatVal()) / 3.0;
    double b = normal3_b;  // Amplitude of second sine wave (double frequency)

    double c = utils::Random::floatVal() * M_PI * 2.0;  // Phase shift 1
    double d = utils::Random::floatVal() * M_PI * 2.0;  // Phase shift 2

    // Find max distance from center to any inner patch vertex
    geom::Point center(offsetX_, offsetY_);  // City center
    double maxDist = 0.0;
    for (auto* patch : inner) {
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            double dist = geom::Point::distance(patch->shape[i], center);
            maxDist = std::max(maxDist, dist);
        }
    }

    // Helper to check if patch borders shore edges
    auto bordersShore = [this](Cell* patch) -> bool {
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            const geom::Point& v0 = patch->shape[i];
            const geom::Point& v1 = patch->shape[(i + 1) % patch->shape.length()];
            for (const auto& edge : shoreE) {
                if ((edge.first == v0 && edge.second == v1) ||
                    (edge.first == v1 && edge.second == v0)) {
                    return true;
                }
            }
        }
        return false;
    };

    // Apply sine-wave pattern to outer cells without wards
    for (auto* patch : cells) {
        if (patch->ward) continue;  // Already has a ward
        if (patch->withinCity) continue;  // Only outer cells

        if (patch->waterbody) {
            // Water cells get base Ward (no buildings)
            continue;
        }

        if (bordersShore(patch)) {
            // MFCG: Shore-adjacent outer cells become Wilderness (not Harbour)
            // Reference: 03-city.js line 503: n.bordersInside(this.shoreE) ? new Wilderness(this, n)
            auto* ward = new wards::Wilderness();
            ward->patch = patch;
            ward->model = this;
            patch->ward = ward;
            wards_.emplace_back(ward);
            continue;
        }

        // Calculate position relative to center
        geom::Point patchCenter = patch->shape.centroid();
        geom::Point delta = patchCenter.subtract(center);
        double angle = std::atan2(delta.y, delta.x);
        double dist = delta.length();

        // Sine-wave threshold: a * sin(angle + c) + b * sin(2*angle + d)
        double threshold = a * std::sin(angle + c) + b * std::sin(2.0 * angle + d);

        // If distance < (threshold + 1) * maxDist, it's a farm
        if (dist < (threshold + 1.0) * maxDist) {
            auto* ward = new wards::Farm();
            ward->patch = patch;
            ward->model = this;
            patch->ward = ward;
            wards_.emplace_back(ward);
        }
        // Otherwise leave as nullptr (no buildings)
    }
}

void City::buildSlums() {
    // Faithful to mfcg.js buildShantyTowns (lines 10763-10823)
    // Creates slums directly adjacent to city walls, growing outward along roads

    // Helper to check if patch borders horizon (outer boundary)
    auto bordersHorizon = [this](Cell* patch) -> bool {
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            const geom::Point& v0 = patch->shape[i];
            const geom::Point& v1 = patch->shape[(i + 1) % patch->shape.length()];
            for (const auto& edge : horizonE) {
                if ((edge.first == v0 && edge.second == v1) ||
                    (edge.first == v1 && edge.second == v0)) {
                    return true;
                }
            }
        }
        return false;
    };

    // Helper to calculate distance-based score for slum placement
    // Reference: mfcg.js lines 10765-10780 - function d(b)
    // Lower score = closer to roads/shore = more likely to be selected
    geom::Point center(offsetX_, offsetY_);
    auto calcScore = [&](Cell* patch) -> double {
        geom::Point patchCenter = patch->shape.centroid();
        double minDist = geom::Point::distance(patchCenter, center) * 3.0;

        // Find minimum distance to any road vertex
        for (const auto& road : roads) {
            for (const auto& pointPtr : road) {
                double d = geom::Point::distance(*pointPtr, patchCenter) * 2.0;
                minDist = std::min(minDist, d);
            }
        }

        // Find minimum distance to shore
        for (const auto& edge : shoreE) {
            double d = geom::Point::distance(edge.first, patchCenter);
            minDist = std::min(minDist, d);
        }

        // Find minimum distance to canals
        for (const auto& canal : canals) {
            for (const auto& pt : canal->getCenterline()) {
                double d = geom::Point::distance(pt, patchCenter);
                minDist = std::min(minDist, d);
            }
        }

        return minDist * minDist;
    };

    // Candidates and scores - built by scanning neighbors of withinCity cells
    std::vector<Cell*> candidates;
    std::vector<double> scores;

    // Helper to check if a cell is near a road
    auto isNearRoad = [&](Cell* patch) -> bool {
        geom::Point patchCenter = patch->shape.centroid();
        const double roadThreshold = 5.0;  // Distance threshold to consider "near road"
        for (const auto& road : roads) {
            for (const auto& pointPtr : road) {
                double d = geom::Point::distance(*pointPtr, patchCenter);
                if (d < roadThreshold) {
                    return true;
                }
            }
        }
        return false;
    };

    // Helper function to find slum candidates adjacent to a withinCity cell
    // Modified: Only allows slums on sides adjacent to walls or roads
    // Reference: mfcg.js lines 10781-10800 - function f(f)
    auto findCandidates = [&](Cell* cityCell) {
        for (auto* neighbor : cityCell->neighbors) {
            // Skip if already in city, water, has ward, or at horizon
            if (neighbor->withinCity) continue;
            if (neighbor->waterbody) continue;
            if (neighbor->ward) continue;
            if (bordersHorizon(neighbor)) continue;

            // Skip if already a candidate
            if (std::find(candidates.begin(), candidates.end(), neighbor) != candidates.end()) {
                continue;
            }

            // Check if this neighbor is adjacent to a withinWalls cell (against the wall)
            bool adjacentToWall = false;
            for (auto* n : neighbor->neighbors) {
                if (n->withinWalls) {
                    adjacentToWall = true;
                    break;
                }
            }

            // Only allow slums on sides adjacent to walls or roads
            // This prevents slums from forming on outward-facing sides
            if (!adjacentToWall && !isNearRoad(neighbor)) {
                continue;
            }

            // Count how many of this neighbor's neighbors are withinCity
            int cityNeighborCount = 0;
            for (auto* n : neighbor->neighbors) {
                if (n->withinCity) ++cityNeighborCount;
            }

            // Need at least 1 city neighbor (relaxed from reference's 2 to get more candidates near walls)
            if (cityNeighborCount >= 1) {
                candidates.push_back(neighbor);
                double score = static_cast<double>(cityNeighborCount * cityNeighborCount) / calcScore(neighbor);
                scores.push_back(score);
            }
        }
    };

    // Initial candidate finding: scan neighbors of all withinCity cells
    // Reference: mfcg.js lines 10801-10804
    int cityCellCount = 0;
    for (auto* patch : cells) {
        if (patch->withinCity) {
            ++cityCellCount;
            findCandidates(patch);
        }
    }
    SDL_Log("buildSlums: Scanned %d withinCity cells", cityCellCount);

    // Randomly select cells to become slums based on weighted score
    // Reference: mfcg.js line 10806-10807: nPatches * (1 + r*r*r) * 0.5
    // Reduce target to prevent excessive spread - slums should cluster near walls
    double r = utils::Random::floatVal();
    int targetSlums = static_cast<int>(nCells_ * (0.3 + r * r * 0.2));  // Much smaller: 30-50% of nCells instead of 50-100%

    SDL_Log("buildSlums: %zu initial candidates, targeting %d slums", candidates.size(), targetSlums);

    int slumsCreated = 0;
    while (targetSlums > 0 && !candidates.empty()) {
        // Calculate total score for weighted selection
        double totalScore = 0.0;
        for (double s : scores) totalScore += s;
        if (totalScore <= 0.0) break;

        // Weighted random selection (reference: Z.weighted)
        double pick = utils::Random::floatVal() * totalScore;
        double acc = 0.0;
        size_t selected = 0;
        for (size_t i = 0; i < scores.size(); ++i) {
            acc += scores[i];
            if (pick <= acc) {
                selected = i;
                break;
            }
        }

        Cell* patch = candidates[selected];

        // Mark as withinCity so it gets urban treatment
        // Reference: mfcg.js line 10816: n.withinCity = !0
        patch->withinCity = true;

        // Check for landing status (near shore) - reference line 10817
        if (maxDocks > 0) {
            bool bordersShore = false;
            for (const auto& edge : shoreE) {
                for (size_t i = 0; i < patch->shape.length(); ++i) {
                    const geom::Point& v0 = patch->shape[i];
                    const geom::Point& v1 = patch->shape[(i + 1) % patch->shape.length()];
                    if ((edge.first == v0 && edge.second == v1) ||
                        (edge.first == v1 && edge.second == v0)) {
                        bordersShore = true;
                        break;
                    }
                }
                if (bordersShore) break;
            }
            if (bordersShore) {
                patch->landing = true;
                --maxDocks;
            }
        }

        // Create Alleys ward - reference line 10818
        auto* ward = new wards::Alleys();
        ward->patch = patch;
        ward->model = this;
        patch->ward = ward;
        wards_.emplace_back(ward);

        // Remove from candidates
        candidates.erase(candidates.begin() + selected);
        scores.erase(scores.begin() + selected);

        // NOTE: Reference has recursive findCandidates(patch) here (line 10822)
        // but this causes slums to spread too far from walls.
        // Only use initial candidates to keep slums close to city edge.

        --targetSlums;
        ++slumsCreated;
    }

    SDL_Log("buildSlums: Created %d slums", slumsCreated);
}

void City::buildGeometry() {
    // First, set edge data on all cells
    setEdgeData();

    // Create WardGroups for unified geometry generation
    createWardGroups();

    // Generate geometry for each ward
    SDL_Log("City: Starting geometry creation for %zu wards", wards_.size());
    for (size_t i = 0; i < wards_.size(); ++i) {
        SDL_Log("City: Creating geometry for ward %zu (%s)", i, wards_[i]->getName().c_str());
        wards_[i]->createGeometry();
    }
    SDL_Log("City: Geometry creation complete");
}

void City::setEdgeData() {
    // Set edge types on all cells and DCEL half-edges (faithful to mfcg.js edge data assignment)
    // Edge types: COAST, ROAD, WALL, CANAL, HORIZON, NONE
    //
    // This propagates edge data to DCEL half-edges so Ward code can use
    // face.edges() with edge.getData<EdgeType>() instead of geometric detection.
    // Reference: mfcg-clean uses EdgeChain.assignData() to set edge.data

    for (auto* patch : cells) {
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

            // Set edge type on Cell's edgeData map
            patch->setEdgeType(i, edgeType);

            // Also set edge type on DCEL half-edge (faithful to mfcg.js EdgeChain.assignData)
            if (patch->face && patch->face->halfEdge) {
                // Find the DCEL half-edge corresponding to this cell edge
                // Cell edge i goes from shape[i] to shape[(i+1) % len]
                // We need to find the half-edge with origin at shape[i]
                geom::PointPtr originPtr = patch->shape.ptr(i);
                for (const auto& edge : patch->face->edges()) {
                    if (edge->origin && edge->origin->point == originPtr) {
                        edge->setData(edgeType);
                        break;
                    }
                }
            }
        }
    }

    SDL_Log("City: Set edge data on %zu cells and DCEL half-edges", cells.size());
}

void City::createWardGroups() {
    // Create WardGroups from adjacent cells with the same ward type
    WardGroupBuilder builder(this);
    wardGroups_ = builder.build();

    SDL_Log("City: Created %zu ward groups", wardGroups_.size());
}

double City::getCanalWidth(const geom::Point& v) const {
    // Get the canal width at a vertex (faithful to mfcg.js getCanalWidth)
    for (const auto& canal : canals) {
        double width = canal->getWidthAtVertex(v);
        if (width > 0) {
            return width;
        }
    }
    return 0.0;
}

geom::Polygon City::getOcean() const {
    // Faithful to mfcg.js getOcean() (lines 10838-10855)
    // Returns the ocean polygon for rendering, smoothed except at landing areas
    // This ensures piers align properly with the rendered water boundary
    //
    // Reference logic:
    // - Fix boundary edges (no twin)
    // - Fix vertices at landing cells (pier connection points)
    // - Fix vertices adjacent to landing cells (sequence preservation)
    // - Fix convex vertices on earthEdge within city (sharp coast corners)

    if (waterEdge.length() == 0) {
        return geom::Polygon();
    }

    // Collect vertices that should NOT be smoothed (fixed points)
    std::vector<geom::Point> fixedPoints;

    // Build a map of water edge vertices to their neighbor cell info
    // We need to know: is this vertex on a landing cell? is the cell within city?
    struct VertexInfo {
        bool isLanding = false;
        bool withinCity = false;
        bool onEarthEdge = false;
        int earthEdgeIndex = -1;
    };
    std::vector<VertexInfo> vertexInfos(waterEdge.length());

    // For each water edge vertex, find neighbor cells and classify
    for (size_t j = 0; j < waterEdge.length(); ++j) {
        const geom::Point& v = waterEdge[j];

        // Check all cells for this vertex
        for (const auto* cell : cells) {
            if (cell->waterbody) continue;  // Skip water cells

            // Check if this vertex is on the cell's boundary
            for (size_t i = 0; i < cell->shape.length(); ++i) {
                if (geom::Point::distance(cell->shape[i], v) < 0.01) {
                    // Found a land cell at this vertex
                    if (cell->landing) {
                        vertexInfos[j].isLanding = true;
                    }
                    if (cell->withinCity) {
                        vertexInfos[j].withinCity = true;
                    }
                    break;
                }
            }
        }

        // Check if on earth edge and get index for convex check
        for (size_t i = 0; i < earthEdge.length(); ++i) {
            if (geom::Point::distance(earthEdge[i], v) < 0.01) {
                vertexInfos[j].onEarthEdge = true;
                vertexInfos[j].earthEdgeIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // Now apply the reference logic: fix points based on landing and convex criteria
    bool prevWasLanding = false;
    for (size_t j = 0; j < waterEdge.length(); ++j) {
        const VertexInfo& info = vertexInfos[j];
        bool shouldFix = false;

        if (info.isLanding) {
            // Landing cell vertex - always fix for pier alignment
            shouldFix = true;
        } else if (info.withinCity && info.onEarthEdge && info.earthEdgeIndex >= 0) {
            // Within city and on earth edge - fix if convex (sharp corner)
            if (earthEdge.isConvexVertexi(info.earthEdgeIndex)) {
                shouldFix = true;
            }
        }

        // If previous vertex was landing, also fix this one (sequence preservation)
        // This ensures smooth transition at dock edges
        if (prevWasLanding) {
            shouldFix = true;
        }

        if (shouldFix) {
            fixedPoints.push_back(waterEdge[j]);
        }

        prevWasLanding = info.isLanding;
    }

    // Apply Chaikin smoothing with fixed points (3 iterations like the reference)
    // Reference: mfcg.js line 10855: Chaikin.render(this.waterEdge, !0, 3, a)
    return geom::Polygon::chaikin(waterEdge, true, 3, &fixedPoints);
}

} // namespace building
} // namespace town_generator
