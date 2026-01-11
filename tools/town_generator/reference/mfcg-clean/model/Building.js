/**
 * Building.js - Building footprint generation
 *
 * Generates building footprints within city blocks.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

export class Building {
    /**
     * Create a building footprint
     * @param {Point[]} lot - The lot polygon (usually a rectangle)
     * @param {number} area - Target building area
     * @param {boolean} [front=false] - Building faces front edge
     * @param {boolean} [symmetric=false] - Building is symmetric
     * @param {number} [margin=0] - Margin from lot edges
     * @returns {Point[]|null}
     */
    static create(lot, area, front = false, symmetric = false, margin = 0) {
        const cellSize = Math.sqrt(area);
        const width = Point.distance(lot[0], lot[1]);
        const height = Point.distance(lot[1], lot[2]);

        const cols = Math.ceil(Math.min(width, Point.distance(lot[2], lot[3])) / cellSize);
        const rows = Math.ceil(Math.min(height, Point.distance(lot[3], lot[0])) / cellSize);

        if (cols <= 1 || rows <= 1) return null;

        // Generate building plan (grid of filled/empty cells)
        let plan;
        if (symmetric) {
            plan = Building.getPlanSymmetric(cols, rows);
        } else if (front) {
            plan = Building.getPlanFront(cols, rows);
        } else {
            plan = Building.getPlan(cols, rows);
        }

        // Count filled cells
        const filledCount = plan.filter(cell => cell).length;
        if (filledCount >= cols * rows) return null;

        // Subdivide lot into grid cells
        const grid = PolygonUtils.grid(lot, cols, rows, margin);

        // Collect filled cells
        const filledCells = [];
        for (let i = 0; i < grid.length; i++) {
            if (plan[i]) {
                filledCells.push(grid[i]);
            }
        }

        // Merge into single polygon
        return Building.circumference(filledCells);
    }

    /**
     * Generate random building plan
     * @param {number} cols
     * @param {number} rows
     * @param {number} [continueProbability=0.5]
     * @returns {boolean[]}
     */
    static getPlan(cols, rows, continueProbability = 0.5) {
        const total = cols * rows;
        const plan = new Array(total).fill(false);

        // Start with random cell
        let x = Random.int(cols);
        let y = Random.int(rows);
        plan[x + y * cols] = true;

        let minX = x, maxX = x;
        let minY = y, maxY = y;

        // Grow building
        while (true) {
            x = Random.int(cols);
            y = Random.int(rows);
            const idx = x + y * cols;

            // Check if adjacent to existing cell
            const adjacent = (
                (x > 0 && plan[idx - 1]) ||
                (y > 0 && plan[idx - cols]) ||
                (x < cols - 1 && plan[idx + 1]) ||
                (y < rows - 1 && plan[idx + cols])
            );

            if (!plan[idx] && adjacent) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
                plan[idx] = true;
            }

            // Check if we should continue
            const touchesBorder = minX === 0 || maxX === cols - 1 ||
                                  minY === 0 || maxY === rows - 1;

            if (touchesBorder) {
                if (!Random.bool(continueProbability)) break;
            }
        }

        return plan;
    }

    /**
     * Generate front-facing building plan
     * @param {number} cols
     * @param {number} rows
     * @returns {boolean[]}
     */
    static getPlanFront(cols, rows) {
        const total = cols * rows;
        const plan = new Array(total).fill(false);

        // Fill front row
        for (let x = 0; x < cols; x++) {
            plan[x] = true;
        }

        let maxDepth = 0;

        // Grow backward
        while (true) {
            const x = Random.int(cols);
            const y = 1 + Random.int(rows - 1);
            const idx = x + y * cols;

            const adjacent = (
                (x > 0 && plan[idx - 1]) ||
                (y > 0 && plan[idx - cols]) ||
                (x < cols - 1 && plan[idx + 1]) ||
                (y < rows - 1 && plan[idx + cols])
            );

            if (!plan[idx] && adjacent) {
                if (y > maxDepth) maxDepth = y;
                plan[idx] = true;
            }

            if (maxDepth >= rows - 1) {
                if (!Random.bool(0.5)) break;
            }
        }

        return plan;
    }

    /**
     * Generate symmetric building plan
     * @param {number} cols
     * @param {number} rows
     * @returns {boolean[]}
     */
    static getPlanSymmetric(cols, rows) {
        const plan = Building.getPlan(cols, rows, 0);

        // Mirror horizontally
        for (let y = 0; y < rows; y++) {
            for (let x = 0; x < cols; x++) {
                const idx = y * cols + x;
                const mirrorIdx = (y + 1) * cols - 1 - x;
                plan[idx] = plan[mirrorIdx] = plan[idx] || plan[mirrorIdx];
            }
        }

        return plan;
    }

    /**
     * Merge multiple polygons into their outer circumference
     * @param {Point[][]} polygons
     * @returns {Point[]}
     */
    static circumference(polygons) {
        if (polygons.length === 0) return [];
        if (polygons.length === 1) return polygons[0];

        // Track edges with their directions
        const edges = [];
        const reverseEdges = [];

        for (const poly of polygons) {
            const n = poly.length;
            for (let i = 0; i < n; i++) {
                const p1 = poly[i];
                const p2 = poly[(i + 1) % n];

                edges.push({ from: p1, to: p2 });
                reverseEdges.push({ from: p2, to: p1 });
            }
        }

        // Find boundary edges (not cancelled by reverse)
        const boundary = [];
        for (let i = 0; i < edges.length; i++) {
            const edge = edges[i];
            let cancelled = false;

            for (let j = 0; j < reverseEdges.length; j++) {
                if (i === j) continue;
                const rev = edges[j];
                if (rev.from === edge.to && rev.to === edge.from) {
                    cancelled = true;
                    break;
                }
            }

            if (!cancelled) {
                boundary.push(edge);
            }
        }

        if (boundary.length === 0) return polygons[0];

        // Chain boundary edges into polygon
        const result = [boundary[0].from];
        let current = boundary[0].to;

        while (current !== result[0] && result.length < boundary.length * 2) {
            result.push(current);

            for (const edge of boundary) {
                if (edge.from === current) {
                    current = edge.to;
                    break;
                }
            }
        }

        // Remove collinear points
        return Building.removeCollinear(result);
    }

    /**
     * Remove collinear points from polygon
     * @param {Point[]} poly
     * @returns {Point[]}
     */
    static removeCollinear(poly) {
        const result = [];
        const n = poly.length;

        for (let i = 0; i < n; i++) {
            const prev = poly[(i + n - 1) % n];
            const curr = poly[i];
            const next = poly[(i + 1) % n];

            const d1 = curr.subtract(prev).normalized();
            const d2 = next.subtract(curr).normalized();

            const dot = d1.dot(d2);
            if (dot < 0.999) {
                result.push(curr);
            }
        }

        return result;
    }
}
