/**
 * CurtainWall.js - City wall with towers and gates
 *
 * Represents a defensive wall around the city or castle.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { DCEL, EdgeChain } from '../geometry/DCEL.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { PolygonSmoother } from '../geometry/Chaikin.js';
import { EdgeType } from './City.js';

export class CurtainWall {
    /** Wall thickness constant */
    static THICKNESS = 2.4;

    /** Large tower radius */
    static LTOWER_RADIUS = 2.5;

    /** Small tower radius */
    static STOWER_RADIUS = 1.5;

    /**
     * Create a curtain wall
     * @param {boolean} real - Whether this is a real wall (vs virtual border)
     * @param {import('./City.js').City} city - The city
     * @param {import('./Cell.js').Cell[]} patches - Cells enclosed by wall
     * @param {Point[]} excludePoints - Points to avoid for gate placement
     */
    constructor(real, city, patches, excludePoints = []) {
        /** @type {boolean} */
        this.real = real;

        /** @type {import('./Cell.js').Cell[]} */
        this.patches = patches;

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.edges = [];

        /** @type {Point[]} */
        this.shape = [];

        /** @type {import('../geometry/DCEL.js').Vertex[]} */
        this.gates = [];

        /** @type {import('../geometry/DCEL.js').Vertex[]} */
        this.towers = [];

        /** @type {boolean[]} */
        this.segments = [];

        /** @type {Map<import('../geometry/DCEL.js').Vertex, number>} */
        this.watergates = new Map();

        // Build wall edges
        if (patches.length === 1) {
            // Single cell (castle)
            this.edges = [];
            for (const edge of patches[0].face.edges()) {
                this.edges.push(edge);
            }
            this.shape = patches[0].shape;
        } else {
            // Multi-cell wall
            const faces = patches.map(p => p.face);
            this.edges = DCEL.circumference(null, faces);
            this.shape = EdgeChain.toPoly(this.edges);
        }

        // Mark edges as wall type
        if (real) {
            EdgeChain.assignData(this.edges, EdgeType.WALL, false);

            // Smooth the wall shape (but avoid excluded points)
            if (patches.length > 1) {
                const smoothed = PolygonSmoother.smooth(this.shape, excludePoints, 3);
                PolygonUtils.set(this.shape, smoothed);
            }
        }

        this.length = this.shape.length;

        // Build gates
        if (patches.length === 1) {
            this.buildCastleGate(city, excludePoints);
        } else {
            this.buildCityGates(real, city, excludePoints);
        }

        // Initialize segment visibility
        this.segments = this.shape.map(() => true);
    }

    /**
     * Build gate for a castle (single entrance)
     * @param {import('./City.js').City} city
     * @param {Point[]} excludePoints
     */
    buildCastleGate(city, excludePoints) {
        // Find edge pointing most toward city center
        let bestEdge = null;
        let bestScore = -Infinity;

        for (const edge of this.edges) {
            const mid = Point.midpoint(edge.origin.point, edge.destination.point);
            const dir = city.center.subtract(mid);
            const edgeDir = edge.destination.point.subtract(edge.origin.point);

            // Prefer edges facing the center
            const score = -dir.dot(edgeDir.perpendicular()) / dir.length;

            // Penalize if near excluded points
            let penalty = 0;
            for (const p of excludePoints) {
                const dist = Math.min(
                    Point.distance(p, edge.origin.point),
                    Point.distance(p, edge.destination.point)
                );
                if (dist < 5) {
                    penalty += 10 / dist;
                }
            }

            if (score - penalty > bestScore) {
                bestScore = score - penalty;
                bestEdge = edge;
            }
        }

        if (bestEdge) {
            this.gates.push(bestEdge.origin);
        }
    }

    /**
     * Build gates for city wall
     * @param {boolean} real
     * @param {import('./City.js').City} city
     * @param {Point[]} excludePoints
     */
    buildCityGates(real, city, excludePoints) {
        // Determine number of gates based on city size
        let numGates = city.bp.gates;
        if (numGates < 0) {
            numGates = Math.max(2, Math.floor(city.nPatches / 10));
        }

        if (city.bp.hub) {
            // Hub mode - gates at regular intervals
            const interval = this.edges.length / numGates;
            for (let i = 0; i < numGates; i++) {
                const idx = Math.floor(i * interval);
                this.gates.push(this.edges[idx].origin);
            }
            return;
        }

        // Find good gate locations based on road connectivity
        const candidates = [];
        const excludeSet = new Set(excludePoints);

        for (let i = 0; i < this.edges.length; i++) {
            const edge = this.edges[i];
            const vertex = edge.origin;

            // Skip if near excluded points
            let skip = false;
            for (const p of excludePoints) {
                if (Point.distance(p, vertex.point) < CurtainWall.LTOWER_RADIUS * 2) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            // Score based on distance from center (prefer gates away from center)
            const distFromCenter = Point.distance(vertex.point, city.center);
            candidates.push({ vertex, score: distFromCenter, index: i });
        }

        // Sort by score and pick best gates with minimum spacing
        candidates.sort((a, b) => b.score - a.score);

        const minSpacing = this.edges.length / (numGates * 2);

        for (const candidate of candidates) {
            if (this.gates.length >= numGates) break;

            // Check spacing from existing gates
            let tooClose = false;
            for (const gate of this.gates) {
                const gateIdx = this.edges.findIndex(e => e.origin === gate);
                const dist = Math.min(
                    Math.abs(candidate.index - gateIdx),
                    this.edges.length - Math.abs(candidate.index - gateIdx)
                );
                if (dist < minSpacing) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) {
                this.gates.push(candidate.vertex);
            }
        }
    }

    /**
     * Build towers along the wall
     */
    buildTowers() {
        this.towers = [];

        // Add towers at gates
        for (const gate of this.gates) {
            if (!this.towers.includes(gate)) {
                this.towers.push(gate);
            }
        }

        // Add towers at corners (convex vertices)
        for (let i = 0; i < this.shape.length; i++) {
            if (this.segments[i] && PolygonUtils.isConvexVertex(this.shape, i)) {
                const vertex = this.edges[i]?.origin;
                if (vertex && !this.towers.includes(vertex)) {
                    this.towers.push(vertex);
                }
            }
        }

        // Add towers at regular intervals on long segments
        const maxSpacing = 15;

        for (let i = 0; i < this.edges.length; i++) {
            if (!this.segments[i]) continue;

            const edge = this.edges[i];
            const length = edge.length;

            if (length > maxSpacing * 1.5) {
                // Add intermediate towers
                const numTowers = Math.ceil(length / maxSpacing) - 1;
                // Note: This would require splitting edges which is complex
            }
        }
    }

    /**
     * Get tower radius at a vertex
     * @param {import('../geometry/DCEL.js').Vertex} vertex
     * @returns {number}
     */
    getTowerRadius(vertex) {
        if (this.gates.includes(vertex)) {
            return CurtainWall.LTOWER_RADIUS;
        }
        if (this.towers.includes(vertex)) {
            return CurtainWall.STOWER_RADIUS;
        }
        return 0;
    }

    /**
     * Get the wall polygon offset outward
     * @returns {Point[]}
     */
    getOuterShape() {
        return PolygonUtils.shrink(this.shape, -CurtainWall.THICKNESS);
    }

    /**
     * Get the wall polygon offset inward
     * @returns {Point[]}
     */
    getInnerShape() {
        return PolygonUtils.shrink(this.shape, CurtainWall.THICKNESS);
    }
}
