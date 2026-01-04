#pragma once

#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/GeomUtils.hpp"
#include <cmath>
#include <algorithm>

namespace town_generator2 {
namespace building {

/**
 * Cutter - Polygon subdivision utilities
 */
struct Cutter {
    /**
     * Bisect polygon at a vertex with given ratio and angle
     */
    static std::vector<geom::Polygon> bisect(
        const geom::Polygon& poly,
        const geom::PointPtr& vertex,
        double ratio = 0.5,
        double angle = 0.0,
        double gap = 0.0
    ) {
        geom::PointPtr nextPtr = poly.next(vertex);
        const geom::Point& next = *nextPtr;
        const geom::Point& v = *vertex;

        geom::Point p1 = geom::GeomUtils::interpolate(v, next, ratio);
        geom::Point d = next.subtract(v);

        double cosB = std::cos(angle);
        double sinB = std::sin(angle);
        double vx = d.x * cosB - d.y * sinB;
        double vy = d.y * cosB + d.x * sinB;
        geom::Point p2(p1.x - vy, p1.y + vx);

        return poly.cut(p1, p2, gap);
    }

    /**
     * Radial subdivision from center to each edge
     */
    static std::vector<geom::Polygon> radial(
        const geom::Polygon& poly,
        const geom::Point* center = nullptr,
        double gap = 0.0
    ) {
        geom::Point c = center ? *center : poly.centroid();

        std::vector<geom::Polygon> sectors;
        poly.forEdge([&sectors, &c, gap](const geom::Point& v0, const geom::Point& v1) {
            geom::Polygon sector({c, v0, v1});
            if (gap > 0) {
                sector = sector.shrink({gap / 2, 0, gap / 2});
            }
            sectors.push_back(sector);
        });
        return sectors;
    }

    /**
     * Semi-radial subdivision - radial from nearest vertex to centroid
     */
    static std::vector<geom::Polygon> semiRadial(
        const geom::Polygon& poly,
        const geom::PointPtr& center = nullptr,
        double gap = 0.0
    ) {
        geom::PointPtr c = center;
        if (!c) {
            geom::Point centroid = poly.centroid();
            c = poly.min([&centroid](const geom::Point& v) {
                return geom::Point::distance(v, centroid);
            });
        }

        double halfGap = gap / 2;
        std::vector<geom::Polygon> sectors;

        poly.forEdgePtr([&sectors, &c, &poly, halfGap](const geom::PointPtr& v0, const geom::PointPtr& v1) {
            if (v0 != c && v1 != c) {
                geom::Polygon sector({*c, *v0, *v1});
                if (halfGap > 0) {
                    std::vector<double> d = {
                        poly.findEdge(c, v0) == -1 ? halfGap : 0.0,
                        0.0,
                        poly.findEdge(v1, c) == -1 ? halfGap : 0.0
                    };
                    sector = sector.shrink(d);
                }
                sectors.push_back(sector);
            }
        });
        return sectors;
    }

    /**
     * Ring subdivision - peel edges to create ring-shaped sectors
     */
    static std::vector<geom::Polygon> ring(const geom::Polygon& poly, double thickness) {
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

        // Sort by length - short sides first
        std::sort(slices.begin(), slices.end(),
            [](const Slice& a, const Slice& b) { return a.len < b.len; });

        std::vector<geom::Polygon> peel;
        geom::Polygon p = poly.deepCopy();

        for (const auto& slice : slices) {
            auto halves = p.cut(slice.p1, slice.p2);
            p = halves[0];
            if (halves.size() == 2) {
                peel.push_back(halves[1]);
            }
        }

        return peel;
    }
};

} // namespace building
} // namespace town_generator2
