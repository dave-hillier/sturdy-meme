/**
 * Bloater.js - Polygon expansion utilities
 *
 * Expands polygons by extruding edges outward.
 */

import { Point } from '../core/Point.js';

/**
 * Polygon bloating/expansion utilities
 */
export class Bloater {
    /**
     * Bloat a polygon by adding intermediate extrusion points
     *
     * @param {Point[]} poly - Input polygon
     * @param {number} minLength - Minimum edge length for subdivision
     * @returns {Point[]} Bloated polygon with additional vertices
     */
    static bloat(poly, minLength) {
        const n = poly.length;
        const result = [];

        for (let i = 0; i < n; i++) {
            const p1 = poly[i];
            const p2 = poly[(i + 1) % n];

            // Recursively extrude this edge
            const extruded = Bloater.extrudeEx(p1, p2, minLength);
            for (const point of extruded) {
                result.push(point);
            }
        }

        return result;
    }

    /**
     * Extrude a single edge segment outward
     *
     * @param {Point} p1 - Start point
     * @param {Point} p2 - End point
     * @param {number} minLength - Minimum edge length
     * @returns {Point|null} Extruded midpoint, or null if edge is too short
     */
    static extrude(p1, p2, minLength) {
        const delta = p1.subtract(p2);
        const length = delta.length;
        const ratio = length / minLength;

        // Only extrude if edge is long enough
        if (ratio <= 0.3) {
            return null;
        }

        // Create perpendicular vector
        const perp = new Point(-delta.y, delta.x);

        // Scale factor based on edge length (0.5 * clamped ratio)
        const scale = 0.5 * Math.min(1, ratio);
        perp.x *= scale;
        perp.y *= scale;

        // Create extruded point at midpoint
        const mid = Point.lerp(p1, p2, 0.5);
        return new Point(mid.x + perp.x, mid.y + perp.y);
    }

    /**
     * Recursively extrude an edge segment
     *
     * @param {Point} p1 - Start point
     * @param {Point} p2 - End point
     * @param {number} minLength - Minimum edge length for subdivision
     * @returns {Point[]} Array of points (always includes p1)
     */
    static extrudeEx(p1, p2, minLength) {
        const extruded = Bloater.extrude(p1, p2, minLength);

        if (extruded === null) {
            // Edge too short, just return start point
            return [p1];
        }

        // Recursively extrude both halves
        const left = Bloater.extrudeEx(p1, extruded, minLength);
        const right = Bloater.extrudeEx(extruded, p2, minLength);

        return left.concat(right);
    }

    /**
     * Bloat polygon with smooth curves
     *
     * @param {Point[]} poly - Input polygon
     * @param {number} amount - Bloat amount
     * @param {number} [segments=3] - Segments per corner
     * @returns {Point[]} Bloated polygon
     */
    static bloatSmooth(poly, amount, segments = 3) {
        const n = poly.length;
        const result = [];

        for (let i = 0; i < n; i++) {
            const prev = poly[(i + n - 1) % n];
            const curr = poly[i];
            const next = poly[(i + 1) % n];

            // Edge directions
            const d1 = curr.subtract(prev).normalized();
            const d2 = next.subtract(curr).normalized();

            // Normals (perpendicular, pointing outward for CCW polygon)
            const n1 = new Point(-d1.y, d1.x);
            const n2 = new Point(-d2.y, d2.x);

            // Offset points
            const offset1 = curr.add(n1.scale(amount));
            const offset2 = curr.add(n2.scale(amount));

            // Add arc between offset points
            if (segments > 1) {
                for (let s = 0; s <= segments; s++) {
                    const t = s / segments;
                    result.push(Point.lerp(offset1, offset2, t));
                }
            } else {
                result.push(offset1);
            }
        }

        return result;
    }
}
