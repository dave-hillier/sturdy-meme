#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace town_generator {
namespace building {

/**
 * Cutter - Polygon splitting algorithms, faithful port from Haxe TownGeneratorOS
 */
class Cutter {
public:
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
