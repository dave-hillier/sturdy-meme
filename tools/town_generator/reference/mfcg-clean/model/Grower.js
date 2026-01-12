/**
 * Grower.js - District growth algorithms
 *
 * Handles expansion of districts by adding neighboring patches.
 */

import { Random } from '../core/Random.js';
import { Alleys } from '../wards/Alleys.js';
import { Park } from '../wards/Park.js';

/**
 * District type indices (matching original Haxe enum)
 */
export const DistrictType = {
    CENTRAL: 0,
    CASTLE: 1,
    DOCKS: 2,
    BRIDGE: 3,
    GATE: 4,
    PARK: 5,
    SPRAWL: 6,
    SLUM: 7
};

/**
 * Edge type indices for growth validation
 */
export const EdgeTypeIndex = {
    NORMAL: 0,
    MAIN: 1,
    ROAD: 2,
    WALL: 3,
    WATER: 4
};

/**
 * Base class for district growth algorithms
 */
export class Grower {
    /**
     * Create a grower for a district
     * @param {import('./District.js').District} district
     */
    constructor(district) {
        /** @type {import('./District.js').District} */
        this.district = district;

        // Set growth rate based on district type
        const typeIndex = district.type?._hx_index ?? district.typeIndex ?? 0;

        switch (typeIndex) {
            case DistrictType.CASTLE:
            case DistrictType.BRIDGE:
            case DistrictType.GATE:
                this.rate = 0.1;
                break;
            case DistrictType.PARK:
                this.rate = 0.5;
                break;
            default:
                this.rate = 1.0;
        }
    }

    /**
     * Attempt to grow the district by one cell
     * @param {import('./Cell.js').Cell[]} availableCells - Cells available for expansion
     * @returns {boolean} True if growth occurred
     */
    grow(availableCells) {
        if (this.rate === 0) {
            return false;
        }

        // Random chance to skip based on rate
        const skipChance = 1 - this.rate;
        if (Random.next() < skipChance) {
            return true; // Continue but don't grow this step
        }

        // Find candidate cells adjacent to district
        const candidates = [];

        for (const face of this.district.faces) {
            // Traverse half-edges around face
            let edge = face.halfEdge;
            const startEdge = edge;

            do {
                if (edge.twin) {
                    const neighborCell = edge.twin.face.data;

                    // Check if neighbor is available
                    if (availableCells.indexOf(neighborCell) !== -1) {
                        const patchScore = this.validatePatch(face.data, neighborCell);
                        const edgeScore = this.validateEdge(edge.data);
                        const score = patchScore * edgeScore;

                        if (Random.next() < score) {
                            candidates.push(neighborCell);
                        }
                    }
                }
                edge = edge.next;
            } while (edge !== startEdge);
        }

        // Pick a random candidate and add to district
        if (candidates.length > 0) {
            const chosen = Random.pick(candidates);
            this.district.faces.push(chosen.face);
            chosen.district = this.district;

            // Remove from available list
            const idx = availableCells.indexOf(chosen);
            if (idx !== -1) {
                availableCells.splice(idx, 1);
            }

            return true;
        }

        return false;
    }

    /**
     * Validate whether a patch can be added to district
     * @param {import('./Cell.js').Cell} currentCell - Cell already in district
     * @param {import('./Cell.js').Cell} candidateCell - Cell being considered
     * @returns {number} Score 0-1, higher = more likely to add
     */
    validatePatch(currentCell, candidateCell) {
        // Default: prefer cells with matching landing status
        return currentCell.landing === candidateCell.landing ? 1 : 0;
    }

    /**
     * Validate edge between cells
     * @param {*} edgeData - Edge data (e.g., road type)
     * @returns {number} Score 0-1
     */
    validateEdge(edgeData) {
        if (edgeData == null) {
            return 1;
        }

        const typeIndex = edgeData._hx_index ?? edgeData.typeIndex ?? 0;

        switch (typeIndex) {
            case EdgeTypeIndex.ROAD:
                return 0.9; // Slightly less likely to cross roads
            case EdgeTypeIndex.WALL:
            case EdgeTypeIndex.WATER:
                return 0; // Cannot cross walls or water
            default:
                return 1;
        }
    }
}

/**
 * Grower for dock/harbor districts
 * Prefers landing areas with Alleys wards
 */
export class DocksGrower extends Grower {
    /**
     * @param {import('./District.js').District} district
     */
    constructor(district) {
        super(district);
    }

    /**
     * @override
     */
    validatePatch(currentCell, candidateCell) {
        // Must be a landing with Alleys ward
        if (candidateCell.landing && candidateCell.ward instanceof Alleys) {
            return 1;
        }
        return 0;
    }
}

/**
 * Grower for park districts
 * Only accepts cells with Park wards
 */
export class ParkGrower extends Grower {
    /**
     * @param {import('./District.js').District} district
     */
    constructor(district) {
        super(district);
    }

    /**
     * @override
     */
    validatePatch(currentCell, candidateCell) {
        // Must have a Park ward
        if (candidateCell.ward instanceof Park) {
            return 1;
        }
        return 0;
    }
}
