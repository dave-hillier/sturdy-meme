#include "town_generator/wards/Market.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

geom::Polygon Market::getAvailable() {
    // Faithful to mfcg.js Market.getAvailable (lines 652-677)
    // Market fills the entire patch, only insets for canal edges
    if (!patch || !model) return patch ? patch->shape : geom::Polygon();

    size_t len = patch->shape.length();

    // Start with zero insets for all edges (market fills entire space)
    std::vector<double> edgeInsets(len, 0.0);

    // Per-vertex canal exclusions (for corner handling)
    std::vector<double> vertexExclusions(len, 0.0);

    // Check each edge for canal type
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Check if edge is on a canal - use canal width / 2
        for (const auto& canal : model->canals) {
            if (canal->containsEdge(v0, v1)) {
                edgeInsets[i] = canal->width / 2.0;
                break;
            }

            // Check canal vertex exclusions
            double canalWidth = canal->getWidthAtVertex(v0);
            if (canalWidth > 0) {
                vertexExclusions[i] = std::max(vertexExclusions[i], canalWidth / 2.0);
                // If this is the start of the canal, add extra offset
                if (!canal->course.empty() && canal->course[0] == v0) {
                    vertexExclusions[i] += ALLEY;
                }
            }
        }
    }

    // Apply vertex exclusions to adjacent edges
    for (size_t i = 0; i < len; ++i) {
        if (vertexExclusions[i] > 0) {
            edgeInsets[i] = std::max(edgeInsets[i], vertexExclusions[i]);
            size_t prevIdx = (i + len - 1) % len;
            edgeInsets[prevIdx] = std::max(edgeInsets[prevIdx], vertexExclusions[i]);
        }
    }

    return patch->shape.shrink(edgeInsets);
}

void Market::createGeometry() {
    // Faithful to mfcg.js Market.createGeometry (lines 632-651)
    if (!patch) return;

    utils::Random::reset(patch->seed);

    geometry.clear();

    // Get available space (uses zero insets, fills whole patch)
    space = getAvailable();
    if (space.length() < 3) return;

    // 60% chance for rectangular statue/monument, else octagonal fountain
    bool statue = utils::Random::boolVal(0.6);
    // If statue, always offset; otherwise 30% chance for offset
    bool offset = statue || utils::Random::boolVal(0.3);

    geom::Point c, d;  // Longest edge endpoints
    size_t longestIdx = 0;

    if (statue || offset) {
        // Find longest edge of the space (not patch->shape)
        double maxLen = -1.0;
        for (size_t i = 0; i < space.length(); ++i) {
            const geom::Point& p0 = space[i];
            const geom::Point& p1 = space[(i + 1) % space.length()];
            double len = geom::Point::distance(p0, p1);
            if (len > maxLen) {
                maxLen = len;
                longestIdx = i;
            }
        }
        c = space[longestIdx];
        d = space[(longestIdx + 1) % space.length()];
    }

    geom::Polygon monument;
    if (statue) {
        // Rectangular statue/monument aligned to longest edge
        monument = geom::Polygon::rect(
            1.0 + utils::Random::floatVal(),
            1.0 + utils::Random::floatVal()
        );
        // Rotate to align with longest edge
        geom::Point edgeDir = d.subtract(c);
        double angle = std::atan2(edgeDir.y, edgeDir.x);
        monument.rotate(angle);
    } else {
        // Octagonal fountain (regular(8, radius))
        monument = geom::Polygon::regular(8, 1.0 + utils::Random::floatVal());
    }

    // Position: at centroid of space, or offset toward longest edge
    geom::Point centroid = space.centroid();
    if (offset) {
        // Offset toward midpoint of longest edge
        geom::Point edgeMid((c.x + d.x) / 2.0, (c.y + d.y) / 2.0);
        double t = 0.2 + utils::Random::floatVal() * 0.4;
        geom::Point pos(
            centroid.x + (edgeMid.x - centroid.x) * t,
            centroid.y + (edgeMid.y - centroid.y) * t
        );
        monument.offset(pos);
    } else {
        monument.offset(centroid);
    }

    geometry.push_back(monument);
}

} // namespace wards
} // namespace town_generator
