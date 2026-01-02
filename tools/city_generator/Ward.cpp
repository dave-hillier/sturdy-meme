// Ward implementation
// Ported from watabou's Medieval Fantasy City Generator

#include "Ward.h"
#include "Model.h"
#include <cmath>

namespace city {

// Get the city block (patch shape inset by street width)
Polygon Ward::getCityBlock() const {
    if (!patch) return Polygon();

    // Determine street width based on whether edges are main streets
    // For simplicity, use regular street width for all edges
    float streetWidth = REGULAR_STREET;

    // Check if any edge borders a main street
    // TODO: Check against model streets for main street detection

    return patch->shape.inset(streetWidth);
}

void Ward::createGeometry(std::mt19937& rng) {
    geometry.clear();
    Polygon block = getCityBlock();
    if (block.area() > 0) {
        geometry.push_back(block);
    }
}

void Ward::filterOutskirts(std::mt19937& rng, float emptyProb) {
    if (!patch || !patch->withinWalls) {
        // Remove some buildings on outskirts
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // Calculate distance from city center
        Vec2 center = model ? Vec2{0, 0} : patch->seed;  // Use model center if available

        geometry.erase(
            std::remove_if(geometry.begin(), geometry.end(),
                [&](const Polygon& p) {
                    float distFromCenter = Vec2::distance(p.centroid(), center);
                    float prob = emptyProb * (1.0f + distFromCenter * 0.01f);
                    return dist(rng) < prob;
                }),
            geometry.end());
    }
}

std::pair<size_t, float> Ward::findLongestEdge(const Polygon& poly) {
    size_t bestIdx = 0;
    float maxLen = 0.0f;

    for (size_t i = 0; i < poly.size(); i++) {
        size_t j = (i + 1) % poly.size();
        float len = Vec2::distance(poly[i], poly[j]);
        if (len > maxLen) {
            maxLen = len;
            bestIdx = i;
        }
    }

    return {bestIdx, maxLen};
}

std::vector<Polygon> Ward::createAlleys(
    const Polygon& block,
    float minArea,
    float gridChaos,
    float sizeChaos,
    std::mt19937& rng)
{
    std::vector<Polygon> result;

    if (block.area() < minArea * 2.0f) {
        // Block is small enough, don't subdivide
        result.push_back(block);
        return result;
    }

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Find longest edge to split along
    auto [edgeIdx, edgeLen] = findLongestEdge(block);

    if (edgeLen < 4.0f) {
        result.push_back(block);
        return result;
    }

    // Calculate split position with some randomness
    float splitPos = 0.5f + (dist(rng) - 0.5f) * sizeChaos;
    splitPos = std::clamp(splitPos, 0.3f, 0.7f);

    // Direction along the edge
    size_t nextIdx = (edgeIdx + 1) % block.size();
    Vec2 edgeStart = block[edgeIdx];
    Vec2 edgeEnd = block[nextIdx];
    Vec2 edgeDir = (edgeEnd - edgeStart).normalized();

    // Perpendicular direction (with some chaos)
    float angle = gridChaos * (dist(rng) - 0.5f) * 0.5f;
    Vec2 perpDir = edgeDir.perpendicular().rotated(angle);

    // Find split point on the edge
    Vec2 splitPoint = Vec2::lerp(edgeStart, edgeEnd, splitPos);

    // Create a splitting line
    Segment splitLine(splitPoint - perpDir * 1000.0f, splitPoint + perpDir * 1000.0f);

    // Split the polygon
    std::vector<Vec2> poly1, poly2;
    bool inFirst = true;

    for (size_t i = 0; i < block.size(); i++) {
        size_t j = (i + 1) % block.size();
        const Vec2& v1 = block[i];
        const Vec2& v2 = block[j];

        if (inFirst) {
            poly1.push_back(v1);
        } else {
            poly2.push_back(v1);
        }

        // Check for intersection
        Segment edge(v1, v2);
        auto intersection = edge.intersect(splitLine);
        if (intersection) {
            poly1.push_back(*intersection);
            poly2.push_back(*intersection);
            inFirst = !inFirst;
        }
    }

    // Recursively subdivide both halves
    Polygon half1(poly1);
    Polygon half2(poly2);

    if (half1.area() >= minArea) {
        auto sub1 = createAlleys(half1, minArea, gridChaos, sizeChaos, rng);
        result.insert(result.end(), sub1.begin(), sub1.end());
    } else if (half1.area() > minArea * 0.5f) {
        result.push_back(half1);
    }

    if (half2.area() >= minArea) {
        auto sub2 = createAlleys(half2, minArea, gridChaos, sizeChaos, rng);
        result.insert(result.end(), sub2.begin(), sub2.end());
    } else if (half2.area() > minArea * 0.5f) {
        result.push_back(half2);
    }

    return result;
}

Polygon Ward::createOrthoBuilding(const Polygon& poly, float ratio, std::mt19937& rng) {
    if (poly.size() < 3) return poly;

    // Find bounding box and orientation
    auto [edgeIdx, edgeLen] = findLongestEdge(poly);
    size_t nextIdx = (edgeIdx + 1) % poly.size();

    Vec2 edgeDir = (poly[nextIdx] - poly[edgeIdx]).normalized();
    Vec2 perpDir = edgeDir.perpendicular();

    // Project all vertices onto edge direction
    float minProj = std::numeric_limits<float>::max();
    float maxProj = std::numeric_limits<float>::lowest();
    float minPerp = std::numeric_limits<float>::max();
    float maxPerp = std::numeric_limits<float>::lowest();

    Vec2 origin = poly.centroid();

    for (const auto& v : poly.vertices) {
        Vec2 d = v - origin;
        float proj = d.dot(edgeDir);
        float perp = d.dot(perpDir);
        minProj = std::min(minProj, proj);
        maxProj = std::max(maxProj, proj);
        minPerp = std::min(minPerp, perp);
        maxPerp = std::max(maxPerp, perp);
    }

    // Create oriented bounding box, then shrink to ratio
    float width = maxProj - minProj;
    float height = maxPerp - minPerp;

    float targetWidth = width * ratio;
    float targetHeight = height * ratio;

    Vec2 center = origin;

    return Polygon({
        center + edgeDir * (-targetWidth/2) + perpDir * (-targetHeight/2),
        center + edgeDir * (targetWidth/2) + perpDir * (-targetHeight/2),
        center + edgeDir * (targetWidth/2) + perpDir * (targetHeight/2),
        center + edgeDir * (-targetWidth/2) + perpDir * (targetHeight/2)
    });
}

float Ward::rateLocation(const Model& model, const Patch& patch, WardType type) {
    switch (type) {
        case WardType::Castle: return CastleWard::rateLocation(model, patch);
        case WardType::Cathedral: return CathedralWard::rateLocation(model, patch);
        case WardType::Market: return MarketWard::rateLocation(model, patch);
        case WardType::Patriciate: return PatriciateWard::rateLocation(model, patch);
        case WardType::Craftsmen: return CraftsmenWard::rateLocation(model, patch);
        case WardType::Merchants: return MerchantsWard::rateLocation(model, patch);
        case WardType::Administration: return AdministrationWard::rateLocation(model, patch);
        case WardType::Military: return MilitaryWard::rateLocation(model, patch);
        case WardType::Slum: return SlumWard::rateLocation(model, patch);
        case WardType::Farm: return FarmWard::rateLocation(model, patch);
        case WardType::Park: return ParkWard::rateLocation(model, patch);
        case WardType::Gate: return GateWard::rateLocation(model, patch);
        default: return 0.0f;
    }
}

// Castle implementation
void CastleWard::createGeometry(std::mt19937& rng) {
    geometry.clear();

    Polygon block = getCityBlock();
    block = block.inset(MAIN_STREET);

    if (block.area() <= 0) return;

    // Create main castle building
    float size = 4.0f * std::sqrt(block.area());
    Polygon castle = createOrthoBuilding(block, 0.6f, rng);
    geometry.push_back(castle);

    // Create curtain wall around the castle
    curtainWall = block.inset(-REGULAR_STREET * 0.5f);
}

float CastleWard::rateLocation(const Model& model, const Patch& patch) {
    // Castle should be central and large
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});
    float areaScore = 1000.0f / std::max(patch.area(), 1.0f);

    // Prefer patches within walls
    if (!patch.withinWalls && patch.withinCity) {
        return distFromCenter + areaScore + 50.0f;
    }
    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    return distFromCenter + areaScore;
}

// Cathedral implementation
void CathedralWard::createGeometry(std::mt19937& rng) {
    geometry.clear();

    Polygon block = getCityBlock();
    block = block.inset(REGULAR_STREET);

    if (block.area() <= 0) return;

    // Create cathedral as large rectangular building
    Polygon cathedral = createOrthoBuilding(block, 0.7f, rng);
    geometry.push_back(cathedral);
}

float CathedralWard::rateLocation(const Model& model, const Patch& patch) {
    // Cathedral should be large and reasonably central
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});
    float areaScore = 500.0f / std::max(patch.area(), 1.0f);

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    return distFromCenter * 0.5f + areaScore;
}

// Market implementation
void MarketWard::createGeometry(std::mt19937& rng) {
    geometry.clear();

    // Markets are mostly open space with a central feature
    Polygon block = getCityBlock();
    if (block.area() <= 0) return;

    // Create small central fountain/statue
    Vec2 center = block.centroid();
    float fountainSize = std::sqrt(block.area()) * 0.15f;
    fountain = Polygon::regular(8, fountainSize, center);

    // The market itself is the open space - no buildings
    // Just add the fountain as geometry
    geometry.push_back(fountain);
}

float MarketWard::rateLocation(const Model& model, const Patch& patch) {
    // Market should be central with good access
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});

    // Check if there's already a market
    for (const auto& p : model.patches) {
        if (p->ward && p->ward->type == WardType::Market) {
            return std::numeric_limits<float>::infinity();
        }
    }

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Prefer patches near plaza if exists
    // Otherwise prefer central location
    return distFromCenter;
}

// Common ward implementation
void CommonWard::createGeometry(std::mt19937& rng) {
    geometry.clear();

    Polygon block = getCityBlock();
    if (block.area() <= 0) return;

    // Subdivide block into building plots
    auto plots = createAlleys(block, minBuildingArea, gridChaos, sizeChaos, rng);

    // Create buildings in plots
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (const auto& plot : plots) {
        // Skip empty probability
        if (dist(rng) < emptyProb) continue;

        // Inset for building footprint
        Polygon building = plot.inset(ALLEY * 0.3f);
        if (building.area() > minBuildingArea * 0.3f) {
            geometry.push_back(building);
        }
    }

    // Filter outskirts if not fully enclosed
    if (!patch->withinWalls) {
        filterOutskirts(rng, emptyProb);
    }
}

// Patriciate location rating
float PatriciateWard::rateLocation(const Model& model, const Patch& patch) {
    // Wealthy areas near center, away from slums/craftsmen
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Penalty for being near slums
    float slumPenalty = 0.0f;
    for (const auto& neighbor : patch.neighbors) {
        if (neighbor->ward && neighbor->ward->type == WardType::Slum) {
            slumPenalty += 50.0f;
        }
    }

    return distFromCenter * 0.5f + slumPenalty;
}

// Craftsmen location rating
float CraftsmenWard::rateLocation(const Model& model, const Patch& patch) {
    // Craftsmen can be anywhere in city, slight preference for periphery
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    return -distFromCenter * 0.2f + 50.0f;  // Slight preference for outer areas
}

// Merchants location rating
float MerchantsWard::rateLocation(const Model& model, const Patch& patch) {
    // Merchants near market or main streets
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Bonus for being near market
    float marketBonus = 0.0f;
    for (const auto& neighbor : patch.neighbors) {
        if (neighbor->ward && neighbor->ward->type == WardType::Market) {
            marketBonus = -30.0f;
        }
    }

    return distFromCenter * 0.3f + marketBonus;
}

// Administration location rating
float AdministrationWard::rateLocation(const Model& model, const Patch& patch) {
    // Administration central, large buildings
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});
    float areaScore = 200.0f / std::max(patch.area(), 1.0f);

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    return distFromCenter + areaScore;
}

// Military location rating
float MilitaryWard::rateLocation(const Model& model, const Patch& patch) {
    // Military near gates or walls
    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Bonus for being near gate
    float gateBonus = 100.0f;
    for (const auto& neighbor : patch.neighbors) {
        if (!neighbor->withinCity || !neighbor->withinWalls) {
            gateBonus = 0.0f;  // Near edge
            break;
        }
    }

    return gateBonus;
}

// Slum location rating
float SlumWard::rateLocation(const Model& model, const Patch& patch) {
    // Slums on periphery
    float distFromCenter = Vec2::distance(patch.seed, Vec2{0, 0});

    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Prefer outer areas
    return -distFromCenter;
}

// Farm implementation
void FarmWard::createGeometry(std::mt19937& rng) {
    geometry.clear();

    Polygon block = getCityBlock();
    if (block.area() <= 0) return;

    // Farm has one farmhouse
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float farmhouseSize = std::sqrt(block.area()) * 0.2f;

    // Place farmhouse off-center
    Vec2 center = block.centroid();
    Vec2 offset{(dist(rng) - 0.5f) * farmhouseSize * 2,
                (dist(rng) - 0.5f) * farmhouseSize * 2};

    Polygon farmhouse = Polygon::rect(
        center.x + offset.x - farmhouseSize/2,
        center.y + offset.y - farmhouseSize/2,
        farmhouseSize, farmhouseSize * 0.7f);

    geometry.push_back(farmhouse);
}

float FarmWard::rateLocation(const Model& model, const Patch& patch) {
    // Farms outside walls
    if (patch.withinWalls) {
        return std::numeric_limits<float>::infinity();
    }
    if (!patch.withinCity) {
        return 0.0f;  // Good for farms
    }
    return 10.0f;
}

// Park implementation
void ParkWard::createGeometry(std::mt19937& rng) {
    geometry.clear();
    // Parks have no buildings - just open space
}

float ParkWard::rateLocation(const Model& model, const Patch& patch) {
    // Parks are rare, can be anywhere
    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }
    return 100.0f;  // Low priority
}

// Gate ward location rating
float GateWard::rateLocation(const Model& model, const Patch& patch) {
    // Gate wards at city entrances
    if (!patch.withinCity) {
        return std::numeric_limits<float>::infinity();
    }

    // Check if patch borders outside
    bool bordersOutside = false;
    for (const auto& neighbor : patch.neighbors) {
        if (!neighbor->withinCity) {
            bordersOutside = true;
            break;
        }
    }

    if (bordersOutside) {
        return 0.0f;
    }

    return std::numeric_limits<float>::infinity();
}

} // namespace city
