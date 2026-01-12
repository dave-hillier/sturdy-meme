/**
 * Landmark.js - Custom map markers
 *
 * Handles placement of landmarks at specific positions in the city,
 * tracking them using barycentric coordinates so they move correctly
 * when the underlying geometry changes.
 */

import { Point } from '../core/Point.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

/**
 * A landmark/marker placed on the map
 */
export class Landmark {
    /**
     * Create a landmark
     *
     * @param {import('./City.js').City} model - The city model
     * @param {Point} pos - Initial position
     * @param {string} [name='Landmark'] - Landmark name
     */
    constructor(model, pos, name = 'Landmark') {
        /** @type {import('./City.js').City} */
        this.model = model;

        /** @type {Point} */
        this.pos = pos;

        /** @type {string} */
        this.name = name;

        // Triangle vertices for barycentric interpolation
        /** @type {Point|null} */
        this.p0 = null;
        /** @type {Point|null} */
        this.p1 = null;
        /** @type {Point|null} */
        this.p2 = null;

        // Barycentric coordinates
        /** @type {number} */
        this.i0 = 0;
        /** @type {number} */
        this.i1 = 0;
        /** @type {number} */
        this.i2 = 0;

        // Find the cell containing this position
        this.assign();
    }

    /**
     * Find the cell containing this landmark and compute barycentric coords
     */
    assign() {
        for (const cell of this.model.cells) {
            if (this.assignPoly(cell.shape)) {
                break;
            }
        }
    }

    /**
     * Try to assign landmark to a polygon
     *
     * @param {Point[]} poly - Polygon to check
     * @returns {boolean} True if landmark was assigned to this polygon
     */
    assignPoly(poly) {
        // Quick bounds check
        const bounds = PolygonUtils.bounds(poly);
        if (this.pos.x < bounds.x || this.pos.x > bounds.x + bounds.width ||
            this.pos.y < bounds.y || this.pos.y > bounds.y + bounds.height) {
            return false;
        }

        // Triangulate from first vertex and check each triangle
        const n = poly.length;
        this.p0 = poly[0];

        for (let i = 2; i < n; i++) {
            this.p1 = poly[i - 1];
            this.p2 = poly[i];

            // Compute barycentric coordinates
            const bary = GeomUtils.barycentric(this.p0, this.p1, this.p2, this.pos);

            // Check if point is inside triangle
            if (bary.x >= 0 && bary.y >= 0 && bary.z >= 0) {
                this.i0 = bary.x;
                this.i1 = bary.y;
                this.i2 = bary.z;
                return true;
            }
        }

        return false;
    }

    /**
     * Update position from barycentric coordinates
     * Call this after geometry has changed
     */
    update() {
        if (!this.p0 || !this.p1 || !this.p2) {
            return;
        }

        // Reconstruct position from barycentric coordinates
        this.pos = new Point(
            this.p0.x * this.i0 + this.p1.x * this.i1 + this.p2.x * this.i2,
            this.p0.y * this.i0 + this.p1.y * this.i1 + this.p2.y * this.i2
        );
    }

    /**
     * Move landmark to a new position
     *
     * @param {Point} newPos - New position
     */
    moveTo(newPos) {
        this.pos = newPos;
        this.assign();
    }

    /**
     * Check if landmark is valid (assigned to a polygon)
     * @returns {boolean}
     */
    get isValid() {
        return this.p0 !== null;
    }

    /**
     * Create a landmark at the center of a cell
     *
     * @param {import('./City.js').City} model - The city model
     * @param {import('./Cell.js').Cell} cell - The cell
     * @param {string} [name] - Landmark name
     * @returns {Landmark}
     */
    static atCellCenter(model, cell, name) {
        const center = PolygonUtils.centroid(cell.shape);
        return new Landmark(model, center, name);
    }

    /**
     * Create a landmark at a random position within a cell
     *
     * @param {import('./City.js').City} model - The city model
     * @param {import('./Cell.js').Cell} cell - The cell
     * @param {string} [name] - Landmark name
     * @returns {Landmark}
     */
    static randomInCell(model, cell, name) {
        // Simple approach: use centroid with some offset
        const center = PolygonUtils.centroid(cell.shape);
        const bounds = PolygonUtils.bounds(cell.shape);

        // Try random positions until we find one inside
        for (let attempts = 0; attempts < 20; attempts++) {
            const pos = new Point(
                center.x + (Math.random() - 0.5) * bounds.width * 0.5,
                center.y + (Math.random() - 0.5) * bounds.height * 0.5
            );

            if (PolygonUtils.containsPoint(cell.shape, pos)) {
                return new Landmark(model, pos, name);
            }
        }

        // Fallback to center
        return new Landmark(model, center, name);
    }
}
