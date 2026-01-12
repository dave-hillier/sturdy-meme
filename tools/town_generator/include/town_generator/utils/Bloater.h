#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace town_generator {
namespace utils {

/**
 * Bloater - Polygon expansion utilities
 *
 * Expands polygons by extruding edges outward.
 * Faithful port from mfcg-clean/utils/Bloater.js
 */
class Bloater {
public:
    /**
     * Bloat a polygon by adding intermediate extrusion points
     *
     * @param poly Input polygon
     * @param minLength Minimum edge length for subdivision
     * @return Bloated polygon with additional vertices
     */
    static std::vector<geom::Point> bloat(const std::vector<geom::Point>& poly, double minLength) {
        size_t n = poly.size();
        std::vector<geom::Point> result;

        for (size_t i = 0; i < n; ++i) {
            const geom::Point& p1 = poly[i];
            const geom::Point& p2 = poly[(i + 1) % n];

            // Recursively extrude this edge
            auto extruded = extrudeEx(p1, p2, minLength);
            for (const auto& point : extruded) {
                result.push_back(point);
            }
        }

        return result;
    }

    /**
     * Extrude a single edge segment outward
     *
     * @param p1 Start point
     * @param p2 End point
     * @param minLength Minimum edge length
     * @return Extruded midpoint, or nullopt if edge is too short
     */
    static std::optional<geom::Point> extrude(const geom::Point& p1, const geom::Point& p2, double minLength) {
        geom::Point delta = p1.subtract(p2);
        double length = delta.length();
        double ratio = length / minLength;

        // Only extrude if edge is long enough
        if (ratio <= 0.3) {
            return std::nullopt;
        }

        // Create perpendicular vector
        geom::Point perp(-delta.y, delta.x);

        // Scale factor based on edge length (0.5 * clamped ratio)
        double scale = 0.5 * std::min(1.0, ratio);
        perp.x *= scale;
        perp.y *= scale;

        // Create extruded point at midpoint
        geom::Point mid = geom::GeomUtils::lerp(p1, p2, 0.5);
        return geom::Point(mid.x + perp.x, mid.y + perp.y);
    }

    /**
     * Recursively extrude an edge segment
     *
     * @param p1 Start point
     * @param p2 End point
     * @param minLength Minimum edge length for subdivision
     * @return Array of points (always includes p1)
     */
    static std::vector<geom::Point> extrudeEx(const geom::Point& p1, const geom::Point& p2, double minLength) {
        auto extruded = extrude(p1, p2, minLength);

        if (!extruded.has_value()) {
            // Edge too short, just return start point
            return {p1};
        }

        // Recursively extrude both halves
        auto left = extrudeEx(p1, *extruded, minLength);
        auto right = extrudeEx(*extruded, p2, minLength);

        // Concatenate results
        left.insert(left.end(), right.begin(), right.end());
        return left;
    }

    /**
     * Bloat polygon with smooth curves
     *
     * @param poly Input polygon
     * @param amount Bloat amount
     * @param segments Segments per corner (default 3)
     * @return Bloated polygon
     */
    static std::vector<geom::Point> bloatSmooth(const std::vector<geom::Point>& poly, double amount, int segments = 3) {
        size_t n = poly.size();
        std::vector<geom::Point> result;

        for (size_t i = 0; i < n; ++i) {
            const geom::Point& prev = poly[(i + n - 1) % n];
            const geom::Point& curr = poly[i];
            const geom::Point& next = poly[(i + 1) % n];

            // Edge directions
            geom::Point d1 = curr.subtract(prev).norm();
            geom::Point d2 = next.subtract(curr).norm();

            // Normals (perpendicular, pointing outward for CCW polygon)
            geom::Point n1(-d1.y, d1.x);
            geom::Point n2(-d2.y, d2.x);

            // Offset points
            geom::Point offset1 = curr.add(n1.scale(amount));
            geom::Point offset2 = curr.add(n2.scale(amount));

            // Add arc between offset points
            if (segments > 1) {
                for (int s = 0; s <= segments; ++s) {
                    double t = static_cast<double>(s) / segments;
                    result.push_back(geom::GeomUtils::lerp(offset1, offset2, t));
                }
            } else {
                result.push_back(offset1);
            }
        }

        return result;
    }

    /**
     * Offset polygon inward or outward uniformly
     *
     * @param poly Input polygon
     * @param amount Offset amount (positive = outward, negative = inward)
     * @return Offset polygon
     */
    static std::vector<geom::Point> offset(const std::vector<geom::Point>& poly, double amount) {
        size_t n = poly.size();
        if (n < 3) return poly;

        std::vector<geom::Point> result;
        result.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            const geom::Point& prev = poly[(i + n - 1) % n];
            const geom::Point& curr = poly[i];
            const geom::Point& next = poly[(i + 1) % n];

            // Edge directions
            geom::Point d1 = curr.subtract(prev).norm();
            geom::Point d2 = next.subtract(curr).norm();

            // Normals
            geom::Point n1(-d1.y, d1.x);
            geom::Point n2(-d2.y, d2.x);

            // Average normal for corner
            geom::Point avgNormal = n1.add(n2).norm();

            // Compute offset distance (handle acute angles)
            double dot = d1.dot(d2);
            double scale = amount / std::max(0.5, std::sqrt((1 + dot) / 2));

            result.push_back(curr.add(avgNormal.scale(scale)));
        }

        return result;
    }

    /**
     * Inflate polygon (offset outward)
     */
    static std::vector<geom::Point> inflate(const std::vector<geom::Point>& poly, double amount) {
        return offset(poly, amount);
    }

    /**
     * Deflate polygon (offset inward)
     */
    static std::vector<geom::Point> deflate(const std::vector<geom::Point>& poly, double amount) {
        return offset(poly, -amount);
    }
};

} // namespace utils
} // namespace town_generator
