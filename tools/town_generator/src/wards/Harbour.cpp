#include "town_generator/wards/Harbour.h"
#include "town_generator/building/City.h"
#include "town_generator/building/Cell.h"
#include "town_generator/building/EdgeData.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace wards {

void Harbour::createGeometry() {
    // Faithful to mfcg.js Harbour.createGeometry (lines 12822-12865)
    // Creates piers along the longest water edge

    if (!patch || !model) return;

    // Mark patch as landing for correct inset calculations
    patch->landing = true;

    // Collect canal start points (mfcg.js lines 12823-12828)
    std::vector<geom::Point> canalStarts;
    for (const auto& canal : model->canals) {
        if (!canal->course.empty()) {
            canalStarts.push_back(canal->course[0]);
        }
    }

    // Find edges that border water (landing neighbors)
    // mfcg.js lines 12829-12838
    std::vector<std::pair<geom::Point, geom::Point>> waterEdges;
    size_t len = patch->shape.length();

    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Check if neighbor is a landing (water edge)
        building::EdgeType edgeType = patch->getEdgeType(i);
        if (edgeType == building::EdgeType::COAST) {
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

            waterEdges.push_back({start, end});
        }
    }

    piers.clear();

    if (waterEdges.empty()) {
        SDL_Log("Harbour: No water edges found, created 0 piers");
        return;
    }

    // Find the longest water edge (mfcg.js lines 12840-12843)
    double longestLen = 0;
    size_t longestIdx = 0;
    for (size_t i = 0; i < waterEdges.size(); ++i) {
        double edgeLen = geom::Point::distance(waterEdges[i].first, waterEdges[i].second);
        if (edgeLen > longestLen) {
            longestLen = edgeLen;
            longestIdx = i;
        }
    }

    // Create piers only along the longest edge (mfcg.js lines 12844-12864)
    const auto& longestEdge = waterEdges[longestIdx];
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
    geom::Point perpDir(-edgeDir.y / edgeLen, edgeDir.x / edgeLen);  // Normalized perpendicular (into water)

    double k = initialOffset;
    for (int p = 0; p < numPiers; ++p) {
        geom::Point pierBase = geom::GeomUtils::lerp(start, end, k);

        // Pier length is 8 units (mfcg.js lines 12856-12859)
        double pierLength = 8.0;
        geom::Point pierEnd = pierBase.add(geom::Point(perpDir.x * pierLength, perpDir.y * pierLength));

        // Create thin rectangle for rendering
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

    SDL_Log("Harbour: Created %zu piers along longest edge (%.1f units)", piers.size(), edgeLen);
}

} // namespace wards
} // namespace town_generator
