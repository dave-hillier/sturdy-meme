/**
 * PolyBool.js - Boolean operations on polygons
 *
 * Provides polygon intersection (AND) operations.
 */

import { Point } from '../core/Point.js';
import { GeomUtils } from './GeomUtils.js';
import { PolygonUtils } from './PolygonUtils.js';

/**
 * Boolean polygon operations
 */
export class PolyBool {
    /**
     * Augment two polygons by finding all intersection points
     * and inserting them into both polygons
     *
     * @param {Point[]} polyA - First polygon
     * @param {Point[]} polyB - Second polygon
     * @returns {[Point[], Point[]]} Augmented polygons with intersection points
     */
    static augmentPolygons(polyA, polyB) {
        const lenA = polyA.length;
        const lenB = polyB.length;

        // Arrays to store intersection points for each edge
        const intersectionsA = [];
        const intersectionsB = [];

        for (let i = 0; i < lenA; i++) {
            intersectionsA.push([]);
        }
        for (let i = 0; i < lenB; i++) {
            intersectionsB.push([]);
        }

        // Find all intersections between edges
        for (let i = 0; i < lenA; i++) {
            const a1 = polyA[i];
            const a2 = polyA[(i + 1) % lenA];
            const ax = a1.x;
            const ay = a1.y;
            const adx = a2.x - ax;
            const ady = a2.y - ay;

            for (let j = 0; j < lenB; j++) {
                const b1 = polyB[j];
                const b2 = polyB[(j + 1) % lenB];
                const bx = b1.x;
                const by = b1.y;

                const result = GeomUtils.intersectLines(
                    ax, ay, adx, ady,
                    bx, by, b2.x - bx, b2.y - by
                );

                if (result !== null &&
                    result.x >= 0 && result.x <= 1 &&
                    result.y >= 0 && result.y <= 1) {
                    // Found valid intersection
                    const point = Point.lerp(a1, a2, result.x);
                    const intersection = {
                        a: result.x,  // Parameter along edge A
                        b: result.y,  // Parameter along edge B
                        p: point      // Intersection point
                    };
                    intersectionsA[i].push(intersection);
                    intersectionsB[j].push(intersection);
                }
            }
        }

        // Build augmented polygon A
        const augmentedA = [];
        for (let i = 0; i < lenA; i++) {
            augmentedA.push(polyA[i]);
            const edgeIntersections = intersectionsA[i];
            if (edgeIntersections.length > 0) {
                // Sort by parameter along edge
                edgeIntersections.sort((x, y) => x.a - y.a);
                for (const inter of edgeIntersections) {
                    augmentedA.push(inter.p);
                }
            }
        }

        // Build augmented polygon B
        const augmentedB = [];
        for (let i = 0; i < lenB; i++) {
            augmentedB.push(polyB[i]);
            const edgeIntersections = intersectionsB[i];
            if (edgeIntersections.length > 0) {
                // Sort by parameter along edge
                edgeIntersections.sort((x, y) => x.b - y.b);
                for (const inter of edgeIntersections) {
                    augmentedB.push(inter.p);
                }
            }
        }

        return [augmentedA, augmentedB];
    }

    /**
     * Compute intersection (AND) of two polygons
     *
     * @param {Point[]} polyA - First polygon
     * @param {Point[]} polyB - Second polygon
     * @param {boolean} [returnA=false] - If no intersection, return polyA instead of polyB
     * @returns {Point[]|null} Intersection polygon, or null if no intersection
     */
    static and(polyA, polyB, returnA = false) {
        const [augA, augB] = PolyBool.augmentPolygons(polyA, polyB);

        // If no new points were added, check containment
        if (augA.length === polyA.length) {
            // No intersections - check if one contains the other
            if (PolygonUtils.containsPoint(polyA, polyB[0])) {
                return returnA ? polyA : polyB;
            }
            if (PolygonUtils.containsPoint(polyB, polyA[0], returnA)) {
                return returnA ? null : polyA;
            }
            return returnA ? polyA : null;
        }

        // Start from a vertex that was added (intersection point)
        let currentPoly = augA;
        let otherPoly = augB;
        let startIdx = -1;
        let startPoint = null;

        for (let i = 0; i < augA.length; i++) {
            if (polyA.indexOf(augA[i]) === -1) {
                // This point is an intersection
                startIdx = i;
                startPoint = augA[i];
                break;
            }
        }

        if (startIdx === -1) {
            return returnA ? polyA : null;
        }

        // Check which polygon to trace first
        const testPoint = Point.lerp(startPoint, augA[(startIdx + 1) % augA.length], 0.5);
        if (!PolygonUtils.containsPoint(polyB, testPoint, returnA)) {
            currentPoly = augB;
            otherPoly = augA;
            startIdx = augB.indexOf(startPoint);
        }

        // Trace the intersection boundary
        const result = [];
        let idx = startIdx;

        while (true) {
            result.push(currentPoly[idx]);

            const nextIdx = (idx + 1) % currentPoly.length;
            const nextPoint = currentPoly[nextIdx];

            // Check if we've completed the loop
            if (nextPoint === result[0] || PolyBool.pointsEqual(nextPoint, result[0])) {
                return result;
            }

            // Check if next point is in other polygon (switch polygons)
            const otherIdx = PolyBool.findPointIndex(otherPoly, nextPoint);
            if (otherIdx !== -1) {
                // Switch polygons
                idx = otherIdx;
                const temp = currentPoly;
                currentPoly = otherPoly;
                otherPoly = temp;
            } else {
                idx = nextIdx;
            }

            // Safety check
            if (result.length > augA.length + augB.length) {
                break;
            }
        }

        return result;
    }

    /**
     * Check if two points are approximately equal
     * @param {Point} a
     * @param {Point} b
     * @param {number} [epsilon=0.0001]
     * @returns {boolean}
     */
    static pointsEqual(a, b, epsilon = 0.0001) {
        return Math.abs(a.x - b.x) < epsilon && Math.abs(a.y - b.y) < epsilon;
    }

    /**
     * Find index of a point in polygon (with tolerance)
     * @param {Point[]} poly
     * @param {Point} point
     * @returns {number} Index or -1 if not found
     */
    static findPointIndex(poly, point) {
        for (let i = 0; i < poly.length; i++) {
            if (PolyBool.pointsEqual(poly[i], point)) {
                return i;
            }
        }
        return -1;
    }
}
