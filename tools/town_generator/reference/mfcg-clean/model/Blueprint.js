/**
 * Blueprint.js - City generation blueprint/parameters
 *
 * Defines all the parameters that control city generation.
 */

import { Random } from '../core/Random.js';

export class Blueprint {
    /**
     * Create a blueprint
     * @param {number} size - City size (number of patches)
     * @param {number} seed - Random seed
     */
    constructor(size, seed) {
        /** @type {number} */
        this.size = size;

        /** @type {number} */
        this.seed = seed;

        /** @type {string|null} */
        this.name = null;

        /** @type {number} */
        this.pop = 0;

        /** @type {boolean} */
        this.citadel = true;

        /** @type {boolean} */
        this.inner = false; // Urban castle (stadtburg)

        /** @type {boolean} */
        this.plaza = true;

        /** @type {boolean} */
        this.temple = true;

        /** @type {boolean} */
        this.walls = true;

        /** @type {boolean} */
        this.shanty = false;

        /** @type {boolean} */
        this.coast = true;

        /** @type {number|null} */
        this.coastDir = null;

        /** @type {boolean} */
        this.river = true;

        /** @type {boolean} */
        this.greens = false;

        /** @type {boolean} */
        this.hub = false;

        /** @type {number} */
        this.gates = -1;

        /** @type {boolean} */
        this.random = true;

        /** @type {string|null} */
        this.style = null;

        /** @type {string|null} */
        this.export = null;
    }

    /**
     * Create a randomized blueprint
     * @param {number} size - City size
     * @param {number} seed - Random seed
     * @returns {Blueprint}
     */
    static create(size, seed) {
        Random.seed = seed;

        const bp = new Blueprint(size, seed);
        bp.name = null;
        bp.pop = 0;
        bp.greens = false;
        bp.random = true;

        // Randomize features based on size
        const wallsChance = (size + 30) / 80;
        bp.walls = Random.bool(wallsChance);

        const shantyChance = size / 80;
        bp.shanty = Random.bool(shantyChance);

        const citadelChance = 0.5 + size / 100;
        bp.citadel = Random.bool(citadelChance);

        const innerChance = bp.walls ? size / (size + 30) : 0.5;
        bp.inner = Random.bool(innerChance);

        bp.plaza = Random.bool(0.9);

        const templeChance = size / 18;
        bp.temple = Random.bool(templeChance);

        bp.river = Random.bool(0.667);
        bp.coast = Random.bool(0.5);

        return bp;
    }

    /**
     * Create a similar blueprint (same parameters, new seed)
     * @param {Blueprint} original
     * @returns {Blueprint}
     */
    static similar(original) {
        const bp = Blueprint.create(original.size, original.seed);
        bp.name = original.name;
        return bp;
    }

    /**
     * Create blueprint from URL parameters
     * @param {URLSearchParams} params
     * @returns {Blueprint|null}
     */
    static fromURL(params) {
        const size = parseInt(params.get('size')) || 0;
        const seed = parseInt(params.get('seed')) || 0;

        if (size === 0 || seed === 0) return null;

        const bp = new Blueprint(size, seed);
        bp.random = false;

        bp.name = params.get('name') || null;
        bp.pop = parseInt(params.get('population')) || 0;

        bp.citadel = params.get('citadel') !== '0';
        bp.inner = params.get('urban_castle') === '1';
        bp.plaza = params.get('plaza') !== '0';
        bp.temple = params.get('temple') !== '0';
        bp.walls = params.get('walls') !== '0';
        bp.shanty = params.get('shantytown') === '1';
        bp.river = params.get('river') === '1';
        bp.coast = params.get('coast') !== '0';
        bp.greens = params.get('greens') === '1';
        bp.hub = params.get('hub') === '1';
        bp.gates = parseInt(params.get('gates')) || -1;

        const sea = params.get('sea');
        bp.coastDir = sea ? parseFloat(sea) : null;

        bp.style = params.get('style') || null;
        bp.export = params.get('export') || null;

        return bp;
    }

    /**
     * Generate URL query string from blueprint
     * @returns {string}
     */
    toURL() {
        const params = new URLSearchParams();

        params.set('size', this.size.toString());
        params.set('seed', this.seed.toString());

        if (this.name) params.set('name', this.name);
        if (this.pop > 0) params.set('population', this.pop.toString());

        if (!this.citadel) params.set('citadel', '0');
        if (this.inner) params.set('urban_castle', '1');
        if (!this.plaza) params.set('plaza', '0');
        if (!this.temple) params.set('temple', '0');
        if (!this.walls) params.set('walls', '0');
        if (this.shanty) params.set('shantytown', '1');
        if (!this.coast) params.set('coast', '0');
        if (this.river) params.set('river', '1');
        if (this.greens) params.set('greens', '1');

        if (this.hub) {
            params.set('hub', '1');
        } else if (this.gates >= 0) {
            params.set('gates', this.gates.toString());
        }

        if (this.coast && this.coastDir !== null) {
            params.set('sea', this.coastDir.toString());
        }

        return params.toString();
    }

    /**
     * Clone this blueprint
     * @returns {Blueprint}
     */
    clone() {
        const bp = new Blueprint(this.size, this.seed);
        Object.assign(bp, this);
        return bp;
    }
}
