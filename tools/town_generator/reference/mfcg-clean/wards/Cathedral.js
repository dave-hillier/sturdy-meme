/**
 * Cathedral.js - Cathedral/temple ward
 *
 * Represents a religious district with a large temple or cathedral.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';
import { Building } from '../model/Building.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

/**
 * Cathedral ward with religious buildings
 */
export class Cathedral extends Ward {
    /**
     * Create a cathedral ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {Point[][]} */
        this.building = [];
    }

    /**
     * Create geometry for the cathedral
     */
    createGeometry() {
        Random.restore(this.patch.seed);

        const available = this.getAvailable();
        if (!available) {
            this.building = [];
            return;
        }

        // Create symmetric building
        const rect = PolygonUtils.lira(available);
        const building = Building.create(rect, 20, false, true, 0.2);

        this.building = building ? [building] : [rect];
    }

    /**
     * Handle context menu
     * @param {*} menu
     * @param {number} x
     * @param {number} y
     */
    onContext(menu, x, y) {
        const point = new Point(x, y);

        for (const building of this.building) {
            if (PolygonUtils.containsPoint(building, point)) {
                // Could add context menu items
                break;
            }
        }
    }
}
