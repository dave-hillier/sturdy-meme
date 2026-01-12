/**
 * Bisector.js - Polygon subdivision utility
 *
 * Recursively subdivides polygons into smaller lots using bisecting cuts.
 * Used for generating building lots within city blocks.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { GeomUtils } from '../geometry/GeomUtils.js';

/**
 * Bisector - recursive polygon subdivision
 */
export class Bisector {
    /**
     * Create a bisector
     * @param {Point[]} polygon - Polygon to subdivide
     * @param {number} minArea - Minimum lot area
     * @param {number} [variance=10] - Area variance multiplier
     */
    constructor(polygon, minArea, variance = 10) {
        /** @type {Point[]} */
        this.poly = polygon;

        /** @type {Point[]} */
        this.shape = polygon;

        /** @type {number} */
        this.minArea = minArea;

        /** @type {number} */
        this.variance = variance;

        /** @type {number} */
        this.minOffset = Math.sqrt(minArea);

        /** @type {number} */
        this.minTurnOffset = 1;

        /** @type {Array} */
        this.cuts = [];
    }

    /**
     * Partition the polygon into lots
     * @returns {Point[][]}
     */
    partition() {
        return this.subdivide(this.shape);
    }

    /**
     * Recursively subdivide a polygon
     * @param {Point[]} poly
     * @returns {Point[][]}
     */
    subdivide(poly) {
        if (this.isSmallEnough(poly)) {
            return [poly];
        }

        const parts = this.makeCut(poly);

        if (parts.length === 1) {
            return [poly];
        }

        const result = [];
        for (const part of parts) {
            const subdivided = this.subdivide(part);
            result.push(...subdivided);
        }

        return result;
    }

    /**
     * Check if polygon is small enough to stop subdividing
     * @param {Point[]} poly
     * @returns {boolean}
     */
    isSmallEnough(poly) {
        // Add randomness to minimum area threshold
        const randomFactor = Math.pow(
            this.variance,
            Math.abs(Random.gaussian())
        );
        const threshold = this.minArea * randomFactor;

        return Math.abs(PolygonUtils.area(poly)) < threshold;
    }

    /**
     * Make a bisecting cut through the polygon
     * @param {Point[]} poly
     * @param {number} [attempt=0] - Retry counter
     * @returns {Point[][]} - Resulting polygon parts
     */
    makeCut(poly, attempt = 0) {
        if (attempt > 10) return [poly];

        const n = poly.length;

        // Get oriented bounding box
        let obb;
        if (attempt > 0) {
            // Try different orientations on retry
            const angle = (attempt / 10) * Math.PI * 2;
            const rotated = this.rotatePoly(poly, angle);
            const aabb = PolygonUtils.bounds(rotated);
            obb = this.unrotateBounds(aabb, angle, poly);
        } else {
            obb = PolygonUtils.obb(poly);
        }

        // Get the two axes of the OBB
        const v0 = obb[1].subtract(obb[0]);
        const v1 = obb[3].subtract(obb[0]);

        // Use the longer axis for cutting
        let longAxis, shortAxis;
        if (v0.length > v1.length) {
            longAxis = v0;
            shortAxis = v1;
        } else {
            longAxis = v1;
            shortAxis = v0;
        }

        // Find cut position along long axis
        const centroid = PolygonUtils.centroid(poly);
        const origin = obb[0];
        const centroidOffset = centroid.subtract(origin);
        const projectedT = centroidOffset.dot(longAxis.normalized()) / longAxis.length;

        // Add randomness to cut position
        const t = (projectedT + Random.gaussian() / 3) / 2 + 0.25;
        const cutPoint = origin.add(longAxis.scale(t));

        // Find entry and exit points
        const cutDir = shortAxis.normalized();
        const perpDir = new Point(-cutDir.y, cutDir.x);

        let entry = null;
        let exit = null;
        let entryIdx = -1;
        let exitIdx = -1;

        for (let i = 0; i < n; i++) {
            const p1 = poly[i];
            const p2 = poly[(i + 1) % n];

            const intersection = GeomUtils.lineSegmentIntersection(
                cutPoint, cutPoint.add(perpDir.scale(1000)),
                p1, p2
            );

            if (intersection) {
                const dist = intersection.subtract(cutPoint).dot(perpDir);
                if (dist > 0 && (!entry || dist < entry.subtract(cutPoint).dot(perpDir))) {
                    entry = intersection;
                    entryIdx = i;
                } else if (dist < 0 && (!exit || dist > exit.subtract(cutPoint).dot(perpDir))) {
                    exit = intersection;
                    exitIdx = i;
                }
            }
        }

        if (!entry || !exit || entryIdx === exitIdx) {
            return this.makeCut(poly, attempt + 1);
        }

        // Check if cut is too close to corners
        const entryDist1 = Point.distance(entry, poly[entryIdx]);
        const entryDist2 = Point.distance(entry, poly[(entryIdx + 1) % n]);
        const exitDist1 = Point.distance(exit, poly[exitIdx]);
        const exitDist2 = Point.distance(exit, poly[(exitIdx + 1) % n]);

        if (Math.min(entryDist1, entryDist2, exitDist1, exitDist2) < this.minTurnOffset) {
            return this.makeCut(poly, attempt + 1);
        }

        // Build the two resulting polygons
        const part1 = [];
        const part2 = [];

        // Part 1: from entry to exit
        part1.push(entry);
        for (let i = (entryIdx + 1) % n; i !== (exitIdx + 1) % n; i = (i + 1) % n) {
            part1.push(poly[i]);
        }
        part1.push(exit);

        // Part 2: from exit to entry
        part2.push(exit);
        for (let i = (exitIdx + 1) % n; i !== (entryIdx + 1) % n; i = (i + 1) % n) {
            part2.push(poly[i]);
        }
        part2.push(entry);

        // Validate results
        const area1 = Math.abs(PolygonUtils.area(part1));
        const area2 = Math.abs(PolygonUtils.area(part2));

        if (area1 < this.minArea / 4 || area2 < this.minArea / 4) {
            return this.makeCut(poly, attempt + 1);
        }

        this.cuts.push([entry, exit]);
        return [part1, part2];
    }

    /**
     * Rotate polygon for different OBB orientations
     * @param {Point[]} poly
     * @param {number} angle
     * @returns {Point[]}
     */
    rotatePoly(poly, angle) {
        const cos = Math.cos(angle);
        const sin = Math.sin(angle);

        return poly.map(p => new Point(
            p.x * cos - p.y * sin,
            p.x * sin + p.y * cos
        ));
    }

    /**
     * Convert rotated AABB back to original coordinates
     * @param {{x: number, y: number, width: number, height: number}} aabb
     * @param {number} angle
     * @param {Point[]} originalPoly
     * @returns {Point[]}
     */
    unrotateBounds(aabb, angle, originalPoly) {
        const cos = Math.cos(-angle);
        const sin = Math.sin(-angle);

        const corners = [
            new Point(aabb.x, aabb.y),
            new Point(aabb.x + aabb.width, aabb.y),
            new Point(aabb.x + aabb.width, aabb.y + aabb.height),
            new Point(aabb.x, aabb.y + aabb.height)
        ];

        return corners.map(p => new Point(
            p.x * cos - p.y * sin,
            p.x * sin + p.y * cos
        ));
    }
}
