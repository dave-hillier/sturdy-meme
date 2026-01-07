#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace town_generator {
namespace building {

/**
 * Cutter - Polygon splitting algorithms, faithful port from Haxe TownGeneratorOS
 * and mfcg.js Zk class
 */
class Cutter {
public:
    // Grid subdivision of a quadrilateral
    // Faithful to mfcg.js Zk.grid (lines 19683-19720)
    static std::vector<geom::Polygon> grid(
        const geom::Polygon& quad,
        int cols,
        int rows,
        double jitter = 0.0
    ) {
        if (quad.length() != 4) {
            return {};
        }

        // Create column divisions [0, 1/cols, 2/cols, ..., 1]
        std::vector<double> colRatios;
        for (int i = 0; i <= cols; ++i) {
            colRatios.push_back(static_cast<double>(i) / cols);
        }

        // Create row divisions [0, 1/rows, 2/rows, ..., 1]
        std::vector<double> rowRatios;
        for (int i = 0; i <= rows; ++i) {
            rowRatios.push_back(static_cast<double>(i) / rows);
        }

        // Add jitter to interior divisions
        if (jitter > 0) {
            for (int i = 1; i < cols; ++i) {
                double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                                 utils::Random::floatVal()) / 3.0;
                colRatios[i] += (normal3 - 0.5) / (cols - 1) * jitter;
            }
            for (int i = 1; i < rows; ++i) {
                double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                                 utils::Random::floatVal()) / 3.0;
                rowRatios[i] += (normal3 - 0.5) / (rows - 1) * jitter;
            }
        }

        // Get corner points (assumes CCW order: bottom-left, bottom-right, top-right, top-left)
        geom::Point p0 = quad[0];  // bottom-left
        geom::Point p1 = quad[1];  // bottom-right
        geom::Point p2 = quad[2];  // top-right
        geom::Point p3 = quad[3];  // top-left

        // Build grid of points
        std::vector<std::vector<geom::Point>> gridPoints;
        for (int r = 0; r <= rows; ++r) {
            // Interpolate left and right edges
            geom::Point left = geom::GeomUtils::lerp(p0, p3, rowRatios[r]);
            geom::Point right = geom::GeomUtils::lerp(p1, p2, rowRatios[r]);

            std::vector<geom::Point> rowPoints;
            for (int c = 0; c <= cols; ++c) {
                rowPoints.push_back(geom::GeomUtils::lerp(left, right, colRatios[c]));
            }
            gridPoints.push_back(rowPoints);
        }

        // Build cells from grid points
        std::vector<geom::Polygon> cells;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                std::vector<geom::Point> cell = {
                    gridPoints[r][c],
                    gridPoints[r][c + 1],
                    gridPoints[r + 1][c + 1],
                    gridPoints[r + 1][c]
                };
                cells.push_back(geom::Polygon(cell));
            }
        }

        return cells;
    }
    // Bisect polygon at a vertex
    static std::vector<geom::Polygon> bisect(
        const geom::Polygon& poly,
        const geom::Point& vertex,
        double ratio = 0.5,
        double angle = 0.0,
        double gap = 0.0
    ) {
        geom::Point next = poly.next(vertex);

        geom::Point p1 = geom::GeomUtils::interpolate(vertex, next, ratio);
        geom::Point d = next.subtract(vertex);

        double cosB = std::cos(angle);
        double sinB = std::sin(angle);
        double vx = d.x * cosB - d.y * sinB;
        double vy = d.y * cosB + d.x * sinB;
        geom::Point p2(p1.x - vy, p1.y + vx);

        return poly.cut(p1, p2, gap);
    }

    // Radial subdivision from center
    static std::vector<geom::Polygon> radial(
        const geom::Polygon& poly,
        const geom::Point* center = nullptr,
        double gap = 0.0
    ) {
        geom::Point actualCenter = center ? *center : poly.centroid();

        std::vector<geom::Polygon> sectors;

        poly.forEdge([&sectors, &actualCenter, gap](const geom::Point& v0, const geom::Point& v1) {
            geom::Polygon sector({actualCenter, v0, v1});

            if (gap > 0) {
                sector = sector.shrink({gap / 2, 0, gap / 2});
            }

            sectors.push_back(sector);
        });

        return sectors;
    }

    // Semi-radial subdivision
    static std::vector<geom::Polygon> semiRadial(
        const geom::Polygon& poly,
        const geom::Point* center = nullptr,
        double gap = 0.0
    ) {
        geom::Point actualCenter;

        if (center) {
            actualCenter = *center;
        } else {
            geom::Point centroid = poly.centroid();
            // Find vertex closest to centroid
            actualCenter = poly.min([&centroid](const geom::Point& v) {
                return geom::Point::distance(v, centroid);
            });
        }

        double halfGap = gap / 2;

        std::vector<geom::Polygon> sectors;

        poly.forEdge([&sectors, &poly, &actualCenter, halfGap](const geom::Point& v0, const geom::Point& v1) {
            if (v0 != actualCenter && v1 != actualCenter) {
                geom::Polygon sector({actualCenter, v0, v1});

                if (halfGap > 0) {
                    std::vector<double> d = {
                        poly.findEdge(actualCenter, v0) == -1 ? halfGap : 0.0,
                        0.0,
                        poly.findEdge(v1, actualCenter) == -1 ? halfGap : 0.0
                    };
                    sector = sector.shrink(d);
                }

                sectors.push_back(sector);
            }
        });

        return sectors;
    }

    // Ring (peel) subdivision
    static std::vector<geom::Polygon> ring(
        const geom::Polygon& poly,
        double thickness
    ) {
        struct Slice {
            geom::Point p1, p2;
            double len;
        };

        std::vector<Slice> slices;

        poly.forEdge([&slices, thickness](const geom::Point& v1, const geom::Point& v2) {
            geom::Point v = v2.subtract(v1);
            geom::Point n = v.rotate90().norm(thickness);
            slices.push_back({v1.add(n), v2.add(n), v.length()});
        });

        // Sort by length (short sides first)
        std::sort(slices.begin(), slices.end(),
            [](const Slice& a, const Slice& b) { return a.len < b.len; });

        std::vector<geom::Polygon> peel;
        geom::Polygon p = poly;

        for (const auto& slice : slices) {
            auto halves = p.cut(slice.p1, slice.p2);
            p = halves[0];
            if (halves.size() == 2) {
                peel.push_back(halves[1]);
            }
        }

        return peel;
    }

    // Equality (stateless utility class)
    bool operator==(const Cutter& other) const { return true; }
    bool operator!=(const Cutter& other) const { return false; }
};

} // namespace building
} // namespace town_generator
