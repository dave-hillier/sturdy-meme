/**
 * PoissonPattern.js - Poisson disk sampling
 *
 * Generates evenly-distributed random points using Bridson's algorithm.
 * Used for placing trees and other natural features.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';

/**
 * Poisson disk sampling pattern generator
 */
export class PoissonPattern {
    /**
     * Create a Poisson pattern
     * @param {number} width - Pattern width
     * @param {number} height - Pattern height
     * @param {number} dist - Minimum distance between points
     * @param {number} [unevenness=0] - Amount of randomness to add (0-1)
     */
    constructor(width, height, dist, unevenness = 0) {
        /** @type {number} */
        this.width = width;

        /** @type {number} */
        this.height = height;

        /** @type {number} */
        this.dist = dist;

        /** @type {number} */
        this.dist2 = dist * dist;

        /** @type {number} */
        this.cellSize = dist / Math.sqrt(2);

        /** @type {number} */
        this.gridWidth = Math.ceil(width / this.cellSize);

        /** @type {number} */
        this.gridHeight = Math.ceil(height / this.cellSize);

        /** @type {(Point|null)[]} */
        this.grid = new Array(this.gridWidth * this.gridHeight).fill(null);

        /** @type {Point[]} */
        this.points = [];

        /** @type {Point[]} */
        this.queue = [];

        // Generate initial point
        this.emit(new Point(
            width * Random.next(),
            height * Random.next()
        ));

        // Generate all points
        while (this.step()) {}

        // Add unevenness if requested
        if (unevenness > 0) {
            this.uneven(unevenness);
        }
    }

    /**
     * Add a point to the pattern
     * @param {Point} point
     */
    emit(point) {
        this.points.push(point);
        this.queue.push(point);
        const gridIdx = Math.floor(point.y / this.cellSize) * this.gridWidth +
                        Math.floor(point.x / this.cellSize);
        this.grid[gridIdx] = point;
    }

    /**
     * Try to add more points around existing ones
     * @returns {boolean} True if there are more points to process
     */
    step() {
        if (this.queue.length === 0) return false;

        // Pick a random point from the queue
        const idx = Math.floor(Random.next() * this.queue.length);
        const point = this.queue[idx];
        let found = false;

        // Try to place new points around it
        for (let i = 0; i < 50; i++) {
            const r = this.dist * (1 + 0.1 * Random.next());
            const angle = 2 * Math.PI * Random.next();
            const candidate = new Point(
                point.x + r * Math.cos(angle),
                point.y + r * Math.sin(angle)
            );

            // Wrap around (toroidal)
            this.warp(candidate);

            if (this.validate(candidate)) {
                found = true;
                this.emit(candidate);
            }
        }

        if (!found) {
            // Remove point from queue if no new points were placed
            this.queue.splice(idx, 1);
        }

        return this.queue.length > 0;
    }

    /**
     * Wrap point coordinates to stay within bounds (toroidal)
     * @param {Point} point
     */
    warp(point) {
        if (point.x < 0) {
            point.x += this.width;
        } else if (point.x >= this.width) {
            point.x -= this.width;
        }

        if (point.y < 0) {
            point.y += this.height;
        } else if (point.y >= this.height) {
            point.y -= this.height;
        }
    }

    /**
     * Check if a point is valid (far enough from other points)
     * @param {Point} point
     * @returns {boolean}
     */
    validate(point) {
        const x = point.x;
        const y = point.y;
        const cellX = Math.floor(x / this.cellSize);
        const cellY = Math.floor(y / this.cellSize);

        // Check neighboring cells
        for (let dy = -2; dy <= 2; dy++) {
            const gridY = ((cellY + dy) + this.gridHeight) % this.gridHeight;
            for (let dx = -2; dx <= 2; dx++) {
                const gridX = ((cellX + dx) + this.gridWidth) % this.gridWidth;
                const neighbor = this.grid[gridY * this.gridWidth + gridX];

                if (neighbor) {
                    // Check distance considering wrapping
                    let distX = Math.abs(neighbor.x - x);
                    let distY = Math.abs(neighbor.y - y);

                    // Handle wrapping
                    if (distX > this.width / 2) distX = this.width - distX;
                    if (distY > this.height / 2) distY = this.height - distY;

                    if (distX * distX + distY * distY < this.dist2) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    /**
     * Add random displacement to make pattern less regular
     * @param {number} amount - Displacement amount (0-1)
     */
    uneven(amount) {
        const maxOffset = this.dist * amount * 0.5;
        for (const point of this.points) {
            point.x += (Random.next() * 2 - 1) * maxOffset;
            point.y += (Random.next() * 2 - 1) * maxOffset;
            this.warp(point);
        }
    }

    /**
     * Fill a polygon with points from this pattern
     * @param {import('./FillablePoly.js').FillablePoly} shape
     * @returns {Point[]}
     */
    fill(shape) {
        const result = [];
        const bounds = shape.bounds;

        for (const point of this.points) {
            // Offset point to shape bounds
            const x = bounds.x + (point.x % this.width);
            const y = bounds.y + (point.y % this.height);
            const translated = new Point(x, y);

            if (shape.containsPoint(translated)) {
                result.push(translated);
            }
        }

        return result;
    }
}

/**
 * Simple fillable polygon wrapper
 */
export class FillablePoly {
    /**
     * Create a fillable polygon
     * @param {Point[]} polygon
     */
    constructor(polygon) {
        /** @type {Point[]} */
        this.polygon = polygon;

        /** @type {{x: number, y: number, width: number, height: number}} */
        this.bounds = this.computeBounds();
    }

    /**
     * Compute axis-aligned bounding box
     * @returns {{x: number, y: number, width: number, height: number}}
     */
    computeBounds() {
        if (this.polygon.length === 0) {
            return { x: 0, y: 0, width: 0, height: 0 };
        }

        let minX = this.polygon[0].x, maxX = this.polygon[0].x;
        let minY = this.polygon[0].y, maxY = this.polygon[0].y;

        for (const p of this.polygon) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }

        return { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
    }

    /**
     * Check if point is inside polygon
     * @param {Point} point
     * @returns {boolean}
     */
    containsPoint(point) {
        let inside = false;
        const n = this.polygon.length;

        for (let i = 0, j = n - 1; i < n; j = i++) {
            const pi = this.polygon[i];
            const pj = this.polygon[j];

            if ((pi.y > point.y) !== (pj.y > point.y) &&
                point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x) {
                inside = !inside;
            }
        }

        return inside;
    }
}
