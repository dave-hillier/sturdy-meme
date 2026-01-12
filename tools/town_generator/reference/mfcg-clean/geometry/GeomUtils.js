/**
 * GeomUtils.js - Geometry utility functions
 */

import { Point } from '../core/Point.js';

export class GeomUtils {
    /**
     * Linear interpolation between two points
     * @param {Point} a
     * @param {Point} b
     * @param {number} [t=0.5]
     * @returns {Point}
     */
    static lerp(a, b, t = 0.5) {
        return Point.lerp(a, b, t);
    }

    /**
     * Compute barycentric coordinates of point in triangle
     * @param {Point} a - Triangle vertex A
     * @param {Point} b - Triangle vertex B
     * @param {Point} c - Triangle vertex C
     * @param {Point} p - Point to compute coords for
     * @returns {{x: number, y: number, z: number}} Barycentric coordinates
     */
    static barycentric(a, b, c, p) {
        const v0 = c.subtract(a);
        const v1 = b.subtract(a);
        const v2 = p.subtract(a);

        const dot00 = v0.dot(v0);
        const dot01 = v0.dot(v1);
        const dot02 = v0.dot(v2);
        const dot11 = v1.dot(v1);
        const dot12 = v1.dot(v2);

        const invDenom = 1 / (dot00 * dot11 - dot01 * dot01);
        const u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        const v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        return { x: 1 - u - v, y: v, z: u };
    }

    /**
     * Find intersection of two lines
     * Line 1: point (x1, y1) + t * direction (dx1, dy1)
     * Line 2: point (x2, y2) + s * direction (dx2, dy2)
     * @returns {{x: number, y: number}} Parameters t and s
     */
    static intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2) {
        const denom = dx1 * dy2 - dy1 * dx2;
        if (Math.abs(denom) < 0.0001) {
            return { x: Infinity, y: Infinity };
        }
        const t = ((x2 - x1) * dy2 - (y2 - y1) * dx2) / denom;
        const s = ((x2 - x1) * dy1 - (y2 - y1) * dx1) / denom;
        return { x: t, y: s };
    }

    /**
     * Check if two line segments converge (share endpoint direction)
     * @param {Point} a1 - Start of segment 1
     * @param {Point} a2 - End of segment 1
     * @param {Point} b1 - Start of segment 2
     * @param {Point} b2 - End of segment 2
     * @returns {boolean}
     */
    static converge(a1, a2, b1, b2) {
        // Check if segments share an endpoint and point in same direction
        if (a2.equals(b1)) {
            const d1 = a2.subtract(a1).normalized();
            const d2 = b2.subtract(b1).normalized();
            return d1.dot(d2) > 0.99;
        }
        return false;
    }

    /**
     * Calculate signed area of triangle
     * @param {Point} a
     * @param {Point} b
     * @param {Point} c
     * @returns {number} Signed area (positive = CCW, negative = CW)
     */
    static triangleArea(a, b, c) {
        return ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
    }

    /**
     * Check if point is left of line from a to b
     * @param {Point} a - Line start
     * @param {Point} b - Line end
     * @param {Point} p - Point to test
     * @returns {boolean}
     */
    static isLeftOf(a, b, p) {
        return GeomUtils.triangleArea(a, b, p) > 0;
    }

    /**
     * Compute convex hull using Graham scan
     * @param {Point[]} points
     * @returns {Point[]}
     */
    static convexHull(points) {
        if (points.length < 3) return points.slice();

        // Find lowest point
        let lowest = 0;
        for (let i = 1; i < points.length; i++) {
            if (points[i].y < points[lowest].y ||
                (points[i].y === points[lowest].y && points[i].x < points[lowest].x)) {
                lowest = i;
            }
        }

        const pivot = points[lowest];
        const sorted = points.slice().sort((a, b) => {
            const angleA = Math.atan2(a.y - pivot.y, a.x - pivot.x);
            const angleB = Math.atan2(b.y - pivot.y, b.x - pivot.x);
            return angleA - angleB;
        });

        const hull = [];
        for (const point of sorted) {
            while (hull.length >= 2 &&
                   GeomUtils.triangleArea(hull[hull.length - 2], hull[hull.length - 1], point) <= 0) {
                hull.pop();
            }
            hull.push(point);
        }

        return hull;
    }

    /**
     * Check if a point lies on a line segment
     * @param {Point} point - Point to test
     * @param {Point} segStart - Segment start
     * @param {Point} segEnd - Segment end
     * @param {number} [tolerance=0.001] - Distance tolerance
     * @returns {boolean}
     */
    static pointOnSegment(point, segStart, segEnd, tolerance = 0.001) {
        const segLen = Point.distance(segStart, segEnd);
        if (segLen < tolerance) {
            return Point.distance(point, segStart) < tolerance;
        }

        const d1 = Point.distance(point, segStart);
        const d2 = Point.distance(point, segEnd);

        // Check if point is between endpoints (within tolerance)
        if (Math.abs(d1 + d2 - segLen) < tolerance) {
            return true;
        }

        return false;
    }

    /**
     * Find intersection point of two line segments
     * @param {Point} a1 - Start of segment 1
     * @param {Point} a2 - End of segment 1
     * @param {Point} b1 - Start of segment 2
     * @param {Point} b2 - End of segment 2
     * @returns {Point|null} - Intersection point or null
     */
    static lineSegmentIntersection(a1, a2, b1, b2) {
        const d1 = a2.subtract(a1);
        const d2 = b2.subtract(b1);

        const cross = d1.x * d2.y - d1.y * d2.x;
        if (Math.abs(cross) < 0.0001) {
            return null; // Parallel
        }

        const d = b1.subtract(a1);
        const t = (d.x * d2.y - d.y * d2.x) / cross;
        const s = (d.x * d1.y - d.y * d1.x) / cross;

        if (t >= 0 && t <= 1 && s >= 0 && s <= 1) {
            return new Point(a1.x + d1.x * t, a1.y + d1.y * t);
        }

        return null;
    }

    /**
     * Compute squared distance from point to line segment
     * @param {Point} point
     * @param {Point} segStart
     * @param {Point} segEnd
     * @returns {number}
     */
    static pointToSegmentDistSq(point, segStart, segEnd) {
        const dx = segEnd.x - segStart.x;
        const dy = segEnd.y - segStart.y;
        const lenSq = dx * dx + dy * dy;

        if (lenSq === 0) {
            return Point.distanceSquared(point, segStart);
        }

        const t = Math.max(0, Math.min(1,
            ((point.x - segStart.x) * dx + (point.y - segStart.y) * dy) / lenSq
        ));

        const proj = new Point(segStart.x + t * dx, segStart.y + t * dy);
        return Point.distanceSquared(point, proj);
    }

    /**
     * Compute distance from point to line segment
     * @param {Point} point
     * @param {Point} segStart
     * @param {Point} segEnd
     * @returns {number}
     */
    static pointToSegmentDist(point, segStart, segEnd) {
        return Math.sqrt(GeomUtils.pointToSegmentDistSq(point, segStart, segEnd));
    }
}
