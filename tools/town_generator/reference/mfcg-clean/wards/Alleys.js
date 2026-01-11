/**
 * Alleys.js - Urban residential ward
 *
 * The most common ward type, representing dense urban areas
 * with buildings, streets, and alleys.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';

/**
 * Urban alley ward with buildings and streets
 */
export class Alleys extends Ward {
    /**
     * Create an alleys ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {import('./WardGroup.js').WardGroup|null} */
        this.group = null;

        /** @type {Point[]|null} */
        this.trees = null;
    }

    /**
     * Create geometry for this ward
     */
    createGeometry() {
        // Geometry is created by WardGroup for all alleys in group
        if (this.group && this.group.core === this.patch.face) {
            this.group.createGeometry();
        }
        this.trees = null;
    }

    /**
     * Spawn trees for this ward
     * @returns {Point[]|null}
     */
    spawnTrees() {
        if (this.group && this.group.core === this.patch.face && this.trees === null) {
            this.trees = [];
            for (const block of this.group.blocks) {
                const blockTrees = block.spawnTrees();
                this.trees.push(...blockTrees);
            }
        }
        return this.trees;
    }

    /**
     * Handle context menu
     * @param {*} menu
     * @param {number} x
     * @param {number} y
     */
    onContext(menu, x, y) {
        // Check which building was clicked
        if (!this.group) return;

        const point = new Point(x, y);

        for (const block of this.group.blocks) {
            if (block.containsPoint(point)) {
                // Could add "Open in Dwellings" option here
                break;
            }
        }
    }
}
