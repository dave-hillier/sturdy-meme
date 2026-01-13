#include "town_generator/wards/Harbour.h"
#include "town_generator/building/City.h"
#include "town_generator/building/Cell.h"
#include "town_generator/building/EdgeData.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace wards {

// Helper to find neighbor cell that shares an edge starting at vertex v0
// Faithful to mfcg.js model.getNeighbour(patch, f.origin)
static building::Cell* getNeighbourForEdge(building::Cell* patch, const geom::Point& v0, const geom::Point& v1) {
    if (!patch) return nullptr;

    for (auto* neighbor : patch->neighbors) {
        if (!neighbor) continue;

        // Check if neighbor shares this edge (in either direction)
        size_t nLen = neighbor->shape.length();
        for (size_t j = 0; j < nLen; ++j) {
            const geom::Point& n0 = neighbor->shape[j];
            const geom::Point& n1 = neighbor->shape[(j + 1) % nLen];

            // Edge match (either direction)
            if ((n0 == v0 && n1 == v1) || (n0 == v1 && n1 == v0)) {
                return neighbor;
            }
        }
    }
    return nullptr;
}

void Harbour::createGeometry() {
    // Faithful to mfcg.js Harbour.createGeometry (lines 12822-12865)
    // Creates piers extending INTO the water cell from edges bordering landing (land) cells
    //
    // Key insight: This ward's patch is a WATER cell. Piers extend from the
    // land/water boundary INTO the water (toward the cell's center).

    if (!patch || !model) return;

    // Collect canal start points (mfcg.js lines 12823-12828)
    std::vector<geom::Point> canalStarts;
    for (const auto& canal : model->canals) {
        if (!canal->course.empty()) {
            canalStarts.push_back(canal->course[0]);
        }
    }

    // Get water cell centroid - piers extend toward this point
    geom::Point waterCentroid = patch->shape.centroid();

    // Find edges that border landing cells (land neighbors)
    // mfcg.js lines 12829-12838: d = this.model.getNeighbour(this.patch, f.origin)
    // if (null != d && d.landing) { ... }
    std::vector<std::pair<geom::Point, geom::Point>> landingEdges;
    size_t len = patch->shape.length();

    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Find neighbor that shares this edge (faithful to getNeighbour)
        building::Cell* neighbor = getNeighbourForEdge(patch, v0, v1);

        // Check if neighbor is a landing cell (land cell bordering water)
        if (neighbor && neighbor->landing) {
            // Check for canal intersection and adjust edge if needed (mfcg.js line 12835)
            geom::Point start = v0;
            geom::Point end = v1;

            // If v0 is at a canal start, adjust start to midpoint
            for (const auto& canalStart : canalStarts) {
                if (geom::Point::distance(v0, canalStart) < 0.1) {
                    start = geom::GeomUtils::lerp(v0, v1, 0.5);
                    break;
                }
            }
            // If v1 is at a canal start, adjust end to midpoint
            for (const auto& canalStart : canalStarts) {
                if (geom::Point::distance(v1, canalStart) < 0.1) {
                    end = geom::GeomUtils::lerp(v0, v1, 0.5);
                    break;
                }
            }

            landingEdges.push_back({start, end});
        }
    }

    piers.clear();

    if (landingEdges.empty()) {
        SDL_Log("Harbour: No landing edges found (no land neighbors), created 0 piers");
        return;
    }

    // Find the longest landing edge (mfcg.js lines 12840-12843)
    double longestLen = 0;
    size_t longestIdx = 0;
    for (size_t i = 0; i < landingEdges.size(); ++i) {
        double edgeLen = geom::Point::distance(landingEdges[i].first, landingEdges[i].second);
        if (edgeLen > longestLen) {
            longestLen = edgeLen;
            longestIdx = i;
        }
    }

    // Create piers only along the longest edge (mfcg.js lines 12844-12864)
    const auto& longestEdge = landingEdges[longestIdx];
    geom::Point start = longestEdge.first;
    geom::Point end = longestEdge.second;
    double edgeLen = longestLen;

    // Number of piers = floor(edgeLen / 6) (mfcg.js line 12845)
    int numPiers = static_cast<int>(edgeLen / 6.0);
    if (numPiers < 1) {
        SDL_Log("Harbour: Edge too short for piers (%.1f units), created 0 piers", edgeLen);
        return;
    }
    // Cap at reasonable maximum to avoid excessive piers on very long edges
    if (numPiers > 20) numPiers = 20;

    // Calculate spacing (mfcg.js lines 12846-12848)
    // a = 6 * (numPiers - 1) = total pier spacing
    // k = (1 - a/edgeLen) / 2 = initial offset
    // n = a / (numPiers - 1) / edgeLen = step between piers
    double totalPierSpace = 6.0 * (numPiers - 1);
    double initialOffset = (1.0 - totalPierSpace / edgeLen) / 2.0;
    double step = (numPiers > 1) ? (totalPierSpace / (numPiers - 1) / edgeLen) : 0.0;

    geom::Point edgeDir = end.subtract(start);

    // Calculate perpendicular direction pointing INTO the water cell (toward centroid)
    // First get a candidate perpendicular (90 degree rotation)
    geom::Point perpCandidate(-edgeDir.y / edgeLen, edgeDir.x / edgeLen);

    // Test which direction points toward water cell center
    geom::Point edgeMid = geom::GeomUtils::lerp(start, end, 0.5);
    geom::Point testPoint = edgeMid.add(geom::Point(perpCandidate.x, perpCandidate.y));
    geom::Point toCenter = waterCentroid.subtract(edgeMid);

    // Dot product: if positive, perpCandidate points toward center; if negative, flip it
    double dot = perpCandidate.x * toCenter.x + perpCandidate.y * toCenter.y;
    geom::Point perpDir = (dot >= 0) ? perpCandidate : geom::Point(-perpCandidate.x, -perpCandidate.y);

    double k = initialOffset;
    for (int p = 0; p < numPiers; ++p) {
        geom::Point pierBase = geom::GeomUtils::lerp(start, end, k);

        // Pier length is 8 units (mfcg.js lines 12856-12859)
        double pierLength = 8.0;
        geom::Point pierEnd = pierBase.add(geom::Point(perpDir.x * pierLength, perpDir.y * pierLength));

        // Create thin rectangle for rendering (C++ extension for visual representation)
        double pierWidth = 1.5;
        geom::Point widthVec(edgeDir.x / edgeLen * pierWidth / 2, edgeDir.y / edgeLen * pierWidth / 2);

        std::vector<geom::Point> pierVerts;
        pierVerts.push_back(pierBase.subtract(widthVec));
        pierVerts.push_back(pierBase.add(widthVec));
        pierVerts.push_back(pierEnd.add(widthVec));
        pierVerts.push_back(pierEnd.subtract(widthVec));

        piers.push_back(geom::Polygon(pierVerts));

        k += step;
    }

    SDL_Log("Harbour: Created %zu piers along longest landing edge (%.1f units), extending into water", piers.size(), edgeLen);
}

} // namespace wards
} // namespace town_generator
