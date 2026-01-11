/**
 * Chaikin.js - Chaikin curve subdivision
 *
 * Implements Chaikin's corner-cutting algorithm for curve smoothing.
 * Used for smoothing roads, walls, coastlines, etc.
 */

import { Point } from '../core/Point.js';

export class Chaikin {
    /**
     * Apply Chaikin subdivision to a polygon or polyline
     * @param {Point[]} points - Input points
     * @param {boolean} closed - Whether the curve is closed
     * @param {number} iterations - Number of subdivision iterations
     * @param {Point[]} [anchors=[]] - Points that should not be moved
     * @returns {Point[]}
     */
    static render(points, closed = true, iterations = 2, anchors = []) {
        if (points.length < 2) return points.slice();

        const anchorSet = new Set(anchors);
        let result = points.slice();

        for (let iter = 0; iter < iterations; iter++) {
            const newPoints = [];
            const n = result.length;

            for (let i = 0; i < n; i++) {
                const curr = result[i];
                const next = result[(i + 1) % n];

                // Don't process last edge if not closed
                if (!closed && i === n - 1) {
                    break;
                }

                if (anchorSet.has(curr)) {
                    // Keep anchor points
                    newPoints.push(curr);
                    if (!anchorSet.has(next)) {
                        newPoints.push(Point.lerp(curr, next, 0.25));
                    }
                } else if (anchorSet.has(next)) {
                    // Approaching anchor
                    newPoints.push(Point.lerp(curr, next, 0.75));
                } else {
                    // Normal subdivision
                    newPoints.push(Point.lerp(curr, next, 0.25));
                    newPoints.push(Point.lerp(curr, next, 0.75));
                }
            }

            // Handle endpoints for open curves
            if (!closed) {
                // Preserve first point
                if (!anchorSet.has(result[0])) {
                    newPoints.unshift(result[0]);
                }
                // Preserve last point
                if (!anchorSet.has(result[n - 1])) {
                    newPoints.push(result[n - 1]);
                }
            }

            result = newPoints;
        }

        return result;
    }

    /**
     * Smooth an open polyline
     * @param {Point[]} points
     * @param {number} iterations
     * @returns {Point[]}
     */
    static smoothOpen(points, iterations = 2) {
        return Chaikin.render(points, false, iterations, []);
    }

    /**
     * Smooth a closed polygon
     * @param {Point[]} points
     * @param {number} iterations
     * @returns {Point[]}
     */
    static smoothClosed(points, iterations = 2) {
        return Chaikin.render(points, true, iterations, []);
    }
}

/**
 * Additional polygon smoothing utilities
 */
export class PolygonSmoother {
    /**
     * Smooth polygon with per-edge control
     * @param {Point[]} poly
     * @param {Point[]} anchors - Points to keep fixed
     * @param {number} iterations
     * @returns {Point[]}
     */
    static smooth(poly, anchors, iterations) {
        if (!poly || poly.length < 3) return poly ? poly.slice() : [];
        return Chaikin.render(poly, true, iterations, anchors || []);
    }

    /**
     * Smooth open polyline with endpoint anchors
     * @param {Point[]} points
     * @param {Point[]} anchors
     * @param {number} iterations
     * @returns {Point[]}
     */
    static smoothOpen(points, anchors, iterations) {
        if (!points || points.length < 2) return points ? points.slice() : [];

        // Add endpoints as anchors
        const allAnchors = anchors ? anchors.slice() : [];
        if (points.length > 0 && !allAnchors.includes(points[0])) {
            allAnchors.push(points[0]);
        }
        if (points.length > 1 && !allAnchors.includes(points[points.length - 1])) {
            allAnchors.push(points[points.length - 1]);
        }

        return Chaikin.render(points, false, iterations, allAnchors);
    }

    /**
     * Inset polygon and optionally add rounded corners
     * @param {Point[]} poly
     * @param {number} amount - Inset amount
     * @returns {Point[]|null}
     */
    static inset(poly, amount) {
        if (!poly || poly.length < 3) return null;

        const n = poly.length;
        const result = [];

        for (let i = 0; i < n; i++) {
            const prev = poly[(i + n - 1) % n];
            const curr = poly[i];
            const next = poly[(i + 1) % n];

            // Compute edge directions
            const d1 = curr.subtract(prev).normalized();
            const d2 = next.subtract(curr).normalized();

            // Compute normals (inward)
            const n1 = new Point(-d1.y, d1.x);
            const n2 = new Point(-d2.y, d2.x);

            // Average normal for corner
            const avgNormal = n1.add(n2).normalized();

            // Compute inset distance (handle acute angles)
            const dot = d1.dot(d2);
            const scale = amount / Math.max(0.5, Math.sqrt((1 + dot) / 2));

            const newPoint = curr.add(avgNormal.scale(scale));
            result.push(newPoint);
        }

        // Check if result is valid (positive area)
        if (polygonArea(result) <= 0) {
            return null;
        }

        return result;
    }
}

// Helper function
function polygonArea(poly) {
    let area = 0;
    const n = poly.length;
    for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        area += poly[i].x * poly[j].y;
        area -= poly[j].x * poly[i].y;
    }
    return area / 2;
}
