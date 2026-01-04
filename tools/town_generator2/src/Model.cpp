#include "town_generator2/building/Model.hpp"
#include "town_generator2/wards/AllWards.hpp"
#include "town_generator2/utils/MathUtils.hpp"
#include <stdexcept>
#include <iostream>
#include <set>

namespace town_generator2 {
namespace building {

Model::Model(int nPatches, int seed) {
    if (seed > 0) {
        utils::Random::reset(seed);
    }
    nPatches_ = (nPatches != -1) ? nPatches : 15;

    plazaNeeded = utils::Random::getBool();
    citadelNeeded = utils::Random::getBool();
    wallsNeeded = utils::Random::getBool();

    // Retry until successful
    for (int attempt = 0; attempt < 100; ++attempt) {
        try {
            build();
            return;  // Success
        } catch (const std::exception& e) {
            // Reset and retry
            ownedPatches_.clear();
            ownedWards_.clear();
            patches.clear();
            inner.clear();
            streets.clear();
            roads.clear();
            arteries.clear();
            gates.clear();
        }
    }
    throw std::runtime_error("Failed to generate town after 100 attempts");
}

void Model::build() {
    streets.clear();
    roads.clear();

    buildPatches();
    optimizeJunctions();
    buildWalls();
    buildStreets();
    createWards();
    buildGeometry();
}

void Model::buildPatches() {
    double sa = utils::Random::getFloat() * 2 * M_PI;

    geom::PointList points;
    for (int i = 0; i < nPatches_ * 8; ++i) {
        double a = sa + std::sqrt(i) * 5;
        double r = (i == 0) ? 0 : 10 + i * (2 + utils::Random::getFloat());
        points.push_back(geom::makePoint(std::cos(a) * r, std::sin(a) * r));
    }

    std::cerr << "[building voronoi with " << points.size() << " points]" << std::flush;
    geom::Voronoi voronoi = geom::Voronoi::build(points);
    std::cerr << "[done: points=" << voronoi.points.size()
              << " tri=" << voronoi.triangles.size() << "]" << std::flush;

    // Relax central wards
    for (int i = 0; i < 3; ++i) {
        geom::PointList toRelax;
        for (int j = 0; j < 3 && j < static_cast<int>(voronoi.points.size()); ++j) {
            toRelax.push_back(voronoi.points[j]);
        }
        if (nPatches_ < static_cast<int>(voronoi.points.size())) {
            toRelax.push_back(voronoi.points[nPatches_]);
        }
        voronoi = geom::Voronoi::relax(voronoi, &toRelax);
    }

    // Sort points by distance from origin
    std::sort(voronoi.points.begin(), voronoi.points.end(),
        [](const geom::PointPtr& p1, const geom::PointPtr& p2) {
            return p1->length() < p2->length();
        });

    std::cerr << "[voronoi after sort: points=" << voronoi.points.size()
              << " triangles=" << voronoi.triangles.size() << "]" << std::flush;

    // Check regions map
    auto& regsMap = voronoi.regions();
    std::cerr << "[regions map size=" << regsMap.size() << "]" << std::flush;

    // Debug: check how many triangles reference each of the first few points
    for (size_t i = 0; i < std::min(size_t(5), voronoi.points.size()); ++i) {
        auto& p = voronoi.points[i];
        int triCount = 0;
        for (const auto& tr : voronoi.triangles) {
            if (tr->p1 == p || tr->p2 == p || tr->p3 == p) triCount++;
        }
        // Check if in regions map
        auto it = regsMap.find(p);
        int regVerts = (it != regsMap.end()) ? it->second.vertices.size() : -1;
        std::cerr << "[pt" << i << " d=" << int(p->length()) << " t=" << triCount << " rv=" << regVerts << "]" << std::flush;
    }

    auto regions = voronoi.partioning();
    std::cerr << "[partioning returned " << regions.size() << " regions]" << std::flush;

    patches.clear();
    inner.clear();

    int count = 0;
    for (auto& r : regions) {
        std::cerr << "[region " << count << ": verts=" << r.vertices.size() << "]" << std::flush;
        auto patchPtr = Patch::fromRegion(r);
        Patch* patch = patchPtr.get();
        std::cerr << "[patch shape=" << patch->shape.length() << "]" << std::flush;
        ownedPatches_.push_back(std::move(patchPtr));
        patches.push_back(patch);

        if (count == 0) {
            center = patch->shape.min([](const geom::Point& p) { return p.length(); });
            if (plazaNeeded) {
                plaza = patch;
            }
        } else if (count == nPatches_ && citadelNeeded) {
            citadel = patch;
            citadel->withinCity = true;
        }

        if (count < nPatches_) {
            patch->withinCity = true;
            patch->withinWalls = wallsNeeded;
            inner.push_back(patch);
        }

        count++;
    }
}

void Model::optimizeJunctions() {
    std::vector<Patch*> patchesToOptimize = inner;
    if (citadel) {
        patchesToOptimize.push_back(citadel);
    }

    std::vector<Patch*> wards2clean;
    for (auto* w : patchesToOptimize) {
        size_t index = 0;
        while (index < w->shape.length()) {
            geom::PointPtr v0 = w->shape.ptr(index);
            geom::PointPtr v1 = w->shape.ptr((index + 1) % w->shape.length());

            if (v0 != v1 && geom::Point::distance(*v0, *v1) < 8) {
                // Update all patches containing v1 to reference v0
                for (auto* w1 : patchByVertex(v1)) {
                    if (w1 != w) {
                        int idx = w1->shape.indexOf(v1);
                        if (idx != -1) {
                            w1->shape.ptr(idx) = v0;
                            wards2clean.push_back(w1);
                        }
                    }
                }

                // Average the two points
                v0->addEq(*v1);
                v0->scaleEq(0.5);

                // Remove v1 from this patch
                w->shape.remove(v1);
            }
            index++;
        }
    }

    // Remove duplicate vertices
    for (auto* w : wards2clean) {
        for (size_t i = 0; i < w->shape.length(); ++i) {
            geom::PointPtr v = w->shape.ptr(i);
            for (size_t j = i + 1; j < w->shape.length();) {
                if (w->shape.ptr(j) == v) {
                    w->shape.splice(j, 1);
                } else {
                    j++;
                }
            }
        }
    }
}

void Model::buildWalls() {
    geom::PointList reserved;
    if (citadel) {
        for (const auto& v : citadel->shape) {
            reserved.push_back(v);
        }
    }

    border = std::make_unique<CurtainWall>(wallsNeeded, *this, inner, reserved);
    if (wallsNeeded) {
        wall = border.get();
        wall->buildTowers();
    }

    double radius = border->getRadius();
    std::vector<Patch*> filteredPatches;
    for (auto* p : patches) {
        if (center && p->shape.distance(*center) < radius * 3) {
            filteredPatches.push_back(p);
        }
    }
    patches = filteredPatches;

    gates = border->gates;

    if (citadel) {
        auto castle = std::make_unique<wards::Castle>(this, citadel);
        castle->wall->buildTowers();
        citadel->ward = castle.get();

        if (citadel->shape.compactness() < 0.75) {
            throw std::runtime_error("Bad citadel shape!");
        }

        // Add castle gates to global gates
        for (const auto& g : castle->wall->gates) {
            gates.push_back(g);
        }

        ownedWards_.push_back(std::move(castle));
    }
}

void Model::buildStreets() {
    auto smoothStreet = [](Street& street) {
        geom::Polygon smoothed = street.smoothVertexEq(3);
        for (size_t i = 1; i + 1 < street.length(); ++i) {
            street[i].set(smoothed[i]);
        }
    };

    topology = std::make_unique<Topology>(*this);

    for (const auto& gate : gates) {
        // Each gate connects to plaza or center
        geom::PointPtr end;
        if (plaza) {
            end = plaza->shape.min([&gate](const geom::Point& v) {
                return geom::Point::distance(v, *gate);
            });
        } else {
            end = center;
        }

        auto street = topology->buildPath(gate, end, &topology->outer);
        if (!street.empty()) {
            geom::Polygon streetPoly(street);
            streets.push_back(streetPoly);

            // Check if this is a border gate
            bool isBorderGate = false;
            for (const auto& bg : border->gates) {
                if (bg == gate) { isBorderGate = true; break; }
            }

            if (isBorderGate) {
                // Find farthest outer point in gate direction
                geom::Point dir = gate->norm(1000);
                geom::PointPtr start;
                double dist = std::numeric_limits<double>::infinity();

                for (const auto& [pt, node] : topology->pt2node) {
                    double d = geom::Point::distance(*pt, dir);
                    if (d < dist) {
                        dist = d;
                        start = pt;
                    }
                }

                if (start) {
                    auto road = topology->buildPath(start, gate, &topology->inner);
                    if (!road.empty()) {
                        roads.push_back(geom::Polygon(road));
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

void Model::tidyUpRoads() {
    struct Segment {
        geom::PointPtr start, end;
    };
    std::vector<Segment> segments;

    auto cut2segments = [&](const Street& street) {
        for (size_t i = 1; i < street.length(); ++i) {
            geom::PointPtr v0 = street.ptr(i - 1);
            geom::PointPtr v1 = street.ptr(i);

            // Skip segments along plaza
            if (plaza && plaza->shape.contains(v0) && plaza->shape.contains(v1)) {
                continue;
            }

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

    for (const auto& street : streets) {
        cut2segments(street);
    }
    for (const auto& road : roads) {
        cut2segments(road);
    }

    arteries.clear();
    while (!segments.empty()) {
        auto seg = segments.back();
        segments.pop_back();

        bool attached = false;
        for (auto& a : arteries) {
            if (a.ptr(0) == seg.end) {
                // Prepend
                a.vertices().insert(a.vertices().begin(), seg.start);
                attached = true;
                break;
            } else if (a.lastPtr() == seg.start) {
                // Append
                a.push(seg.end);
                attached = true;
                break;
            }
        }

        if (!attached) {
            geom::Polygon newArtery;
            newArtery.push(seg.start);
            newArtery.push(seg.end);
            arteries.push_back(newArtery);
        }
    }
}

void Model::createWards() {
    std::vector<Patch*> unassigned = inner;

    if (plaza) {
        auto market = std::make_unique<wards::Market>(this, plaza);
        plaza->ward = market.get();
        ownedWards_.push_back(std::move(market));
        unassigned.erase(std::find(unassigned.begin(), unassigned.end(), plaza));
    }

    // Assign inner city gate wards
    for (const auto& gate : border->gates) {
        for (auto* patch : patchByVertex(gate)) {
            if (patch->withinCity && !patch->ward) {
                double chance = (wall == nullptr) ? 0.2 : 0.5;
                if (utils::Random::getBool(chance)) {
                    auto gateWard = std::make_unique<wards::GateWard>(this, patch);
                    patch->ward = gateWard.get();
                    ownedWards_.push_back(std::move(gateWard));
                    unassigned.erase(std::find(unassigned.begin(), unassigned.end(), patch));
                }
            }
        }
    }

    // Ward type distribution (simplified)
    std::vector<int> wardTypes;
    // 0=CraftsmenWard, 1=MerchantWard, 2=Cathedral, 3=AdministrationWard, 4=Slum, 5=PatriciateWard, 6=Market, 7=MilitaryWard, 8=Park
    for (int i = 0; i < 35; ++i) {
        if (i == 2) wardTypes.push_back(2);  // Cathedral
        else if (i == 5) wardTypes.push_back(2);  // Another Cathedral slot
        else if (i == 14) wardTypes.push_back(3);  // AdministrationWard
        else if (i == 16 || i == 18 || i == 24 || i == 25 || i == 30) wardTypes.push_back(4);  // Slum
        else if (i == 19 || i == 32) wardTypes.push_back(5);  // PatriciateWard
        else if (i == 20 || i == 33) wardTypes.push_back(6);  // Market
        else if (i == 1 || i == 34) wardTypes.push_back(1);  // MerchantWard
        else if (i == 29) wardTypes.push_back(7);  // MilitaryWard
        else if (i == 31) wardTypes.push_back(8);  // Park
        else wardTypes.push_back(0);  // CraftsmenWard
    }

    // Some shuffling
    for (size_t i = 0; i < wardTypes.size() / 10; ++i) {
        size_t idx = utils::Random::getInt(0, wardTypes.size() - 1);
        std::swap(wardTypes[idx], wardTypes[idx + 1]);
    }

    size_t wardIdx = 0;
    while (!unassigned.empty()) {
        int wardType = (wardIdx < wardTypes.size()) ? wardTypes[wardIdx++] : 4;  // Default to Slum

        Patch* bestPatch = nullptr;
        double bestRate = std::numeric_limits<double>::infinity();

        for (auto* patch : unassigned) {
            if (patch->ward) continue;

            double rate = 0;
            switch (wardType) {
                case 1: rate = wards::MerchantWard::rateLocation(this, patch); break;
                case 2: rate = wards::Cathedral::rateLocation(this, patch); break;
                case 3: rate = wards::AdministrationWard::rateLocation(this, patch); break;
                case 4: rate = wards::Slum::rateLocation(this, patch); break;
                case 5: rate = wards::PatriciateWard::rateLocation(this, patch); break;
                case 6: rate = wards::Market::rateLocation(this, patch); break;
                case 7: rate = wards::MilitaryWard::rateLocation(this, patch); break;
                default: rate = utils::Random::getFloat(); break;
            }

            if (rate < bestRate) {
                bestRate = rate;
                bestPatch = patch;
            }
        }

        if (!bestPatch) {
            bestPatch = unassigned[utils::Random::getInt(0, unassigned.size())];
        }

        std::unique_ptr<wards::Ward> ward;
        switch (wardType) {
            case 1: ward = std::make_unique<wards::MerchantWard>(this, bestPatch); break;
            case 2: ward = std::make_unique<wards::Cathedral>(this, bestPatch); break;
            case 3: ward = std::make_unique<wards::AdministrationWard>(this, bestPatch); break;
            case 4: ward = std::make_unique<wards::Slum>(this, bestPatch); break;
            case 5: ward = std::make_unique<wards::PatriciateWard>(this, bestPatch); break;
            case 6: ward = std::make_unique<wards::Market>(this, bestPatch); break;
            case 7: ward = std::make_unique<wards::MilitaryWard>(this, bestPatch); break;
            case 8: ward = std::make_unique<wards::Park>(this, bestPatch); break;
            default: ward = std::make_unique<wards::CraftsmenWard>(this, bestPatch); break;
        }

        bestPatch->ward = ward.get();
        ownedWards_.push_back(std::move(ward));
        unassigned.erase(std::find(unassigned.begin(), unassigned.end(), bestPatch));
    }

    // Outskirts
    if (wall) {
        for (const auto& gate : wall->gates) {
            if (utils::Random::getBool(1.0 / (nPatches_ - 5))) continue;

            for (auto* patch : patchByVertex(gate)) {
                if (!patch->ward) {
                    patch->withinCity = true;
                    auto gateWard = std::make_unique<wards::GateWard>(this, patch);
                    patch->ward = gateWard.get();
                    ownedWards_.push_back(std::move(gateWard));
                }
            }
        }
    }

    // Calculate city radius and process countryside
    cityRadius = 0;
    for (auto* patch : patches) {
        if (patch->withinCity) {
            for (const auto& v : patch->shape) {
                cityRadius = std::max(cityRadius, v->length());
            }
        } else if (!patch->ward) {
            std::unique_ptr<wards::Ward> ward;
            if (utils::Random::getBool(0.2) && patch->shape.compactness() >= 0.7) {
                ward = std::make_unique<wards::Farm>(this, patch);
            } else {
                ward = std::make_unique<wards::Ward>(this, patch);
            }
            patch->ward = ward.get();
            ownedWards_.push_back(std::move(ward));
        }
    }
}

void Model::buildGeometry() {
    for (auto* patch : patches) {
        if (patch->ward) {
            patch->ward->createGeometry();
        }
    }
}

std::vector<Patch*> Model::patchByVertex(const geom::PointPtr& v) {
    std::vector<Patch*> result;
    for (auto* patch : patches) {
        if (patch->shape.contains(v)) {
            result.push_back(patch);
        }
    }
    return result;
}

geom::Polygon Model::findCircumference(const std::vector<Patch*>& wards) {
    if (wards.empty()) return geom::Polygon();
    if (wards.size() == 1) return wards[0]->shape.copy();

    geom::PointList A, B;

    // DEBUG: Check vertex sharing
    std::set<geom::PointPtr> allVertices;
    std::set<geom::PointPtr> sharedVertices;
    size_t totalVertCount = 0;
    for (auto* w : wards) {
        totalVertCount += w->shape.length();
        for (size_t i = 0; i < w->shape.length(); ++i) {
            auto v = w->shape.ptr(i);
            if (allVertices.count(v)) {
                sharedVertices.insert(v);
            }
            allVertices.insert(v);
        }
    }
    std::cerr << "[findCirc: wards=" << wards.size()
              << " totalVertCount=" << totalVertCount
              << " uniqueVerts=" << allVertices.size()
              << " sharedVerts=" << sharedVertices.size() << "]" << std::flush;

    for (auto* w1 : wards) {
        w1->shape.forEdgePtr([&](const geom::PointPtr& a, const geom::PointPtr& b) {
            bool outerEdge = true;
            for (auto* w2 : wards) {
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

    std::cerr << "[outerEdges=" << A.size() << "]" << std::flush;

    if (A.empty()) {
        return geom::Polygon();
    }

    geom::Polygon result;
    size_t index = 0;
    size_t startIndex = 0;
    size_t iterations = 0;
    size_t maxIterations = A.size() + 1;

    do {
        result.push(A[index]);
        // Find where B[index] appears in A
        bool found = false;
        for (size_t i = 0; i < A.size(); ++i) {
            if (A[i] == B[index]) {
                index = i;
                found = true;
                break;
            }
        }
        if (!found) {
            // Boundary not closed
            std::cerr << "[BREAK: B[" << index << "] not found in A]" << std::flush;
            break;
        }
        iterations++;
        if (iterations > maxIterations) {
            // Safety: prevent infinite loop
            break;
        }
    } while (index != startIndex);

    std::cerr << "[result=" << result.length() << "]" << std::flush;
    return result;
}

Patch* Model::getNeighbour(Patch* patch, const geom::PointPtr& v) {
    geom::PointPtr next = patch->shape.next(v);
    for (auto* p : patches) {
        if (p->shape.findEdge(next, v) != -1) {
            return p;
        }
    }
    return nullptr;
}

std::vector<Patch*> Model::getNeighbours(Patch* patch) {
    std::vector<Patch*> result;
    for (auto* p : patches) {
        if (p != patch && p->shape.borders(patch->shape)) {
            result.push_back(p);
        }
    }
    return result;
}

bool Model::isEnclosed(Patch* patch) {
    if (!patch->withinCity) return false;
    if (patch->withinWalls) return true;

    for (auto* n : getNeighbours(patch)) {
        if (!n->withinCity) return false;
    }
    return true;
}

} // namespace building
} // namespace town_generator2
