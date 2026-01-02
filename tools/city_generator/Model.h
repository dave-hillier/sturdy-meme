// Model: Main city generation orchestrator
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// 1. Generate seed points in spiral pattern
// 2. Create Voronoi tessellation -> patches
// 3. Optionally add city walls (citadel + main wall)
// 4. Build street network from gates to center
// 5. Assign wards to patches based on location ratings
// 6. Generate building geometry for each ward

#pragma once

#include "Geometry.h"
#include "Voronoi.h"
#include "Patch.h"
#include "Ward.h"
#include "CurtainWall.h"
#include "Graph.h"
#include "WaterFeatures.h"
#include <vector>
#include <memory>
#include <random>
#include <optional>
#include <cmath>

namespace city {

// City generation parameters
struct CityParams {
    // Size and scale
    float radius = 100.0f;          // City radius in units
    int numPatches = 30;            // Number of Voronoi patches
    int relaxIterations = 3;        // Lloyd relaxation iterations

    // Feature flags
    bool hasWalls = true;           // Generate city walls
    bool hasCitadel = false;        // Generate inner citadel
    bool hasPlaza = true;           // Generate central plaza
    bool hasTemple = true;          // Generate cathedral/temple
    bool hasCastle = true;          // Generate castle

    // Street parameters
    float mainStreetWidth = 2.0f;
    float streetWidth = 1.0f;
    float alleyWidth = 0.6f;

    // Wall parameters
    float wallRadius = 0.7f;        // Wall at 70% of city radius
    float citadelRadius = 0.3f;     // Citadel at 30% of city radius
    float minGateDistance = 30.0f;  // Minimum distance between gates

    // Water parameters
    bool hasRiver = false;          // City has a river flowing through
    bool hasCoast = false;          // City is coastal
    bool hasShantyTown = true;      // Buildings outside walls
    float coastDirection = 0.0f;    // Direction to coast (radians, 0=east)
    float riverWidth = 5.0f;        // Base river width
    int numPiers = 3;               // Number of piers for coastal cities

    // Random seed (0 = random)
    uint32_t seed = 0;
};

// Street segment
struct Street {
    std::vector<Vec2> path;
    float width = 1.0f;
    bool isMainStreet = false;
};

class Model {
public:
    CityParams params;

    // City boundary
    Polygon border;

    // Voronoi patches
    std::vector<std::unique_ptr<Patch>> patches;
    std::vector<Patch*> innerPatches;     // Patches within city
    std::vector<Patch*> wallPatches;      // Patches within walls

    // Fortifications
    std::optional<CurtainWall> wall;
    std::optional<CurtainWall> citadel;
    std::vector<Vec2> gates;

    // Streets
    std::vector<Street> streets;
    std::vector<Street> roads;            // Roads to outer patches

    // Wards
    std::vector<std::unique_ptr<Ward>> wards;

    // Plaza (if any)
    std::optional<Polygon> plaza;
    Vec2 plazaCenter{0, 0};

    // Water features
    WaterFeatures water;

    // Generate the city
    void generate(const CityParams& params);

    // Get the center of the city
    Vec2 getCenter() const { return {0, 0}; }

    // Get all building polygons
    std::vector<Polygon> getAllBuildings() const;

    // Get patches by ward type
    std::vector<Patch*> getPatchesByWardType(WardType type) const;

private:
    std::mt19937 rng;

    // Generation steps
    void generateBorder();
    void generatePatches();
    void generateWater();
    void buildWalls();
    void buildStreets();
    void assignWards();
    void createGeometry();

    // Helper methods
    std::vector<Vec2> generateSpiralPoints(int count, float radius);
    void findNeighbors();
    void classifyPatches();
    Ward* createWard(Patch* patch, WardType type);
    WardType selectWardType(Patch* patch);
};

// Implementation

inline void Model::generate(const CityParams& p) {
    params = p;

    // Initialize RNG
    if (params.seed == 0) {
        std::random_device rd;
        rng.seed(rd());
    } else {
        rng.seed(params.seed);
    }

    generateBorder();
    generatePatches();
    findNeighbors();
    generateWater();
    buildWalls();
    classifyPatches();
    buildStreets();
    assignWards();
    createGeometry();
}

inline void Model::generateWater() {
    if (!params.hasRiver && !params.hasCoast) return;

    WaterConfig config;
    config.hasRiver = params.hasRiver;
    config.hasCoast = params.hasCoast;
    config.hasPonds = true;  // Always allow small ponds
    config.riverWidth = params.riverWidth;
    config.coastDirection = params.coastDirection;
    config.numPiers = params.hasCoast ? params.numPiers : 0;

    // Collect patch pointers
    std::vector<Patch*> patchPtrs;
    for (auto& p : patches) {
        patchPtrs.push_back(p.get());
    }

    water.generate(config, params.radius, patchPtrs, rng);

    // Mark water patches as not within city (can't build on water)
    auto waterPatches = water.getWaterPatches(patchPtrs);
    for (auto* patch : waterPatches) {
        patch->withinCity = false;
        patch->withinWalls = false;
    }
}

inline void Model::generateBorder() {
    // Create circular city boundary
    border = Polygon::regular(32, params.radius, {0, 0});
}

inline std::vector<Vec2> Model::generateSpiralPoints(int count, float radius) {
    std::vector<Vec2> points;
    points.reserve(count);

    // Use Fermat's spiral for even distribution
    float goldenAngle = 3.14159265f * (3.0f - std::sqrt(5.0f));

    for (int i = 0; i < count; i++) {
        float r = radius * std::sqrt(static_cast<float>(i) / count);
        float theta = i * goldenAngle;

        // Add some randomness
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        float jitterR = r * 0.1f * dist(rng);
        float jitterTheta = 0.1f * dist(rng);

        points.push_back({
            (r + jitterR) * std::cos(theta + jitterTheta),
            (r + jitterR) * std::sin(theta + jitterTheta)
        });
    }

    return points;
}

inline void Model::generatePatches() {
    // Generate seed points
    auto seeds = generateSpiralPoints(params.numPatches, params.radius * 0.9f);

    // Build Voronoi diagram
    Voronoi voronoi = Voronoi::build(seeds);

    // Relax for better distribution
    for (int i = 0; i < params.relaxIterations; i++) {
        voronoi = Voronoi::relax(voronoi);
    }

    // Create patches from Voronoi regions
    auto regions = voronoi.getInteriorRegions();
    for (auto* region : regions) {
        auto patch = std::make_unique<Patch>(*region);

        // Clip to city boundary
        // For now, just check if centroid is within city
        if (border.contains(patch->seed)) {
            patch->withinCity = true;
        }

        patches.push_back(std::move(patch));
    }
}

inline void Model::findNeighbors() {
    // Find neighboring patches (those that share edges)
    for (size_t i = 0; i < patches.size(); i++) {
        for (size_t j = i + 1; j < patches.size(); j++) {
            if (patches[i]->borders(*patches[j])) {
                patches[i]->neighbors.push_back(patches[j].get());
                patches[j]->neighbors.push_back(patches[i].get());
            }
        }
    }
}

inline void Model::buildWalls() {
    if (!params.hasWalls) return;

    // Collect patches that should be within walls
    std::vector<Patch*> wallInner;
    float wallDist = params.radius * params.wallRadius;

    for (auto& patch : patches) {
        if (patch->withinCity) {
            float dist = Vec2::distance(patch->seed, getCenter());
            if (dist < wallDist) {
                wallInner.push_back(patch.get());
                patch->withinWalls = true;
            }
        }
    }

    if (wallInner.empty()) return;

    // Build main wall
    std::vector<Patch*> allPatches;
    for (auto& p : patches) allPatches.push_back(p.get());

    wall.emplace();
    wall->build(wallInner, allPatches, 2);
    wall->buildGates(wallInner, params.minGateDistance, rng);
    wall->buildTowers();

    // Copy gates
    gates = wall->gates;

    // Build citadel if requested
    if (params.hasCitadel) {
        std::vector<Patch*> citadelInner;
        float citadelDist = params.radius * params.citadelRadius;

        for (auto* patch : wallInner) {
            float dist = Vec2::distance(patch->seed, getCenter());
            if (dist < citadelDist) {
                citadelInner.push_back(patch);
            }
        }

        if (citadelInner.size() >= 3) {
            citadel.emplace();
            citadel->build(citadelInner, allPatches, 1);
        }
    }
}

inline void Model::classifyPatches() {
    // Mark which patches are within walls
    for (auto& patch : patches) {
        if (patch->withinCity) {
            innerPatches.push_back(patch.get());

            if (patch->withinWalls) {
                wallPatches.push_back(patch.get());
            }
        }
    }
}

inline void Model::buildStreets() {
    // Build street network using pathfinding
    // Streets connect gates to city center and to each other

    if (gates.empty()) {
        // No gates, create streets from center outward
        if (params.hasPlaza) {
            // Find central patch for plaza
            Patch* centralPatch = nullptr;
            float minDist = std::numeric_limits<float>::max();

            for (auto* patch : wallPatches) {
                float dist = Vec2::distance(patch->seed, getCenter());
                if (dist < minDist) {
                    minDist = dist;
                    centralPatch = patch;
                }
            }

            if (centralPatch) {
                plazaCenter = centralPatch->seed;
                plaza = centralPatch->shape.inset(REGULAR_STREET);
            }
        }
        return;
    }

    // Create topology for pathfinding
    Topology topology;
    std::vector<std::vector<Vec2>*> shapes;
    std::vector<bool> withinCity;

    for (auto& patch : patches) {
        shapes.push_back(&patch->shape.vertices);
        withinCity.push_back(patch->withinCity);
    }

    // Blocked points are wall vertices (except gates)
    std::vector<Vec2> blocked;
    if (wall) {
        for (const auto& v : wall->shape.vertices) {
            bool isGate = false;
            for (const auto& g : gates) {
                if (v == g) {
                    isGate = true;
                    break;
                }
            }
            if (!isGate) {
                blocked.push_back(v);
            }
        }
    }

    topology.build(shapes, withinCity, blocked, &border);

    // Find or create plaza center
    if (params.hasPlaza) {
        Patch* centralPatch = nullptr;
        float minDist = std::numeric_limits<float>::max();

        for (auto* patch : wallPatches) {
            float dist = Vec2::distance(patch->seed, getCenter());
            if (dist < minDist) {
                minDist = dist;
                centralPatch = patch;
            }
        }

        if (centralPatch) {
            plazaCenter = centralPatch->seed;
            plaza = centralPatch->shape.inset(REGULAR_STREET);
        }
    }

    // Build main streets from gates to center
    for (const auto& gate : gates) {
        // Find the vertex in topology closest to gate
        Vec2* gateVertex = nullptr;
        float minDist = std::numeric_limits<float>::max();

        for (auto& [pt, node] : topology.pointToNode) {
            float dist = Vec2::distance(*pt, gate);
            if (dist < minDist) {
                minDist = dist;
                gateVertex = pt;
            }
        }

        // Find vertex closest to plaza center
        Vec2* centerVertex = nullptr;
        minDist = std::numeric_limits<float>::max();

        for (auto& [pt, node] : topology.pointToNode) {
            float dist = Vec2::distance(*pt, plazaCenter);
            if (dist < minDist) {
                minDist = dist;
                centerVertex = pt;
            }
        }

        if (gateVertex && centerVertex) {
            auto path = topology.buildPath(gateVertex, centerVertex);
            if (!path.empty()) {
                Street street;
                street.path = path;
                street.width = params.mainStreetWidth;
                street.isMainStreet = true;
                streets.push_back(street);
            }
        }
    }
}

inline void Model::assignWards() {
    // Create list of available ward types (with quantities)
    std::vector<WardType> availableTypes;

    // Special wards (one each max)
    if (params.hasCastle) availableTypes.push_back(WardType::Castle);
    if (params.hasTemple) availableTypes.push_back(WardType::Cathedral);
    if (params.hasPlaza) availableTypes.push_back(WardType::Market);

    // Fill remaining with common ward types
    while (availableTypes.size() < patches.size()) {
        std::uniform_int_distribution<int> dist(0, 6);
        switch (dist(rng)) {
            case 0: availableTypes.push_back(WardType::Patriciate); break;
            case 1: availableTypes.push_back(WardType::Craftsmen); break;
            case 2: availableTypes.push_back(WardType::Merchants); break;
            case 3: availableTypes.push_back(WardType::Slum); break;
            case 4: availableTypes.push_back(WardType::Military); break;
            case 5: availableTypes.push_back(WardType::Administration); break;
            default: availableTypes.push_back(WardType::Craftsmen); break;
        }
    }

    // Assign wards greedily based on location ratings
    std::vector<Patch*> unassigned;
    for (auto& patch : patches) {
        if (patch->withinCity) {
            unassigned.push_back(patch.get());
        }
    }

    for (WardType type : availableTypes) {
        if (unassigned.empty()) break;

        // Find best patch for this ward type
        Patch* bestPatch = nullptr;
        float bestRating = std::numeric_limits<float>::infinity();

        for (Patch* patch : unassigned) {
            float rating = Ward::rateLocation(*this, *patch, type);
            if (rating < bestRating) {
                bestRating = rating;
                bestPatch = patch;
            }
        }

        if (bestPatch && bestRating < std::numeric_limits<float>::infinity()) {
            Ward* ward = createWard(bestPatch, type);
            bestPatch->ward = ward;

            // Remove from unassigned
            unassigned.erase(
                std::remove(unassigned.begin(), unassigned.end(), bestPatch),
                unassigned.end());
        }
    }

    // Assign remaining patches as common wards
    for (Patch* patch : unassigned) {
        WardType type = selectWardType(patch);
        Ward* ward = createWard(patch, type);
        patch->ward = ward;
    }

    // Assign farms to outer patches
    for (auto& patch : patches) {
        if (!patch->withinCity && !patch->ward) {
            Ward* ward = createWard(patch.get(), WardType::Farm);
            patch->ward = ward;
        }
    }
}

inline WardType Model::selectWardType(Patch* patch) {
    // Select appropriate ward type based on location
    float distFromCenter = Vec2::distance(patch->seed, getCenter());

    // Near center: merchants, patriciate
    if (distFromCenter < params.radius * 0.3f) {
        std::uniform_int_distribution<int> dist(0, 2);
        switch (dist(rng)) {
            case 0: return WardType::Merchants;
            case 1: return WardType::Patriciate;
            default: return WardType::Administration;
        }
    }

    // Middle: craftsmen, merchants
    if (distFromCenter < params.radius * 0.6f) {
        std::uniform_int_distribution<int> dist(0, 2);
        switch (dist(rng)) {
            case 0: return WardType::Craftsmen;
            case 1: return WardType::Merchants;
            default: return WardType::Craftsmen;
        }
    }

    // Outer: slums, craftsmen
    std::uniform_int_distribution<int> dist(0, 2);
    switch (dist(rng)) {
        case 0: return WardType::Slum;
        case 1: return WardType::Craftsmen;
        default: return WardType::Slum;
    }
}

inline Ward* Model::createWard(Patch* patch, WardType type) {
    std::unique_ptr<Ward> ward;

    switch (type) {
        case WardType::Castle:
            ward = std::make_unique<CastleWard>(this, patch);
            break;
        case WardType::Cathedral:
            ward = std::make_unique<CathedralWard>(this, patch);
            break;
        case WardType::Market:
            ward = std::make_unique<MarketWard>(this, patch);
            break;
        case WardType::Patriciate:
            ward = std::make_unique<PatriciateWard>(this, patch);
            break;
        case WardType::Craftsmen:
            ward = std::make_unique<CraftsmenWard>(this, patch);
            break;
        case WardType::Merchants:
            ward = std::make_unique<MerchantsWard>(this, patch);
            break;
        case WardType::Administration:
            ward = std::make_unique<AdministrationWard>(this, patch);
            break;
        case WardType::Military:
            ward = std::make_unique<MilitaryWard>(this, patch);
            break;
        case WardType::Slum:
            ward = std::make_unique<SlumWard>(this, patch);
            break;
        case WardType::Farm:
            ward = std::make_unique<FarmWard>(this, patch);
            break;
        case WardType::Park:
            ward = std::make_unique<ParkWard>(this, patch);
            break;
        case WardType::Gate:
            ward = std::make_unique<GateWard>(this, patch);
            break;
        default:
            ward = std::make_unique<CraftsmenWard>(this, patch);
            break;
    }

    Ward* ptr = ward.get();
    wards.push_back(std::move(ward));
    return ptr;
}

inline void Model::createGeometry() {
    // Generate building geometry for all wards
    for (auto& ward : wards) {
        ward->createGeometry(rng);
    }
}

inline std::vector<Polygon> Model::getAllBuildings() const {
    std::vector<Polygon> buildings;
    for (const auto& ward : wards) {
        buildings.insert(buildings.end(),
                        ward->geometry.begin(),
                        ward->geometry.end());
    }
    return buildings;
}

inline std::vector<Patch*> Model::getPatchesByWardType(WardType type) const {
    std::vector<Patch*> result;
    for (const auto& patch : patches) {
        if (patch->ward && patch->ward->type == type) {
            result.push_back(patch.get());
        }
    }
    return result;
}

} // namespace city
