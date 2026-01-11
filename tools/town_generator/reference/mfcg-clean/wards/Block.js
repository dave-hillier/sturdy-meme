/**
 * Block.js - City block with building lots
 *
 * Represents a single city block containing multiple building lots.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { Building } from '../model/Building.js';

/**
 * City block with building lots
 */
export class Block {
    /**
     * Create a block
     * @param {import('./WardGroup.js').WardGroup} group - Parent ward group
     * @param {Point[]} shape - Block boundary
     * @param {boolean} [small=false] - Is this a small/special block
     */
    constructor(group, shape, small = false) {
        /** @type {import('./WardGroup.js').WardGroup} */
        this.group = group;

        /** @type {Point[]} */
        this.shape = shape;

        /** @type {Point[][]} */
        this.lots = [];

        /** @type {Point[][]} */
        this.buildings = [];

        /** @type {Point[][]} */
        this.rects = [];

        /** @type {Point[]|null} */
        this.trees = null;

        // Generate lots
        this.subdivideLots(small);
    }

    /**
     * Subdivide block into building lots
     * @param {boolean} small
     */
    subdivideLots(small) {
        if (!this.shape || this.shape.length < 3) return;

        const area = Math.abs(PolygonUtils.area(this.shape));
        const config = this.group.district?.alleys;

        const lotSize = config?.minSq || 100;
        const minFront = config?.minFront || 3;

        if (area < lotSize || small) {
            // Single lot block
            this.lots.push(this.shape);
            return;
        }

        // Find longest edge for frontage
        const frontIdx = PolygonUtils.longestEdge(this.shape);
        const frontP1 = this.shape[frontIdx];
        const frontP2 = this.shape[(frontIdx + 1) % this.shape.length];
        const frontLen = Point.distance(frontP1, frontP2);

        if (frontLen < minFront * 2) {
            // Too small to subdivide
            this.lots.push(this.shape);
            return;
        }

        // Calculate number of lots along frontage
        const numLots = Math.max(2, Math.floor(frontLen / minFront));

        // Create lots by subdividing along front edge
        for (let i = 0; i < numLots; i++) {
            const t1 = i / numLots;
            const t2 = (i + 1) / numLots;

            const p1 = GeomUtils.lerp(frontP1, frontP2, t1);
            const p2 = GeomUtils.lerp(frontP1, frontP2, t2);

            // Find corresponding back edge
            const backIdx = (frontIdx + 2) % this.shape.length;
            const backP1 = this.shape[backIdx];
            const backP2 = this.shape[(backIdx + 1) % this.shape.length];

            const p3 = GeomUtils.lerp(backP2, backP1, t2);
            const p4 = GeomUtils.lerp(backP2, backP1, t1);

            this.lots.push([p1, p2, p3, p4]);
        }
    }

    /**
     * Generate building geometries for all lots
     */
    generateBuildings() {
        this.buildings = [];
        this.rects = [];

        for (const lot of this.lots) {
            const rect = PolygonUtils.lira(lot);
            this.rects.push(rect);

            const area = Math.abs(PolygonUtils.area(lot));
            const building = Building.create(rect, area / 4);

            if (building) {
                this.buildings.push(building);
            } else {
                this.buildings.push(rect);
            }
        }
    }

    /**
     * Spawn trees in empty spaces
     * @returns {Point[]}
     */
    spawnTrees() {
        if (this.trees !== null) return this.trees;

        this.trees = [];

        // Add trees in gaps between buildings
        const bounds = PolygonUtils.bounds(this.shape);
        const spacing = 3;

        for (let x = bounds.x; x < bounds.x + bounds.width; x += spacing) {
            for (let y = bounds.y; y < bounds.y + bounds.height; y += spacing) {
                const point = new Point(
                    x + Random.gaussian() * spacing * 0.3,
                    y + Random.gaussian() * spacing * 0.3
                );

                // Check if inside block but outside all lots
                if (!PolygonUtils.containsPoint(this.shape, point)) continue;

                let inLot = false;
                for (const lot of this.lots) {
                    if (PolygonUtils.containsPoint(lot, point)) {
                        inLot = true;
                        break;
                    }
                }

                if (!inLot && Random.bool(0.3)) {
                    this.trees.push(point);
                }
            }
        }

        return this.trees;
    }

    /**
     * Check if point is inside this block
     * @param {Point} point
     * @returns {boolean}
     */
    containsPoint(point) {
        return PolygonUtils.containsPoint(this.shape, point);
    }

    /**
     * Get centroid of block
     * @returns {Point}
     */
    get center() {
        return PolygonUtils.centroid(this.shape);
    }

    /**
     * Get area of block
     * @returns {number}
     */
    get area() {
        return Math.abs(PolygonUtils.area(this.shape));
    }
}
