/**
 * Ported from: Source/com/watabou/towngenerator/building/Model.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../geom/Segment.hpp"
#include "../geom/Voronoi.hpp"
#include "../utils/MathUtils.hpp"
#include "../utils/Random.hpp"
#include "../geom/Graph.hpp"
#include "../building/Patch.hpp"
#include "../building/Topology.hpp"
#include "../building/CurtainWall.hpp"

// Ward includes
#include "../wards/Ward.hpp"
#include "../wards/CommonWard.hpp"
#include "../wards/Castle.hpp"
#include "../wards/Cathedral.hpp"
#include "../wards/Market.hpp"
#include "../wards/GateWard.hpp"
#include "../wards/Farm.hpp"
#include "../wards/Park.hpp"
#include "../wards/Slum.hpp"
#include "../wards/CraftsmenWard.hpp"
#include "../wards/MerchantWard.hpp"
#include "../wards/MilitaryWard.hpp"
#include "../wards/PatriciateWard.hpp"
#include "../wards/AdministrationWard.hpp"

namespace town {

// Type alias for Street
using Street = Polygon;

// Forward declaration for WardFactory
class Model;

/**
 * WardFactory - replaces Haxe's Type.createInstance and Reflect pattern
 *
 * Provides factory functions for creating wards and rating their locations.
 * This is the C++ equivalent of Haxe's runtime reflection.
 */
class WardFactory {
public:
    using CreateFunc = std::function<std::shared_ptr<Ward>(std::shared_ptr<Model>, std::shared_ptr<Patch>)>;
    using RateFunc = std::function<float(std::shared_ptr<Model>, std::shared_ptr<Patch>)>;

    struct WardType {
        std::string name;
        CreateFunc create;
        RateFunc rate;  // nullptr if no rating function
    };

    // Predefined ward types matching the original WARDS array
    static std::vector<WardType> getWardTypes() {
        return {
            // The original array from Model.hx:
            // CraftsmenWard, CraftsmenWard, MerchantWard, CraftsmenWard, CraftsmenWard, Cathedral,
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "MerchantWard", createMerchantWard, MerchantWard::rateLocation },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "Cathedral", createCathedral, Cathedral::rateLocation },

            // CraftsmenWard, CraftsmenWard, CraftsmenWard, CraftsmenWard, CraftsmenWard,
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },

            // CraftsmenWard, CraftsmenWard, CraftsmenWard, AdministrationWard, CraftsmenWard,
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "AdministrationWard", createAdministrationWard, AdministrationWard::rateLocation },
            { "CraftsmenWard", createCraftsmenWard, nullptr },

            // Slum, CraftsmenWard, Slum, PatriciateWard, Market,
            { "Slum", createSlum, Slum::rateLocation },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "Slum", createSlum, Slum::rateLocation },
            { "PatriciateWard", createPatriciateWard, PatriciateWard::rateLocation },
            { "Market", createMarket, Market::rateLocation },

            // Slum, CraftsmenWard, CraftsmenWard, CraftsmenWard, Slum,
            { "Slum", createSlum, Slum::rateLocation },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "Slum", createSlum, Slum::rateLocation },

            // CraftsmenWard, CraftsmenWard, CraftsmenWard, MilitaryWard, Slum,
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "MilitaryWard", createMilitaryWard, MilitaryWard::rateLocation },
            { "Slum", createSlum, Slum::rateLocation },

            // CraftsmenWard, Park, PatriciateWard, Market, MerchantWard
            { "CraftsmenWard", createCraftsmenWard, nullptr },
            { "Park", createPark, nullptr },
            { "PatriciateWard", createPatriciateWard, PatriciateWard::rateLocation },
            { "Market", createMarket, Market::rateLocation },
            { "MerchantWard", createMerchantWard, MerchantWard::rateLocation },
        };
    }

    // Default ward type (Slum) for when the list is exhausted
    static WardType getDefaultWardType() {
        return { "Slum", createSlum, Slum::rateLocation };
    }

private:
    // Creator functions
    static std::shared_ptr<Ward> createCraftsmenWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<CraftsmenWard>(m, p);
    }
    static std::shared_ptr<Ward> createMerchantWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<MerchantWard>(m, p);
    }
    static std::shared_ptr<Ward> createCathedral(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Cathedral>(m, p);
    }
    static std::shared_ptr<Ward> createAdministrationWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<AdministrationWard>(m, p);
    }
    static std::shared_ptr<Ward> createSlum(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Slum>(m, p);
    }
    static std::shared_ptr<Ward> createPatriciateWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<PatriciateWard>(m, p);
    }
    static std::shared_ptr<Ward> createMarket(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Market>(m, p);
    }
    static std::shared_ptr<Ward> createMilitaryWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<MilitaryWard>(m, p);
    }
    static std::shared_ptr<Ward> createPark(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Park>(m, p);
    }
    static std::shared_ptr<Ward> createGateWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<GateWard>(m, p);
    }
    static std::shared_ptr<Ward> createFarm(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Farm>(m, p);
    }
    static std::shared_ptr<Ward> createCastle(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Castle>(m, p);
    }
    static std::shared_ptr<Ward> createBaseWard(std::shared_ptr<Model> m, std::shared_ptr<Patch> p) {
        return std::make_shared<Ward>(m, p);
    }
};

class Model : public std::enable_shared_from_this<Model> {
public:
    static std::shared_ptr<Model> instance;

    // Size constants (from comments in original):
    // Small Town: 6, Large Town: 10, Small City: 15, Large City: 24, Metropolis: 40

    std::shared_ptr<Topology> topology;

    std::vector<std::shared_ptr<Patch>> patches;
    std::vector<std::shared_ptr<Patch>> waterbody;
    // For a walled city it's a list of patches within the walls,
    // for a city without walls it's just a list of all city wards
    std::vector<std::shared_ptr<Patch>> inner;
    std::shared_ptr<Patch> citadel;
    std::shared_ptr<Patch> plaza;
    Point center;

    std::shared_ptr<CurtainWall> border;
    std::shared_ptr<CurtainWall> wall;

    float cityRadius = 0.0f;

    // List of all entrances of a city including castle gates
    std::vector<PointPtr> gates;

    // Joined list of streets (inside walls) and roads (outside walls)
    // without duplicating segments
    std::vector<Street> arteries;
    std::vector<Street> streets;
    std::vector<Street> roads;

    /**
     * Factory method - the only way to create a Model.
     * Matches the original Haxe constructor: new Model(nPatches, seed)
     * With additional optional flags for plaza/citadel/walls.
     *
     * @param nPatches Number of patches (-1 for default 15)
     * @param seed Random seed (-1 for time-based)
     * @param plaza Tri-state: -1=random, 0=disabled, 1=enabled
     * @param citadel Tri-state: -1=random, 0=disabled, 1=enabled
     * @param walls Tri-state: -1=random, 0=disabled, 1=enabled
     */
    static std::shared_ptr<Model> create(int nPatches = -1, int seed = -1,
                                          int plaza = -1, int citadel = -1, int walls = -1) {
        auto model = std::shared_ptr<Model>(new Model());
        model->initWithParams(nPatches, seed, plaza, citadel, walls);
        return model;
    }

    static Polygon findCircumference(const std::vector<std::shared_ptr<Patch>>& wards) {
        if (wards.empty()) {
            return Polygon();
        }
        if (wards.size() == 1) {
            return Polygon(wards[0]->shape);
        }

        std::vector<PointPtr> A;
        std::vector<PointPtr> B;

        for (auto& w1 : wards) {
            w1->shape.forEdge([&](PointPtr a, PointPtr b) {
                bool outerEdge = true;
                for (auto& w2 : wards) {
                    if (w2->shape.findEdge(b, a) != -1) {
                        outerEdge = false;
                        break;
                    }
                }
                if (outerEdge) {
                    A.push_back(a);
                    B.push_back(b);
                }
            });
        }

        Polygon result;
        if (!A.empty()) {
            size_t index = 0;
            size_t safetyLimit = A.size() + 1;
            do {
                if (--safetyLimit == 0) break;  // Safety exit
                result.push_back(A[index]);
                // Find index of B[index] in A (by pointer identity)
                auto it = std::find(A.begin(), A.end(), B[index]);
                index = (it != A.end()) ? std::distance(A.begin(), it) : 0;
            } while (index != 0);
        }

        return result;
    }

    std::vector<std::shared_ptr<Patch>> patchByVertex(PointPtr v) {
        std::vector<std::shared_ptr<Patch>> result;
        for (auto& patch : patches) {
            if (patch->shape.contains(v)) {
                result.push_back(patch);
            }
        }
        return result;
    }

    std::shared_ptr<Patch> getNeighbour(std::shared_ptr<Patch> patch, PointPtr v) {
        PointPtr nextPt = patch->shape.next(v);
        for (auto& p : patches) {
            if (p->shape.findEdge(nextPt, v) != -1) {
                return p;
            }
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<Patch>> getNeighbours(std::shared_ptr<Patch> patch) {
        std::vector<std::shared_ptr<Patch>> result;
        for (auto& p : patches) {
            if (p != patch && p->shape.borders(patch->shape)) {
                result.push_back(p);
            }
        }
        return result;
    }

    // A ward is "enclosed" if it belongs to the city and
    // it's surrounded by city wards and water
    bool isEnclosed(std::shared_ptr<Patch> patch) {
        if (!patch->withinCity) return false;
        if (patch->withinWalls) return true;

        auto neighbours = getNeighbours(patch);
        return std::all_of(neighbours.begin(), neighbours.end(),
            [](std::shared_ptr<Patch>& p) { return p->withinCity; });
    }

private:
    int nPatches_;
    bool plazaNeeded_;
    bool citadelNeeded_;
    bool wallsNeeded_;

    // Keep Voronoi alive - it owns the Triangle circumcenters that Patches reference
    std::unique_ptr<Voronoi> voronoi_;

    // Private constructor for create() factory
    Model() : nPatches_(15), plazaNeeded_(false), citadelNeeded_(false), wallsNeeded_(false) {}

    void initWithParams(int nPatches, int seed, int plaza = -1, int citadel = -1, int walls = -1) {
        nPatches_ = nPatches != -1 ? nPatches : 15;

        if (seed > 0) {
            Random::reset(seed);
        }

        // Use provided flags or fall back to random
        plazaNeeded_ = (plaza == -1) ? Random::getBool() : (plaza == 1);
        citadelNeeded_ = (citadel == -1) ? Random::getBool() : (citadel == 1);
        wallsNeeded_ = (walls == -1) ? Random::getBool() : (walls == 1);

        bool success = false;
        int retries = 0;
        constexpr int MAX_RETRIES = 100;
        while (!success && retries < MAX_RETRIES) {
            try {
                build();
                instance = shared_from_this();
                success = true;
            } catch (const std::exception& e) {
                instance = nullptr;
                retries++;
            }
        }
        if (!success) {
            throw std::runtime_error("Failed to build model after maximum retries");
        }
    }

    void build() {
        streets.clear();
        roads.clear();

        buildPatches();
        optimizeJunctions();
        buildWalls();
        buildStreets();
        createWards();
        buildGeometry();
    }

    void buildPatches() {
        float sa = Random::getFloat() * 2.0f * static_cast<float>(M_PI);

        std::vector<Point> points;
        for (int i = 0; i < nPatches_ * 8; i++) {
            float a = sa + std::sqrt(static_cast<float>(i)) * 5.0f;
            float r = (i == 0) ? 0.0f : 10.0f + i * (2.0f + Random::getFloat());
            points.emplace_back(std::cos(a) * r, std::sin(a) * r);
        }

        voronoi_ = Voronoi::build(points);

        // Relaxing central wards
        for (int i = 0; i < 3; i++) {
            std::vector<PointPtr> toRelax;
            auto& pts = voronoi_->getPointsMutable();
            for (int j = 0; j < 3 && j < static_cast<int>(pts.size()); j++) {
                toRelax.push_back(pts[j]);
            }
            if (nPatches_ < static_cast<int>(pts.size())) {
                toRelax.push_back(pts[nPatches_]);
            }
            voronoi_ = Voronoi::relax(*voronoi_, &toRelax);
        }

        // Sort points by distance from origin
        auto& pts = voronoi_->getPointsMutable();
        std::sort(pts.begin(), pts.end(),
            [](const PointPtr& p1, const PointPtr& p2) {
                return p1->length() < p2->length();
            });

        auto regions = voronoi_->partioning();

        patches.clear();
        inner.clear();

        int count = 0;
        for (Region* r : regions) {
            auto patch = Patch::fromRegion(*r);
            patches.push_back(patch);

            if (count == 0) {
                PointPtr centerPtr = patch->shape.min([](PointPtr p) { return p ? p->length() : std::numeric_limits<float>::infinity(); });
                if (centerPtr) {
                    center = *centerPtr;
                }
                if (plazaNeeded_) {
                    plaza = patch;
                }
            } else if (count == nPatches_ && citadelNeeded_) {
                citadel = patch;
                citadel->withinCity = true;
            }

            if (count < nPatches_) {
                patch->withinCity = true;
                patch->withinWalls = wallsNeeded_;
                inner.push_back(patch);
            }

            count++;
        }
    }

    void buildWalls() {
        Polygon reserved;
        if (citadel) {
            reserved = citadel->shape.copy();
        }

        border = std::make_shared<CurtainWall>(wallsNeeded_, shared_from_this(), inner, reserved);
        if (wallsNeeded_) {
            wall = border;
            wall->buildTowers();
        }

        float radius = border->getRadius();
        patches.erase(
            std::remove_if(patches.begin(), patches.end(),
                [&](std::shared_ptr<Patch>& p) {
                    if (p->shape.empty()) return true;  // Remove empty patches
                    return p->shape.distance(center) >= radius * 3.0f;
                }),
            patches.end()
        );

        gates = border->gates;

        if (citadel) {
            auto castle = std::make_shared<Castle>(shared_from_this(), citadel);
            castle->wall->buildTowers();
            citadel->ward = castle;

            if (citadel->shape.compactness() < 0.75f) {
                throw std::runtime_error("Bad citadel shape!");
            }

            gates.insert(gates.end(), castle->wall->gates.begin(), castle->wall->gates.end());
        }
    }

    void buildStreets() {
        topology = std::make_shared<Topology>(shared_from_this());

        for (PointPtr gate : gates) {
            // Each gate is connected to the nearest corner of the plaza or to the central junction
            Point end;
            if (plaza && !plaza->shape.empty()) {
                PointPtr endPtr = plaza->shape.min([&](PointPtr v) {
                    return v ? Point::distance(*v, *gate) : std::numeric_limits<float>::infinity();
                });
                if (endPtr) {
                    end = *endPtr;
                } else {
                    end = center;
                }
            } else {
                end = center;
            }

            auto street = topology->buildPath(*gate, end, &topology->outer);
            if (street) {
                streets.push_back(*street);

                // Check if this gate is a border gate (compare by pointer identity)
                bool isBorderGate = std::find(border->gates.begin(), border->gates.end(), gate)
                    != border->gates.end();

                if (isBorderGate) {
                    Point dir = gate->norm(1000.0f);
                    PointPtr start = nullptr;
                    float dist = std::numeric_limits<float>::infinity();

                    for (auto& [node, pt] : topology->node2pt) {
                        float d = Point::distance(*pt, dir);
                        if (d < dist) {
                            dist = d;
                            start = pt;
                        }
                    }

                    if (start) {
                        auto road = topology->buildPath(*start, *gate, &topology->inner);
                        if (road) {
                            roads.push_back(*road);
                        }
                    }
                }
            } else {
                throw std::runtime_error("Unable to build a street!");
            }
        }

        tidyUpRoads();

        for (auto& a : arteries) {
            smoothStreet(a);
        }
    }

    void smoothStreet(Street& street) {
        auto smoothed = street.smoothVertexEq(3);
        for (size_t i = 1; i < street.size() - 1; i++) {
            street[i]->set(*smoothed[i]);
        }
    }

    void tidyUpRoads() {
        std::vector<Segment> segments;

        auto cut2segments = [&](Street& street) {
            if (street.size() < 2) return;

            PointPtr v1 = street[0];
            for (size_t i = 1; i < street.size(); i++) {
                PointPtr v0 = v1;
                v1 = street[i];

                // Removing segments which go along the plaza (compare by pointer identity)
                if (plaza && plaza->shape.contains(v0) && plaza->shape.contains(v1)) {
                    continue;
                }

                bool exists = false;
                for (auto& seg : segments) {
                    // Compare by pointer identity
                    if (seg.start == v0 && seg.end == v1) {
                        exists = true;
                        break;
                    }
                }

                if (!exists) {
                    segments.emplace_back(v0, v1);
                }
            }
        };

        for (auto& street : streets) {
            cut2segments(street);
        }
        for (auto& road : roads) {
            cut2segments(road);
        }

        arteries.clear();
        while (!segments.empty()) {
            Segment seg = segments.back();
            segments.pop_back();

            bool attached = false;
            for (auto& a : arteries) {
                if (!a.empty() && a[0] == seg.end) {
                    a.unshift(seg.start);
                    attached = true;
                    break;
                } else if (!a.empty() && a.last() == seg.start) {
                    a.push_back(seg.end);
                    attached = true;
                    break;
                }
            }

            if (!attached) {
                // Create a street with just two points
                Polygon newArtery;
                newArtery.push(seg.start);
                newArtery.push(seg.end);
                arteries.push_back(std::move(newArtery));
            }
        }
    }

    void optimizeJunctions() {
        std::vector<std::shared_ptr<Patch>> patchesToOptimize = inner;
        if (citadel) {
            patchesToOptimize.push_back(citadel);
        }

        std::vector<std::shared_ptr<Patch>> wards2clean;

        for (auto& w : patchesToOptimize) {
            size_t index = 0;
            while (index < w->shape.size()) {
                PointPtr v0 = w->shape[index];
                PointPtr v1 = w->shape[(index + 1) % w->shape.size()];

                // Compare by pointer identity (v0 != v1 means different objects)
                if (v0 != v1 && Point::distance(*v0, *v1) < 8.0f) {
                    auto vertPatches = patchByVertex(v1);
                    for (auto& w1 : vertPatches) {
                        if (w1 != w) {
                            int idx = w1->shape.indexOf(v1);
                            if (idx != -1) {
                                // Replace v1 with v0 in the other patch's shape
                                w1->shape.data()[idx] = v0;
                            }
                            wards2clean.push_back(w1);
                        }
                    }

                    // Merge coordinates into v0
                    v0->addEq(*v1);
                    v0->scaleEq(0.5f);

                    w->shape.remove(v1);
                }
                index++;
            }
        }

        // Removing duplicate vertices (by pointer identity)
        for (auto& w : wards2clean) {
            for (size_t i = 0; i < w->shape.size(); i++) {
                PointPtr v = w->shape[i];
                int dupIdx;
                while ((dupIdx = w->shape.indexOf(v, i + 1)) != -1) {
                    w->shape.splice(dupIdx, 1);
                }
            }
        }
    }

    void createWards() {
        std::vector<std::shared_ptr<Patch>> unassigned = inner;

        if (plaza) {
            plaza->ward = std::make_shared<Market>(shared_from_this(), plaza);
            unassigned.erase(
                std::remove(unassigned.begin(), unassigned.end(), plaza),
                unassigned.end()
            );
        }

        // Assigning inner city gate wards
        for (PointPtr gate : border->gates) {
            auto gatePatches = patchByVertex(gate);
            for (auto& patch : gatePatches) {
                if (patch->withinCity && !patch->ward) {
                    float prob = (wall == nullptr) ? 0.2f : 0.5f;
                    if (Random::getBool(prob)) {
                        patch->ward = std::make_shared<GateWard>(shared_from_this(), patch);
                        unassigned.erase(
                            std::remove(unassigned.begin(), unassigned.end(), patch),
                            unassigned.end()
                        );
                    }
                }
            }
        }

        auto wardTypes = WardFactory::getWardTypes();

        // some shuffling
        int shuffleCount = static_cast<int>(wardTypes.size()) / 10;
        for (int i = 0; i < shuffleCount; i++) {
            int index = Random::getInt(0, static_cast<int>(wardTypes.size()) - 1);
            if (index + 1 < static_cast<int>(wardTypes.size())) {
                std::swap(wardTypes[index], wardTypes[index + 1]);
            }
        }

        size_t wardIndex = 0;

        // Assigning inner city wards
        while (!unassigned.empty()) {
            std::shared_ptr<Patch> bestPatch = nullptr;

            auto wardType = (wardIndex < wardTypes.size())
                ? wardTypes[wardIndex++]
                : WardFactory::getDefaultWardType();

            if (!wardType.rate) {
                // No rating function - pick randomly
                int safetyCounter = 100;
                do {
                    size_t idx = static_cast<size_t>(Random::getFloat() * static_cast<float>(unassigned.size()));
                    bestPatch = unassigned[idx];
                } while (bestPatch->ward != nullptr && --safetyCounter > 0);
                if (safetyCounter <= 0) {
                    // Fallback: find any unassigned patch
                    bestPatch = nullptr;
                    for (auto& p : unassigned) {
                        if (!p->ward) { bestPatch = p; break; }
                    }
                }
            } else {
                // Find patch with minimum rating
                float minRate = std::numeric_limits<float>::infinity();
                for (auto& patch : unassigned) {
                    if (!patch->ward) {
                        float rate = wardType.rate(shared_from_this(), patch);
                        if (rate < minRate) {
                            minRate = rate;
                            bestPatch = patch;
                        }
                    }
                }
            }

            if (bestPatch) {
                bestPatch->ward = wardType.create(shared_from_this(), bestPatch);
                unassigned.erase(
                    std::remove(unassigned.begin(), unassigned.end(), bestPatch),
                    unassigned.end()
                );
            }
        }

        // Outskirts
        if (wall) {
            for (PointPtr gate : wall->gates) {
                float prob = 1.0f / static_cast<float>(nPatches_ - 5);
                if (!Random::getBool(prob)) {
                    auto gatePatches = patchByVertex(gate);
                    for (auto& patch : gatePatches) {
                        if (!patch->ward) {
                            patch->withinCity = true;
                            patch->ward = std::make_shared<GateWard>(shared_from_this(), patch);
                        }
                    }
                }
            }
        }

        // Calculating radius and processing countryside
        cityRadius = 0.0f;
        for (auto& patch : patches) {
            if (patch->withinCity) {
                // Radius of the city is the farthest point of all wards from the center
                for (PointPtr v : patch->shape) {
                    cityRadius = std::max(cityRadius, v->length());
                }
            } else if (!patch->ward) {
                bool makeFarm = Random::getBool(0.2f) && patch->shape.compactness() >= 0.7f;
                if (makeFarm) {
                    patch->ward = std::make_shared<Farm>(shared_from_this(), patch);
                } else {
                    patch->ward = std::make_shared<Ward>(shared_from_this(), patch);
                }
            }
        }
    }

    void buildGeometry() {
        for (auto& patch : patches) {
            if (patch->ward) {
                patch->ward->createGeometry();
            }
        }
    }
};

// Static member definition
inline std::shared_ptr<Model> Model::instance = nullptr;

// ============================================================================
// Implementation of methods that need full Model definition
// ============================================================================

// Castle constructor implementation
inline Castle::Castle(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
    : Ward(model, patch)
{
    // Filter vertices that are shared with non-city patches
    Polygon reserved;
    for (PointPtr v : patch->shape) {
        auto vertPatches = model->patchByVertex(v);
        bool hasNonCity = std::any_of(vertPatches.begin(), vertPatches.end(),
            [](std::shared_ptr<Patch>& p) { return !p->withinCity; });
        if (hasNonCity) {
            reserved.push_back(v);
        }
    }

    wall = std::make_shared<CurtainWall>(
        true,
        model,
        std::vector<std::shared_ptr<Patch>>{ patch },
        reserved
    );
}

// CommonWard::isEnclosed implementation
inline bool CommonWard::isEnclosed() {
    return model->isEnclosed(patch);
}

// Cathedral::rateLocation implementation
inline float Cathedral::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Ideally the main temple should overlook the plaza,
    // otherwise it should be as close to the plaza as possible
    if (model->plaza && patch->shape.borders(model->plaza->shape)) {
        return -1.0f / patch->shape.square();
    } else {
        Point target = model->plaza ? model->plaza->shape.center() : model->center;
        return patch->shape.distance(target) * patch->shape.square();
    }
}

// Market::rateLocation implementation
inline float Market::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // One market should not touch another
    for (auto& p : model->inner) {
        if (std::dynamic_pointer_cast<Market>(p->ward) && p->shape.borders(patch->shape)) {
            return std::numeric_limits<float>::infinity();
        }
    }

    // Market shouldn't be much larger than the plaza
    if (model->plaza) {
        return patch->shape.square() / model->plaza->shape.square();
    } else {
        return patch->shape.distance(model->center);
    }
}

// Slum::rateLocation implementation
inline float Slum::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Slums should be as far from the center as possible
    Point target = model->plaza ? model->plaza->shape.center() : model->center;
    return -patch->shape.distance(target);
}

// MerchantWard::rateLocation implementation
inline float MerchantWard::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Merchant ward should be as close to the center as possible
    Point target = model->plaza ? model->plaza->shape.center() : model->center;
    return patch->shape.distance(target);
}

// MilitaryWard::rateLocation implementation
inline float MilitaryWard::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Military ward should border the citadel or the city walls
    if (model->citadel && model->citadel->shape.borders(patch->shape)) {
        return 0.0f;
    } else if (model->wall && model->wall->borders(patch)) {
        return 1.0f;
    } else {
        return (model->citadel == nullptr && model->wall == nullptr)
            ? 0.0f
            : std::numeric_limits<float>::infinity();
    }
}

// PatriciateWard::rateLocation implementation
inline float PatriciateWard::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Patriciate ward prefers to border a park and not to border slums
    int rate = 0;
    for (auto& p : model->patches) {
        if (p->ward && p->shape.borders(patch->shape)) {
            if (std::dynamic_pointer_cast<Park>(p->ward)) {
                rate--;
            } else if (std::dynamic_pointer_cast<Slum>(p->ward)) {
                rate++;
            }
        }
    }
    return static_cast<float>(rate);
}

// AdministrationWard::rateLocation implementation
inline float AdministrationWard::rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
    // Ideally administration ward should overlook the plaza,
    // otherwise it should be as close to the plaza as possible
    if (model->plaza) {
        if (patch->shape.borders(model->plaza->shape)) {
            return 0.0f;
        } else {
            return patch->shape.distance(model->plaza->shape.center());
        }
    } else {
        return patch->shape.distance(model->center);
    }
}

// Ward::getCityBlock implementation (needs Model)
inline Polygon Ward::getCityBlock() {
    std::vector<float> insetDist;

    bool innerPatch = model->wall == nullptr || patch->withinWalls;

    patch->shape.forEdge([&](PointPtr v0, PointPtr v1) {
        if (model->wall && model->wall->bordersBy(patch, v0, v1)) {
            // Not too close to the wall
            insetDist.push_back(MAIN_STREET / 2.0f);
        } else {
            bool onStreet = false;
            if (innerPatch && model->plaza && model->plaza->shape.findEdge(v1, v0) != -1) {
                onStreet = true;
            }
            if (!onStreet) {
                for (auto& street : model->arteries) {
                    if (street.contains(v0) && street.contains(v1)) {
                        onStreet = true;
                        break;
                    }
                }
            }
            float width = onStreet ? MAIN_STREET : (innerPatch ? REGULAR_STREET : ALLEY);
            insetDist.push_back(width / 2.0f);
        }
    });

    if (patch->shape.isConvex()) {
        return patch->shape.shrink(insetDist);
    } else {
        return patch->shape.buffer(insetDist);
    }
}

// Ward::filterOutskirts implementation (needs Model)
inline void Ward::filterOutskirts() {
    struct PopulatedEdge {
        float x, y, dx, dy, d;
    };
    std::vector<PopulatedEdge> populatedEdges;

    auto addEdge = [&](PointPtr v1, PointPtr v2, float factor = 1.0f) {
        float dx = v2->x - v1->x;
        float dy = v2->y - v1->y;

        float maxDist = -std::numeric_limits<float>::infinity();
        for (PointPtr v : patch->shape) {
            float dist = 0.0f;
            // Compare by pointer identity
            if (v != v1 && v != v2) {
                dist = GeomUtils::distance2line(v1->x, v1->y, dx, dy, v->x, v->y) * factor;
            }
            if (dist > maxDist) {
                maxDist = dist;
            }
        }

        populatedEdges.push_back({ v1->x, v1->y, dx, dy, maxDist });
    };

    patch->shape.forEdge([&](PointPtr v1, PointPtr v2) {
        bool onRoad = false;
        for (auto& street : model->arteries) {
            if (street.contains(v1) && street.contains(v2)) {
                onRoad = true;
                break;
            }
        }

        if (onRoad) {
            addEdge(v1, v2, 1.0f);
        } else {
            auto n = model->getNeighbour(patch, v1);
            if (n) {
                if (n->withinCity) {
                    addEdge(v1, v2, model->isEnclosed(n) ? 1.0f : 0.4f);
                }
            }
        }
    });

    // For every vertex: if this belongs only
    // to patches within city, then 1, otherwise 0
    std::vector<float> density;
    for (PointPtr v : patch->shape) {
        // Compare by pointer identity
        bool isGate = std::find(model->gates.begin(), model->gates.end(), v)
            != model->gates.end();

        if (isGate) {
            density.push_back(1.0f);
        } else {
            auto vertPatches = model->patchByVertex(v);
            bool allWithinCity = std::all_of(vertPatches.begin(), vertPatches.end(),
                [](std::shared_ptr<Patch>& p) { return p->withinCity; });
            density.push_back(allWithinCity ? 2.0f * Random::getFloat() : 0.0f);
        }
    }

    geometry.erase(
        std::remove_if(geometry.begin(), geometry.end(),
            [&](Polygon& building) {
                float minDist = 1.0f;
                for (auto& edge : populatedEdges) {
                    for (PointPtr v : building) {
                        float d = GeomUtils::distance2line(edge.x, edge.y, edge.dx, edge.dy, v->x, v->y);
                        float dist = d / edge.d;
                        if (dist < minDist) {
                            minDist = dist;
                        }
                    }
                }

                Point c = building.center();
                auto interp = patch->shape.interpolate(c);
                float p = 0.0f;
                for (size_t j = 0; j < interp.size() && j < density.size(); j++) {
                    p += density[j] * interp[j];
                }
                minDist /= p;

                return !(Random::fuzzy(1.0f) > minDist);
            }),
        geometry.end()
    );
}

} // namespace town
