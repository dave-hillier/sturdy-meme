/**
 * Canal.js - Rivers and canals
 *
 * Represents water features that flow through the city.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { EdgeChain } from '../geometry/DCEL.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { PolygonSmoother } from '../geometry/Chaikin.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { EdgeType } from './City.js';

export class Canal {
    /** Default river width */
    static DEFAULT_WIDTH = 4;

    /**
     * Create a canal
     * @param {import('./City.js').City} city
     * @param {import('../geometry/DCEL.js').HalfEdge[]} course - Edge chain defining the canal path
     */
    constructor(city, course) {
        /** @type {import('./City.js').City} */
        this.city = city;

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.course = course;

        /** @type {number} */
        this.width = Canal.DEFAULT_WIDTH;

        /** @type {Point[]} */
        this.path = [];

        /** @type {Point[]} */
        this.leftBank = [];

        /** @type {Point[]} */
        this.rightBank = [];

        // Process the course
        this.processCourse();
    }

    /**
     * Process the canal course for rendering
     */
    processCourse() {
        // Convert edge chain to point array
        let points = EdgeChain.toPolyline(this.course);

        // Adjust endpoints at shore if needed
        if (this.city.waterEdge.length > 0 && points.length > 0) {
            const first = points[0];
            const second = points[1];
            // Shift first point slightly toward second
            points[0] = GeomUtils.lerp(first, second);

            // Update shore points for smooth connection
            const shoreIdx = this.city.shore.indexOf(first);
            if (shoreIdx >= 2) {
                const newPos = GeomUtils.lerp(
                    this.city.shore[shoreIdx - 1],
                    GeomUtils.lerp(this.city.shore[shoreIdx - 2], first)
                );
                this.city.shore[shoreIdx - 1].copyFrom(newPos);
            }
            if (shoreIdx < this.city.shore.length - 2) {
                const newPos = GeomUtils.lerp(
                    this.city.shore[shoreIdx + 1],
                    GeomUtils.lerp(this.city.shore[shoreIdx + 2], first)
                );
                this.city.shore[shoreIdx + 1].copyFrom(newPos);
            }
        }

        // Smooth the path
        points = PolygonSmoother.smoothOpen(points, null, 1);
        this.path = points;

        // Mark edges as canal type
        EdgeChain.assignData(this.course, EdgeType.CANAL);

        // Adjust path at wall intersection if city has walls
        if (this.city.wall) {
            this.adjustAtWall();
        }
    }

    /**
     * Adjust canal path where it crosses the city wall
     */
    adjustAtWall() {
        const wallShape = this.city.wall.shape;

        for (let i = 1; i < this.path.length - 1; i++) {
            const point = this.path[i];
            const wallIdx = wallShape.indexOf(point);

            if (wallIdx >= 0) {
                // Point is on wall - adjust for gate opening
                const prev = this.path[i - 1];
                const next = this.path[i + 1];
                const dir = next.subtract(prev).normalized();

                const wallLen = wallShape.length;
                const wallPrev = wallShape[(wallIdx + wallLen - 1) % wallLen];
                const wallNext = wallShape[(wallIdx + 1) % wallLen];

                // Find intersection of canal direction with wall edge
                const intersection = GeomUtils.intersectLines(
                    prev.x, prev.y, dir.x, dir.y,
                    wallPrev.x, wallPrev.y,
                    wallNext.x - wallPrev.x, wallNext.y - wallPrev.y
                );

                if (isFinite(intersection.x)) {
                    const newPoint = new Point(
                        prev.x + dir.x * intersection.x,
                        prev.y + dir.y * intersection.x
                    );
                    // Move point slightly toward the calculated intersection
                    this.path[i] = GeomUtils.lerp(point, newPoint);
                }
            }
        }
    }

    /**
     * Update state for rendering
     */
    updateState() {
        this.computeBanks();
    }

    /**
     * Compute left and right bank polygons
     */
    computeBanks() {
        if (this.path.length < 2) return;

        this.leftBank = [];
        this.rightBank = [];

        const halfWidth = this.width / 2;

        for (let i = 0; i < this.path.length; i++) {
            const curr = this.path[i];

            // Compute tangent direction
            let tangent;
            if (i === 0) {
                tangent = this.path[1].subtract(curr);
            } else if (i === this.path.length - 1) {
                tangent = curr.subtract(this.path[i - 1]);
            } else {
                tangent = this.path[i + 1].subtract(this.path[i - 1]);
            }
            tangent = tangent.normalized();

            // Perpendicular (normal)
            const normal = new Point(-tangent.y, tangent.x);

            this.leftBank.push(curr.add(normal.scale(halfWidth)));
            this.rightBank.push(curr.add(normal.scale(-halfWidth)));
        }
    }

    /**
     * Get the full canal polygon (both banks)
     * @returns {Point[]}
     */
    getPolygon() {
        return [...this.leftBank, ...this.rightBank.slice().reverse()];
    }

    /**
     * Create a river through the city
     * @param {import('./City.js').City} city
     * @returns {Canal}
     */
    static createRiver(city) {
        // Find path from shore through city to opposite shore or edge
        const shoreVertices = city.shoreE.map(e => e.origin);

        if (shoreVertices.length === 0) {
            return null;
        }

        // Pick a starting vertex on the shore
        const startVertex = Random.pick(shoreVertices);

        // Build path through city cells toward opposite side
        const path = [startVertex];
        const visited = new Set([startVertex]);

        let current = startVertex;
        const targetDir = city.center.subtract(startVertex.point).normalized();

        for (let step = 0; step < 100; step++) {
            // Find adjacent vertices not yet visited
            const candidates = [];

            for (const edge of current.edges) {
                const dest = edge.destination;
                if (!dest || visited.has(dest)) continue;

                // Score based on direction toward center (for first half)
                // then away from center (for second half)
                const dir = dest.point.subtract(current.point).normalized();
                let score = dir.dot(targetDir);

                // Prefer vertices in city cells
                const cell = edge.face.data;
                if (cell && cell.withinCity) {
                    score += 0.5;
                }

                candidates.push({ vertex: dest, score });
            }

            if (candidates.length === 0) break;

            // Pick best candidate with some randomness
            candidates.sort((a, b) => b.score - a.score);
            const choice = candidates[Math.min(Random.int(3), candidates.length - 1)];

            current = choice.vertex;
            visited.add(current);
            path.push(current);

            // Check if we've reached the edge
            const onEdge = current.edges.some(e => !e.twin);
            if (onEdge) break;
        }

        // Convert vertex path to edge chain
        const edges = city.dcel.vertices2chain(path);

        if (edges.length === 0) return null;

        return new Canal(city, edges);
    }
}
