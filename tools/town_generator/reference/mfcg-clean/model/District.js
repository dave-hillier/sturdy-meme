/**
 * District.js - City district/neighborhood
 *
 * Represents a named district of the city containing multiple wards.
 */

import { Random } from '../core/Random.js';
import { DCEL, EdgeChain } from '../geometry/DCEL.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

/**
 * Alley configuration for block generation
 */
export class AlleyConfig {
    constructor() {
        /** @type {number} Minimum block area */
        this.minSq = 100;

        /** @type {number} Block size multiplier */
        this.blockSize = 4;

        /** @type {number} Size variation */
        this.sizeChaos = 0.5;

        /** @type {number} Grid irregularity */
        this.gridChaos = 0.3;

        /** @type {number} Minimum frontage length */
        this.minFront = 3;
    }
}

/**
 * City district
 */
export class District {
    /**
     * Create a district
     * @param {import('../geometry/DCEL.js').Face[]} faces - DCEL faces in this district
     */
    constructor(faces) {
        /** @type {import('../geometry/DCEL.js').Face[]} */
        this.faces = faces;

        /** @type {string} */
        this.name = '';

        /** @type {number} Color (as 0xRRGGBB) */
        this.color = 0x808080;

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.border = [];

        /** @type {import('../core/Point.js').Point[]} */
        this.shape = [];

        /** @type {import('./WardGroup.js').WardGroup[]} */
        this.groups = [];

        /** @type {AlleyConfig} */
        this.alleys = new AlleyConfig();

        /** @type {number} Tree density for parks (0-1) */
        this.greenery = 0.3;

        // Compute border and shape
        if (faces.length > 0) {
            this.border = DCEL.circumference(null, faces);
            this.shape = EdgeChain.toPoly(this.border);
        }

        // Link faces to this district
        for (const face of faces) {
            if (face.data) {
                face.data.district = this;
            }
        }
    }

    /**
     * Get centroid of district
     * @returns {import('../core/Point.js').Point}
     */
    get center() {
        return PolygonUtils.centroid(this.shape);
    }

    /**
     * Get area of district
     * @returns {number}
     */
    get area() {
        return Math.abs(PolygonUtils.area(this.shape));
    }

    /**
     * Update geometry after changes
     */
    updateGeometry() {
        for (const group of this.groups) {
            group.createGeometry();
        }
    }

    /**
     * Generate a random district color
     * @returns {number}
     */
    static randomColor() {
        const h = Random.next();
        const s = 0.3 + Random.next() * 0.3;
        const l = 0.6 + Random.next() * 0.2;

        // HSL to RGB conversion
        const c = (1 - Math.abs(2 * l - 1)) * s;
        const x = c * (1 - Math.abs((h * 6) % 2 - 1));
        const m = l - c / 2;

        let r, g, b;
        const hi = Math.floor(h * 6);

        switch (hi) {
            case 0: r = c; g = x; b = 0; break;
            case 1: r = x; g = c; b = 0; break;
            case 2: r = 0; g = c; b = x; break;
            case 3: r = 0; g = x; b = c; break;
            case 4: r = x; g = 0; b = c; break;
            default: r = c; g = 0; b = x;
        }

        return (Math.floor((r + m) * 255) << 16) |
               (Math.floor((g + m) * 255) << 8) |
               Math.floor((b + m) * 255);
    }
}

/**
 * Builder for organizing cells into districts
 */
export class DistrictBuilder {
    /**
     * Create district builder
     * @param {import('./City.js').City} city
     */
    constructor(city) {
        /** @type {import('./City.js').City} */
        this.city = city;

        /** @type {District[]} */
        this.districts = [];
    }

    /**
     * Build districts from city cells
     */
    build() {
        // Group cells into districts based on connectivity and type
        const innerFaces = [];

        for (const cell of this.city.inner) {
            if (cell.withinCity && !cell.waterbody) {
                innerFaces.push(cell.face);
            }
        }

        if (innerFaces.length === 0) {
            return this.districts;
        }

        // For now, create one district per connected component
        const components = DCEL.split(innerFaces);

        for (const component of components) {
            const district = new District(component);
            district.name = this.generateDistrictName();
            district.color = District.randomColor();

            // Configure alley settings based on district position
            const center = district.center;
            const distFromCenter = center.length;

            // Denser blocks near center
            district.alleys.minSq = 80 + distFromCenter * 2;
            district.alleys.blockSize = 3 + distFromCenter / 20;

            this.districts.push(district);
        }

        return this.districts;
    }

    /**
     * Generate a district name
     * @returns {string}
     */
    generateDistrictName() {
        const prefixes = ['Old', 'New', 'Upper', 'Lower', 'East', 'West', 'North', 'South'];
        const types = ['Quarter', 'Ward', 'District', 'Gate', 'Market', 'Square'];
        const names = ['Merchant', 'Temple', 'Castle', 'River', 'Harbor', 'Garden', 'Crown', 'Stone'];

        if (Random.bool(0.3)) {
            return Random.pick(prefixes) + ' ' + Random.pick(names) + ' ' + Random.pick(types);
        } else {
            return Random.pick(names) + ' ' + Random.pick(types);
        }
    }
}
