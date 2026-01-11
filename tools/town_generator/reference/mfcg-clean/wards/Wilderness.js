/**
 * Wilderness.js - Wilderness/forest ward
 *
 * Represents undeveloped areas outside the city.
 */

import { Ward } from './Ward.js';

/**
 * Wilderness ward (empty, natural area)
 */
export class Wilderness extends Ward {
    /**
     * Create a wilderness ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);
    }

    // Wilderness has no geometry - it's empty space
}
