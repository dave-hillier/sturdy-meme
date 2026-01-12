/**
 * Forester.js - Tree placement utility
 *
 * Places trees within polygonal areas using Poisson disk sampling
 * and noise-based density variation.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Noise } from '../utils/Noise.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { PoissonPattern, FillablePoly } from '../geometry/PoissonPattern.js';

/**
 * Forester - tree placement utility
 */
export class Forester {
    /** @type {PoissonPattern|null} */
    static pattern = null;

    /** @type {Noise|null} */
    static noise = null;

    /** @type {number} */
    static patternSize = 1000;

    /** @type {number} */
    static treeSpacing = 3;

    /**
     * Initialize the pattern and noise if needed
     */
    static init() {
        if (!Forester.pattern) {
            Forester.pattern = new PoissonPattern(
                Forester.patternSize,
                Forester.patternSize,
                Forester.treeSpacing
            );
        }

        if (!Forester.noise) {
            Forester.noise = Noise.fractal(4, 10, 0.5);
        }
    }

    /**
     * Fill an area with trees
     * @param {Point[]} polygon - Area to fill
     * @param {number} [density=1] - Tree density (0-1)
     * @returns {Point[]}
     */
    static fillArea(polygon, density = 1) {
        Forester.init();

        const fillable = new FillablePoly(polygon);
        const candidates = Forester.pattern.fill(fillable);

        if (density >= 1) {
            return candidates;
        }

        // Filter by noise-based density
        const result = [];
        for (const point of candidates) {
            const noiseValue = (Forester.noise.get(point.x, point.y) + 1) / 2;
            if (noiseValue < density) {
                result.push(point);
            }
        }

        return result;
    }

    /**
     * Fill a line with trees (e.g., along a road or river)
     * @param {Point} start - Start point
     * @param {Point} end - End point
     * @param {number} [density=1] - Tree density (0-1)
     * @returns {Point[]}
     */
    static fillLine(start, end, density = 1) {
        Forester.init();

        const dist = Point.distance(start, end);
        const count = Math.ceil(dist / Forester.treeSpacing);
        const result = [];

        for (let i = 0; i < count; i++) {
            const t = (i + Random.next()) / count;
            const point = GeomUtils.lerp(start, end, t);

            if (density < 1) {
                const noiseValue = (Forester.noise.get(point.x, point.y) + 1) / 2;
                if (noiseValue >= density) {
                    continue;
                }
            }

            result.push(point);
        }

        return result;
    }

    /**
     * Fill multiple polygons with trees
     * @param {Point[][]} polygons - Areas to fill
     * @param {number} [density=1] - Tree density (0-1)
     * @returns {Point[]}
     */
    static fillAreas(polygons, density = 1) {
        const result = [];

        for (const polygon of polygons) {
            result.push(...Forester.fillArea(polygon, density));
        }

        return result;
    }

    /**
     * Reset the pattern (useful for different random seeds)
     */
    static reset() {
        Forester.pattern = null;
        Forester.noise = null;
    }

    /**
     * Set tree spacing
     * @param {number} spacing
     */
    static setSpacing(spacing) {
        if (spacing !== Forester.treeSpacing) {
            Forester.treeSpacing = spacing;
            Forester.pattern = null; // Force regeneration
        }
    }
}
