#include "town_generator/wards/Farm.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"
#include <cmath>
#include <algorithm>

namespace town_generator {
namespace wards {

geom::Polygon Farm::getAvailable() {
    if (!patch || !model) return {};

    // Farm-specific getAvailable (mfcg.js lines 12615-12659)
    // Farms use smaller insets when bordering other farms
    size_t len = patch->shape.length();
    std::vector<double> insetDistances(len, 0.0);
    std::vector<double> towerDistances(len, 0.0);

    // Per-vertex tower radius exclusions
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v = patch->shape[i];

        // Check tower radius from walls
        if (model->wall) {
            double towerRadius = model->wall->getTowerRadius(v);
            if (towerRadius > 0) {
                towerDistances[i] = towerRadius;
            }
        }
        if (model->citadel) {
            double towerRadius = model->citadel->getTowerRadius(v);
            if (towerRadius > 0) {
                towerDistances[i] = std::max(towerDistances[i], towerRadius);
            }
        }

        // Check canal width at vertex
        for (const auto& canal : model->canals) {
            double canalWidth = canal->getWidthAtVertex(v);
            if (canalWidth > 0) {
                double canalInset = canalWidth / 2.0 + ALLEY;
                towerDistances[i] = std::max(towerDistances[i], canalInset);
            }
        }
    }

    // Per-edge insets based on what borders the edge
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Check if edge is on canal
        for (const auto& canal : model->canals) {
            if (canal->containsEdge(v0, v1)) {
                double canalInset = canal->width / 2.0 + ALLEY;
                insetDistances[i] = std::max(insetDistances[i], canalInset);
                break;
            }
        }

        // Check neighboring cells - if neighbor is also Farm, use smaller inset (1.0)
        // If neighbor is other ward type, use larger inset (2.0)
        for (const auto* neighbor : patch->neighbors) {
            if (!neighbor) continue;

            // Check if neighbor shares this edge
            bool sharesEdge = false;
            for (size_t j = 0; j < neighbor->shape.length(); ++j) {
                const geom::Point& n0 = neighbor->shape[j];
                const geom::Point& n1 = neighbor->shape[(j + 1) % neighbor->shape.length()];
                if ((n0 == v0 && n1 == v1) || (n0 == v1 && n1 == v0)) {
                    sharesEdge = true;
                    break;
                }
            }

            if (sharesEdge) {
                // Check neighbor ward type (faithful to mfcg.js line 12641)
                if (neighbor->ward) {
                    Farm* farmNeighbor = dynamic_cast<Farm*>(neighbor->ward);
                    if (farmNeighbor) {
                        // Neighbor is also a Farm - minimal inset
                        insetDistances[i] = std::max(insetDistances[i], 1.0);
                    } else {
                        // Neighbor is other ward type - larger inset
                        insetDistances[i] = std::max(insetDistances[i], 2.0);
                    }
                } else {
                    // No ward assigned yet - use minimal inset
                    insetDistances[i] = std::max(insetDistances[i], 1.0);
                }
                break;
            }
        }
    }

    return patch->shape.shrink(insetDistances);
}

std::vector<geom::Polygon> Farm::splitField(const geom::Polygon& field) {
    // Faithful to mfcg.js splitField (lines 12709-12731)
    double area = std::abs(field.square());

    // Normal distribution approximation: (4 randoms / 2) - 1 gives range roughly -1 to 1
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    double threshold = MIN_SUBPLOT * (1.0 + std::abs(normal4));

    if (area < threshold) {
        return {field};
    }

    // Get OBB to find longest axis
    auto obb = field.orientedBoundingBox();
    if (obb.size() < 4) {
        return {field};
    }

    // Find which axis is longer
    double len01 = geom::Point::distance(obb[0], obb[1]);
    double len12 = geom::Point::distance(obb[1], obb[2]);
    int longAxis = (len01 > len12) ? 0 : 1;

    // Cut ratio with some randomness
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal()) / 3.0;
    double cutRatio = 0.5 + 0.2 * (2.0 * normal3 - 1.0);

    // Cut direction - mostly perpendicular, sometimes angled
    double angleVariation = 0.0;
    if (utils::Random::boolVal(0.5)) {
        double normal3b = (utils::Random::floatVal() + utils::Random::floatVal() +
                          utils::Random::floatVal()) / 3.0;
        angleVariation = M_PI / 8.0 * (2.0 * normal3b - 1.0);
    }

    // Calculate cut line
    geom::Point axis = obb[(longAxis + 1) % 4].subtract(obb[longAxis]);
    geom::Point cutPoint(
        obb[longAxis].x + axis.x * cutRatio,
        obb[longAxis].y + axis.y * cutRatio
    );

    // Perpendicular direction with optional angle variation
    geom::Point perpendicular(-axis.y, axis.x);
    if (std::abs(angleVariation) > 0.001) {
        double cs = std::cos(angleVariation);
        double sn = std::sin(angleVariation);
        perpendicular = geom::Point(
            perpendicular.x * cs - perpendicular.y * sn,
            perpendicular.x * sn + perpendicular.y * cs
        );
    }

    geom::Point cutEnd = cutPoint.add(perpendicular);
    auto halves = field.cut(cutPoint, cutEnd, 2.0);  // 2.0 gap for farm paths

    if (halves.size() < 2) {
        return {field};
    }

    // Recursively split each half
    std::vector<geom::Polygon> result;
    for (const auto& half : halves) {
        auto subFields = splitField(half);
        result.insert(result.end(), subFields.begin(), subFields.end());
    }

    return result;
}

geom::Polygon Farm::roundCorners(const geom::Polygon& subplot) {
    // Faithful to mfcg.js round() - rounds sharp corners (lines 12733-12741)
    std::vector<geom::Point> result;
    size_t n = subplot.length();

    for (size_t i = 0; i < n; ++i) {
        const geom::Point& curr = subplot[i];
        const geom::Point& next = subplot[(i + 1) % n];
        double dist = geom::Point::distance(curr, next);

        if (dist < 2 * MIN_FURROW) {
            // Short edge - just add midpoint
            result.push_back(geom::Point(
                (curr.x + next.x) / 2,
                (curr.y + next.y) / 2
            ));
        } else {
            // Long edge - add two points offset from corners
            double t = MIN_FURROW / dist;
            result.push_back(geom::Point(
                curr.x + (next.x - curr.x) * t,
                curr.y + (next.y - curr.y) * t
            ));
            result.push_back(geom::Point(
                next.x - (next.x - curr.x) * t,
                next.y - (next.y - curr.y) * t
            ));
        }
    }

    return geom::Polygon(result);
}

bool Farm::edgeTouchesNonFarm(const geom::Point& v0, const geom::Point& v1) {
    // Check if this edge borders a non-Farm ward (mfcg.js lines 12670-12697)
    if (!patch) return false;

    for (const auto* neighbor : patch->neighbors) {
        if (!neighbor || !neighbor->ward) continue;

        // Check if neighbor shares this edge
        for (size_t j = 0; j < neighbor->shape.length(); ++j) {
            const geom::Point& n0 = neighbor->shape[j];
            const geom::Point& n1 = neighbor->shape[(j + 1) % neighbor->shape.length()];
            if ((n0 == v0 && n1 == v1) || (n0 == v1 && n1 == v0)) {
                // Neighbor is not a Farm
                Farm* farmNeighbor = dynamic_cast<Farm*>(neighbor->ward);
                if (!farmNeighbor) {
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

// Create an L-shaped or T-shaped building (simpler than full cell growth)
static std::vector<geom::Point> createLShapedBuilding(
    const geom::Point& pos,
    const geom::Point& edgeDir,
    const geom::Point& perpDir,
    double width,
    double height
) {
    std::vector<geom::Point> points;

    // Randomly choose building shape
    double shapeChoice = utils::Random::floatVal();

    if (shapeChoice < 0.4) {
        // Simple rectangle (40% chance)
        points = {
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(height/2))
        };
    } else if (shapeChoice < 0.7) {
        // L-shape (30% chance)
        double cutW = width * (0.3 + utils::Random::floatVal() * 0.3);  // 30-60% cut
        double cutH = height * (0.3 + utils::Random::floatVal() * 0.3);

        // L-shape with corner cut out
        points = {
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(height/2 - cutH)),
            pos.add(edgeDir.scale(width/2 - cutW)).add(perpDir.scale(height/2 - cutH)),
            pos.add(edgeDir.scale(width/2 - cutW)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(height/2))
        };
    } else if (shapeChoice < 0.85) {
        // T-shape (15% chance)
        double stemW = width * 0.4;
        double stemH = height * 0.4;

        points = {
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(-height/2 + stemH)),
            pos.add(edgeDir.scale(stemW/2)).add(perpDir.scale(-height/2 + stemH)),
            pos.add(edgeDir.scale(stemW/2)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(-stemW/2)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(-stemW/2)).add(perpDir.scale(-height/2 + stemH)),
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(-height/2 + stemH))
        };
    } else {
        // U-shape (15% chance)
        double gapW = width * 0.3;
        double gapH = height * 0.5;

        points = {
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(-height/2)),
            pos.add(edgeDir.scale(width/2)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(width/2 - gapW)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(width/2 - gapW)).add(perpDir.scale(-height/2 + gapH)),
            pos.add(edgeDir.scale(-width/2 + gapW)).add(perpDir.scale(-height/2 + gapH)),
            pos.add(edgeDir.scale(-width/2 + gapW)).add(perpDir.scale(height/2)),
            pos.add(edgeDir.scale(-width/2)).add(perpDir.scale(height/2))
        };
    }

    return points;
}

geom::Polygon Farm::createHousing(const geom::Polygon& subplot) {
    // Faithful to mfcg.js getHousing (lines 12743-12764)
    // Creates a farmhouse building along the longest edge of the subplot

    if (subplot.length() < 3) return {};

    // Building dimensions - vary for organic look
    double width = 4.0 + utils::Random::floatVal() * 3.0;   // 4-7 range
    double height = 2.5 + utils::Random::floatVal() * 2.0;  // 2.5-4.5 range

    // Find longest edge
    size_t longestIdx = 0;
    double longestLen = 0;
    for (size_t i = 0; i < subplot.length(); ++i) {
        const geom::Point& p0 = subplot[i];
        const geom::Point& p1 = subplot[(i + 1) % subplot.length()];
        double len = geom::Point::distance(p0, p1);
        if (len > longestLen) {
            longestLen = len;
            longestIdx = i;
        }
    }

    const geom::Point& edgeStart = subplot[longestIdx];
    const geom::Point& edgeEnd = subplot[(longestIdx + 1) % subplot.length()];

    // Edge direction (normalized)
    geom::Point edgeDir = edgeEnd.subtract(edgeStart);
    double edgeLen = edgeDir.length();
    if (edgeLen < 0.01) return {};
    edgeDir = edgeDir.scale(1.0 / edgeLen);

    // Position along edge (randomly near start or end)
    geom::Point buildingPos;
    if (utils::Random::boolVal(0.5)) {
        buildingPos = edgeStart.add(edgeDir.scale(width / 2 + 1.0));
    } else {
        buildingPos = edgeEnd.subtract(edgeDir.scale(width / 2 + 1.0));
    }

    // Perpendicular direction (into the subplot)
    geom::Point perpDir(-edgeDir.y, edgeDir.x);
    buildingPos = buildingPos.add(perpDir.scale(height / 2 + 0.5));

    // Create L-shaped, T-shaped, U-shaped, or rectangular building
    std::vector<geom::Point> buildingPoints = createLShapedBuilding(
        buildingPos, edgeDir, perpDir, width, height
    );

    return geom::Polygon(buildingPoints);
}

void Farm::createGeometry() {
    if (!patch || !model) return;

    // Get available area (inset from roads, walls, etc.)
    geom::Polygon available = getAvailable();
    if (available.length() < 3) return;

    // Clear previous data
    subPlots.clear();
    furrows.clear();
    farmBuildings.clear();
    geometry.clear();

    // Split field into subplots
    subPlots = splitField(available);

    // Filter subplots that touch non-Farm wards (mfcg.js lines 12672-12697)
    std::vector<geom::Polygon> filteredSubplots;
    for (const auto& subplot : subPlots) {
        bool touchesNonFarm = false;

        // Check each edge of the subplot
        for (size_t i = 0; i < subplot.length() && !touchesNonFarm; ++i) {
            const geom::Point& p0 = subplot[i];
            const geom::Point& p1 = subplot[(i + 1) % subplot.length()];

            // Check if this subplot edge coincides with any patch edge that borders non-Farm
            for (size_t j = 0; j < patch->shape.length(); ++j) {
                const geom::Point& v0 = patch->shape[j];
                const geom::Point& v1 = patch->shape[(j + 1) % patch->shape.length()];

                // Check if subplot edge overlaps with patch edge
                // Use distance-based check for approximate match
                double d0 = geom::Point::distance(p0, v0) + geom::Point::distance(p1, v1);
                double d1 = geom::Point::distance(p0, v1) + geom::Point::distance(p1, v0);

                if (d0 < 0.5 || d1 < 0.5) {
                    // This subplot edge matches a patch edge
                    if (edgeTouchesNonFarm(v0, v1)) {
                        touchesNonFarm = true;
                        break;
                    }
                }
            }
        }

        if (!touchesNonFarm) {
            filteredSubplots.push_back(subplot);
        }
    }
    subPlots = filteredSubplots;

    // Round corners and generate furrows for each subplot
    for (size_t i = 0; i < subPlots.size(); ++i) {
        geom::Polygon rounded = roundCorners(subPlots[i]);
        subPlots[i] = rounded;

        // Generate furrows (visual detail)
        auto obb = subPlots[i].orientedBoundingBox();
        if (obb.size() >= 4) {
            double len01 = geom::Point::distance(obb[0], obb[1]);
            int numFurrows = static_cast<int>(std::ceil(len01 / MIN_FURROW));

            for (int f = 0; f < numFurrows; ++f) {
                double t = (f + 0.5) / numFurrows;

                geom::Point start(
                    obb[0].x + (obb[1].x - obb[0].x) * t,
                    obb[0].y + (obb[1].y - obb[0].y) * t
                );
                geom::Point end(
                    obb[3].x + (obb[2].x - obb[3].x) * t,
                    obb[3].y + (obb[2].y - obb[3].y) * t
                );

                // Pierce the subplot to get actual furrow endpoints
                // (simplified - just use full line for now)
                if (geom::Point::distance(start, end) > MIN_FURROW) {
                    furrows.push_back({start, end});
                }
            }
        }

        // Create housing for some subplots (mfcg.js line 12705: 20% probability)
        if (utils::Random::boolVal(0.20)) {
            geom::Polygon housing = createHousing(subPlots[i]);
            if (housing.length() >= 3) {
                farmBuildings.push_back(housing);
            }
        }
    }

    // Add farm buildings to geometry for rendering
    geometry = farmBuildings;
}

} // namespace wards
} // namespace town_generator
