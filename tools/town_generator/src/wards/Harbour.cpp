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
    if (!patch || !model) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Find edges that border water using EdgeData
    // (faithful to mfcg.js Harbour which uses edge data for COAST detection)
    std::vector<std::pair<geom::Point, geom::Point>> waterEdges;
    size_t len = patch->shape.length();

    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Use edge data if available
        building::EdgeType edgeType = patch->getEdgeType(i);
        if (edgeType == building::EdgeType::COAST) {
            waterEdges.push_back({v0, v1});
        } else {
            // Fallback: check neighbors
            for (auto* neighbor : patch->neighbors) {
                if (neighbor->waterbody) {
                    if (neighbor->shape.contains(v0) && neighbor->shape.contains(v1)) {
                        waterEdges.push_back({v0, v1});
                        break;
                    }
                }
            }
        }
    }

    // Mark patch as landing for correct inset calculations
    patch->landing = true;

    // Find the longest water edge (faithful to mfcg.js Harbour.createGeometry)
    // mfcg.js: f = Z.max(b, function(a) { return I.distance(a.start, a.end) })
    double longestLen = 0;
    size_t longestIdx = 0;
    for (size_t i = 0; i < waterEdges.size(); ++i) {
        double edgeLen = geom::Point::distance(waterEdges[i].first, waterEdges[i].second);
        if (edgeLen > longestLen) {
            longestLen = edgeLen;
            longestIdx = i;
        }
    }

    // Create pier structures ONLY on the longest water edge
    // Faithful to mfcg.js: piers are only created on the longest water edge
    piers.clear();
    if (!waterEdges.empty() && longestLen > 0) {
        const auto& edge = waterEdges[longestIdx];

        // Number of piers = floor(edgeLength / 6) (faithful to mfcg.js: d = h / 6 | 0)
        int numPiers = static_cast<int>(longestLen / 6.0);
        if (numPiers < 1) numPiers = 1;

        geom::Point edgeDir = edge.second.subtract(edge.first);
        edgeDir = edgeDir.norm(1.0);
        geom::Point perpDir(-edgeDir.y, edgeDir.x);  // Points into water

        double spacing = longestLen / (numPiers + 1);

        for (int p = 1; p <= numPiers; ++p) {
            double t = p * spacing / longestLen;
            geom::Point pierBase = geom::GeomUtils::interpolate(edge.first, edge.second, t);

            // Pier dimensions (faithful to mfcg.js: width 1-2, length 3-8)
            double pierWidth = 1.0 + utils::Random::floatVal() * 1.0;
            double pierLength = 3.0 + utils::Random::floatVal() * 5.0;

            // Create pier polygon (rectangle extending into water)
            std::vector<geom::Point> pierVerts;
            geom::Point halfWidth = edgeDir.scale(pierWidth / 2);
            pierVerts.push_back(pierBase.subtract(halfWidth));
            pierVerts.push_back(pierBase.add(halfWidth));
            pierVerts.push_back(pierBase.add(halfWidth).add(perpDir.scale(pierLength)));
            pierVerts.push_back(pierBase.subtract(halfWidth).add(perpDir.scale(pierLength)));

            piers.push_back(geom::Polygon(pierVerts));
        }
    }

    // Create warehouses (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    params.emptyProb = 0.05;  // 5% empty lots

    createAlleys(block, params);

    // Add piers to geometry
    for (const auto& pier : piers) {
        geometry.push_back(pier);
    }

    SDL_Log("Harbour: Created %zu piers and %zu buildings", piers.size(), geometry.size() - piers.size());
}

} // namespace wards
} // namespace town_generator
