/**
 * Ported from: Source/com/watabou/towngenerator/building/Cutter.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../geom/GeomUtils.hpp"

namespace town {

/**
 * Cutter class - provides static methods for subdividing polygons.
 * Used for creating building lots, streets, and other town features.
 */
class Cutter {
public:
    /**
     * Bisects a polygon along a line perpendicular to an edge.
     * @param poly The polygon to bisect
     * @param vertex The starting vertex of the edge to bisect from (shared_ptr)
     * @param ratio Position along the edge (0.0 to 1.0, default 0.5)
     * @param angle Rotation of the cut line in radians (default 0.0)
     * @param gap Gap to leave between the resulting polygons (default 0.0)
     * @return Vector of resulting polygons (usually 2)
     */
    static std::vector<Polygon> bisect(const Polygon& poly, const PointPtr& vertex,
                                        float ratio = 0.5f, float angle = 0.0f,
                                        float gap = 0.0f) {
        PointPtr next = poly.next(vertex);
        if (!next) return {poly};

        Point p1 = GeomUtils::interpolate(*vertex, *next, ratio);
        Point d = next->subtract(*vertex);

        float cosB = std::cos(angle);
        float sinB = std::sin(angle);
        float vx = d.x * cosB - d.y * sinB;
        float vy = d.y * cosB + d.x * sinB;
        Point p2(p1.x - vy, p1.y + vx);

        return poly.cut(p1, p2, gap);
    }

    /**
     * Divides a polygon into radial sectors from a center point.
     * @param poly The polygon to divide
     * @param center The center point (defaults to centroid if null)
     * @param gap Gap to leave between sectors (default 0.0)
     * @return Vector of sector polygons
     */
    static std::vector<Polygon> radial(const Polygon& poly,
                                        const Point* center = nullptr,
                                        float gap = 0.0f) {
        Point c;
        if (center) {
            c = *center;
        } else {
            c = poly.centroid();
        }

        std::vector<Polygon> sectors;

        poly.forEdge([&](const Point& v0, const Point& v1) {
            Polygon sector({c, v0, v1});
            if (gap > 0) {
                std::vector<float> shrinkAmounts = {gap / 2.0f, 0.0f, gap / 2.0f};
                sector = sector.shrink(shrinkAmounts);
            }
            sectors.push_back(sector);
        });

        return sectors;
    }

    /**
     * Divides a polygon into semi-radial sectors.
     * Similar to radial, but the center is one of the polygon's vertices.
     * @param poly The polygon to divide
     * @param center The center vertex (defaults to vertex closest to centroid)
     * @param gap Gap to leave between sectors (default 0.0)
     * @return Vector of sector polygons
     */
    static std::vector<Polygon> semiRadial(const Polygon& poly,
                                            const Point* center = nullptr,
                                            float gap = 0.0f) {
        PointPtr c;
        if (center) {
            // Find the vertex in poly that matches the given center point
            c = poly.min([center](const Point& v) {
                return Point::distance(v, *center);
            });
        } else {
            Point centroid = poly.centroid();
            // Find vertex closest to centroid
            c = poly.min([&centroid](const Point& v) {
                return Point::distance(v, centroid);
            });
        }

        if (!c) return {};

        float halfGap = gap / 2.0f;

        std::vector<Polygon> sectors;

        // Get the vertices as shared_ptrs for proper edge finding
        for (size_t i = 0; i < poly.size(); ++i) {
            const PointPtr& v0 = poly[i];
            const PointPtr& v1 = poly[(i + 1) % poly.size()];

            // Skip edges that include the center
            if (v0 != c && v1 != c) {
                Polygon sector({*c, *v0, *v1});
                if (halfGap > 0) {
                    // Check if c->v0 edge exists in original polygon
                    bool cToV0Exists = poly.findEdge(c, v0) != -1;
                    // Check if v1->c edge exists in original polygon
                    bool v1ToCExists = poly.findEdge(v1, c) != -1;

                    std::vector<float> d = {
                        cToV0Exists ? 0.0f : halfGap,
                        0.0f,
                        v1ToCExists ? 0.0f : halfGap
                    };
                    sector = sector.shrink(d);
                }
                sectors.push_back(sector);
            }
        }

        return sectors;
    }

    /**
     * Creates ring-shaped slices around the perimeter of a polygon.
     * @param poly The polygon to peel
     * @param thickness The thickness of the ring
     * @return Vector of polygon slices forming the ring
     */
    static std::vector<Polygon> ring(const Polygon& poly, float thickness) {
        struct Slice {
            Point p1;
            Point p2;
            float len;
        };

        std::vector<Slice> slices;

        poly.forEdge([&](const Point& v1, const Point& v2) {
            Point v = v2.subtract(v1);
            Point n = v.rotate90().norm(thickness);

            Slice slice;
            slice.p1 = v1.add(n);
            slice.p2 = v2.add(n);
            slice.len = v.length();
            slices.push_back(slice);
        });

        // Short sides should be sliced first
        std::sort(slices.begin(), slices.end(),
            [](const Slice& s1, const Slice& s2) {
                return s1.len < s2.len;
            });

        std::vector<Polygon> peel;
        Polygon p = poly;

        for (size_t i = 0; i < slices.size(); ++i) {
            auto halves = p.cut(slices[i].p1, slices[i].p2);
            if (!halves.empty()) {
                p = halves[0];
                if (halves.size() == 2) {
                    peel.push_back(halves[1]);
                }
            }
        }

        return peel;
    }
};

} // namespace town
