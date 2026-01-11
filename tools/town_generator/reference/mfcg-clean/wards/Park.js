/**
 * Park.js - Park/green space ward
 *
 * Represents gardens and green spaces within the city.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';
import { Chaikin } from '../geometry/Chaikin.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

/**
 * Park ward with gardens and trees
 */
export class Park extends Ward {
    /**
     * Create a park ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {Point[]} */
        this.green = [];

        /** @type {Point[]|null} */
        this.trees = null;
    }

    /**
     * Create park geometry
     */
    createGeometry() {
        const available = this.getAvailable();
        if (!available) {
            this.green = [];
            return;
        }

        // Create smooth organic boundary
        const n = available.length;
        const expanded = [];

        for (let i = 0; i < n; i++) {
            const curr = available[i];
            const next = available[(i + 1) % n];
            expanded.push(curr);
            expanded.push(GeomUtils.lerp(curr, next));
        }

        this.green = Chaikin.smoothClosed(expanded, 3);
        this.trees = null;
    }

    /**
     * Spawn trees in the park
     * @returns {Point[]}
     */
    spawnTrees() {
        if (this.trees !== null) return this.trees;

        const available = this.getAvailable();
        if (!available) {
            this.trees = [];
            return this.trees;
        }

        const density = this.patch.district?.greenery || 0.3;
        this.trees = Park.fillArea(available, density);

        return this.trees;
    }

    /**
     * Fill an area with trees using Poisson disk sampling
     * @param {Point[]} area - Polygon to fill
     * @param {number} density - Tree density (0-1)
     * @returns {Point[]}
     */
    static fillArea(area, density) {
        const trees = [];
        const bounds = PolygonUtils.bounds(area);

        // Minimum distance between trees
        const minDist = 2 / Math.sqrt(density);

        // Grid for fast neighbor lookup
        const cellSize = minDist / Math.sqrt(2);
        const gridWidth = Math.ceil(bounds.width / cellSize);
        const gridHeight = Math.ceil(bounds.height / cellSize);
        const grid = new Array(gridWidth * gridHeight).fill(null);

        const getGridIdx = (x, y) => {
            const gx = Math.floor((x - bounds.x) / cellSize);
            const gy = Math.floor((y - bounds.y) / cellSize);
            if (gx < 0 || gx >= gridWidth || gy < 0 || gy >= gridHeight) return -1;
            return gx + gy * gridWidth;
        };

        // Start with random point inside area
        const active = [];

        for (let attempt = 0; attempt < 30; attempt++) {
            const x = bounds.x + Random.next() * bounds.width;
            const y = bounds.y + Random.next() * bounds.height;
            const p = new Point(x, y);

            if (PolygonUtils.containsPoint(area, p)) {
                trees.push(p);
                active.push(p);
                const idx = getGridIdx(x, y);
                if (idx >= 0) grid[idx] = p;
                break;
            }
        }

        // Poisson disk sampling
        while (active.length > 0 && trees.length < 1000) {
            const idx = Random.int(active.length);
            const point = active[idx];
            let found = false;

            for (let attempt = 0; attempt < 30; attempt++) {
                const angle = Random.next() * 2 * Math.PI;
                const dist = minDist * (1 + Random.next());
                const newPoint = point.add(Point.polar(dist, angle));

                // Check bounds and area
                if (!PolygonUtils.containsPoint(area, newPoint)) continue;

                // Check distance to existing trees
                let tooClose = false;
                const gx = Math.floor((newPoint.x - bounds.x) / cellSize);
                const gy = Math.floor((newPoint.y - bounds.y) / cellSize);

                for (let dx = -2; dx <= 2 && !tooClose; dx++) {
                    for (let dy = -2; dy <= 2 && !tooClose; dy++) {
                        const nx = gx + dx;
                        const ny = gy + dy;
                        if (nx < 0 || nx >= gridWidth || ny < 0 || ny >= gridHeight) continue;

                        const neighbor = grid[nx + ny * gridWidth];
                        if (neighbor && Point.distance(newPoint, neighbor) < minDist) {
                            tooClose = true;
                        }
                    }
                }

                if (!tooClose) {
                    trees.push(newPoint);
                    active.push(newPoint);
                    const gridIdx = getGridIdx(newPoint.x, newPoint.y);
                    if (gridIdx >= 0) grid[gridIdx] = newPoint;
                    found = true;
                    break;
                }
            }

            if (!found) {
                active.splice(idx, 1);
            }
        }

        return trees;
    }
}
