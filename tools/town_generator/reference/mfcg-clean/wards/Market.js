/**
 * Market.js - Market square ward
 *
 * Represents the central market plaza of the city.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { GeomUtils } from '../geometry/GeomUtils.js';

/**
 * Market square ward
 */
export class Market extends Ward {
    /**
     * Create a market ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {Point[]} */
        this.space = [];

        /** @type {Point[]} */
        this.monument = [];
    }

    /**
     * Get available area (market-specific)
     * @returns {Point[]|null}
     */
    getAvailable() {
        const shape = this.patch.shape;
        const n = shape.length;

        const cornerRadii = new Array(n).fill(0);
        const edgeInsets = [];

        let i = 0;
        for (const edge of this.patch.face.edges()) {
            // Check for canal at vertex
            for (const canal of this.model.canals) {
                const courseEdge = canal.course.find(e => e.origin === edge.origin);
                if (courseEdge) {
                    cornerRadii[i] = canal.width / 2;
                    if (edge.origin === canal.course[0].origin) {
                        cornerRadii[i] += 1.2;
                    }
                }
            }

            // Edge inset based on type
            if (edge.data === 4) { // CANAL
                edgeInsets.push(this.model.canals[0]?.width / 2 || 2);
            } else {
                edgeInsets.push(0);
            }
            i++;
        }

        return Ward.inset(shape, edgeInsets, cornerRadii);
    }

    /**
     * Create market geometry
     */
    createGeometry() {
        Random.restore(this.patch.seed);

        this.space = this.getAvailable();
        if (!this.space) {
            this.space = this.patch.shape.slice();
        }

        // Decide monument type
        const hasRect = Random.bool(0.6);
        const hasMonument = hasRect || Random.bool(0.3);

        let monumentPos = null;
        let monumentDir = null;

        if (hasMonument) {
            // Find longest edge for orientation
            const longestIdx = PolygonUtils.longestEdge(this.space);
            const p1 = this.space[longestIdx];
            const p2 = this.space[(longestIdx + 1) % this.space.length];

            monumentPos = GeomUtils.lerp(p1, p2);
            monumentDir = p2.subtract(p1);
        }

        if (hasRect) {
            // Rectangular monument (fountain, statue base)
            const width = 1 + Random.next();
            const height = 1 + Random.next();
            this.monument = PolygonUtils.rectangle(width, height);

            const angle = Math.atan2(monumentDir.y, monumentDir.x);
            PolygonUtils.rotate(this.monument, angle);
        } else {
            // Circular monument (well, column)
            const radius = 1 + Random.next();
            this.monument = PolygonUtils.regular(8, radius);
        }

        // Position monument
        const center = PolygonUtils.centroid(this.space);
        if (monumentPos) {
            // Between center and edge
            const pos = GeomUtils.lerp(center, monumentPos, 0.2 + Random.next() * 0.4);
            PolygonUtils.translateBy(this.monument, pos);
        } else {
            PolygonUtils.translateBy(this.monument, center);
        }
    }
}
