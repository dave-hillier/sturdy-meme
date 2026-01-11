/**
 * Ward.js - Base ward class
 *
 * A ward is a functional area within a city cell.
 * Different ward types include markets, cathedrals, farms, etc.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { CurtainWall } from '../model/CurtainWall.js';
import { EdgeChain } from '../geometry/DCEL.js';

/**
 * Base class for all ward types
 */
export class Ward {
    /**
     * Create a ward
     * @param {import('../model/City.js').City} model - The city
     * @param {import('../model/Cell.js').Cell} patch - The cell this ward occupies
     */
    constructor(model, patch) {
        /** @type {import('../model/City.js').City} */
        this.model = model;

        /** @type {import('../model/Cell.js').Cell} */
        this.patch = patch;

        // Link patch to this ward
        patch.ward = this;

        // Store seed for reproducible geometry
        patch.seed = Random.seed;
    }

    /**
     * Create geometry for this ward
     * Override in subclasses
     */
    createGeometry() {
        // Default: no geometry
    }

    /**
     * Spawn trees for this ward
     * Override in subclasses
     * @returns {Point[]|null}
     */
    spawnTrees() {
        return null;
    }

    /**
     * Get available area within the cell (after insets)
     * @returns {Point[]|null}
     */
    getAvailable() {
        const shape = this.patch.shape;
        const n = shape.length;

        // Calculate tower radii for each vertex
        const towerRadii = [];
        for (const edge of this.patch.face.edges()) {
            const vertex = edge.origin;
            let maxRadius = 0;

            for (const wall of this.model.walls) {
                maxRadius = Math.max(maxRadius, wall.getTowerRadius(vertex));
            }

            // Check canals
            for (const canal of this.model.canals) {
                if (EdgeChain.edgeByOrigin(canal.course, vertex)) {
                    maxRadius = Math.max(maxRadius, canal.width);
                }
            }

            towerRadii.push(maxRadius);
        }

        // Calculate edge insets
        const edgeInsets = [];
        let i = 0;
        for (const edge of this.patch.face.edges()) {
            // Check for canal at this edge
            for (const canal of this.model.canals) {
                if (EdgeChain.edgeByOrigin(canal.course, edge.origin)) {
                    towerRadii[i] = canal.width / 2 + 1.2;
                    if (edge.origin === canal.course[0].origin) {
                        towerRadii[i] += 1.2;
                    }
                }
            }

            // Determine edge type for inset
            let inset = 0;
            if (edge.data === null) {
                // Check if adjacent to plaza
                if (edge.twin && edge.twin.face.data === this.model.plaza) {
                    inset = 2 / 2;
                } else {
                    inset = 1.2 / 2;
                }
            } else {
                switch (edge.data) {
                    case 1: // COAST
                        inset = this.patch.landing ? 2 : 1.2;
                        break;
                    case 2: // ROAD
                        inset = 1;
                        break;
                    case 3: // WALL
                        inset = CurtainWall.THICKNESS / 2 + 1.2;
                        break;
                    case 4: // CANAL
                        inset = this.model.canals[0]?.width / 2 + 1.2 || 2;
                        break;
                    default:
                        inset = 0;
                }
            }
            edgeInsets.push(inset);
            i++;
        }

        return Ward.inset(shape, edgeInsets, towerRadii);
    }

    /**
     * Inset polygon with corner rounding
     * @param {Point[]} poly
     * @param {number[]} edgeInsets
     * @param {number[]} cornerRadii
     * @returns {Point[]|null}
     */
    static inset(poly, edgeInsets, cornerRadii) {
        // Simple inset without corner rounding
        const insetPoly = PolygonUtils.inset(poly, edgeInsets);
        if (!insetPoly) return null;

        // Add corner cutoffs for tower locations
        const n = cornerRadii.length;
        let result = insetPoly;

        for (let i = 0; i < n; i++) {
            const radius = cornerRadii[i];
            const prevRadius = cornerRadii[(i + n - 1) % n];

            if (radius > edgeInsets[i] && radius > edgeInsets[(i + n - 1) % n]) {
                // Create circular cutoff at corner
                const corner = poly[i];
                const cutoff = PolygonUtils.regular(9, radius, Random.next());
                PolygonUtils.translateBy(cutoff, corner);

                // Subtract from result (simplified - just skip for now)
                // Full implementation would use polygon boolean operations
            }
        }

        return result;
    }

    /**
     * Get display label for this ward
     * @returns {string|null}
     */
    getLabel() {
        if (this.patch.district) {
            return this.patch.district.name;
        }
        return null;
    }

    /**
     * Get color for this ward
     * @returns {number}
     */
    getColor() {
        if (this.patch.district) {
            return this.patch.district.color;
        }
        return 0;
    }

    /**
     * Handle context menu
     * @param {*} menu
     * @param {number} x
     * @param {number} y
     */
    onContext(menu, x, y) {
        // Override in subclasses
    }
}
