/**
 * Cutter.js - Polygon cutting and subdivision utilities
 *
 * Provides grid subdivision of quadrilaterals for building layouts.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';

/**
 * Polygon cutting utilities
 */
export class Cutter {
    /**
     * Subdivide a quadrilateral into a grid of cells
     *
     * @param {Point[]} quad - 4-vertex quadrilateral [topLeft, topRight, bottomRight, bottomLeft]
     * @param {number} cols - Number of columns
     * @param {number} rows - Number of rows
     * @param {number} [chaos=0] - Amount of randomness in grid lines (0-1)
     * @returns {Point[][]} Array of cell quadrilaterals
     * @throws {Error} If input is not a quadrilateral
     */
    static grid(quad, cols, rows, chaos = 0) {
        if (quad.length !== 4) {
            throw new Error('Not a quadrangle!');
        }

        // Create normalized grid positions [0, 1]
        const xPositions = [];
        for (let i = 0; i <= cols; i++) {
            xPositions.push(i / cols);
        }

        const yPositions = [];
        for (let i = 0; i <= rows; i++) {
            yPositions.push(i / rows);
        }

        // Add chaos to interior positions
        if (chaos > 0) {
            // Randomize x positions (skip first and last)
            for (let i = 1; i < cols; i++) {
                const noise = (Random.triangular() - 0.5) / (cols - 1) * chaos;
                xPositions[i] += noise;
            }

            // Randomize y positions (skip first and last)
            for (let i = 1; i < rows; i++) {
                const noise = (Random.triangular() - 0.5) / (rows - 1) * chaos;
                yPositions[i] += noise;
            }
        }

        // Quad corners: [topLeft, topRight, bottomRight, bottomLeft]
        const p0 = quad[0]; // Top-left
        const p1 = quad[1]; // Top-right
        const p2 = quad[2]; // Bottom-right
        const p3 = quad[3]; // Bottom-left

        // Generate grid of vertices using bilinear interpolation
        const vertices = [];
        for (let row = 0; row <= rows; row++) {
            const t = yPositions[row];
            // Interpolate left and right edges
            const left = Point.lerp(p0, p3, t);
            const right = Point.lerp(p1, p2, t);

            const rowVertices = [];
            for (let col = 0; col <= cols; col++) {
                const s = xPositions[col];
                rowVertices.push(Point.lerp(left, right, s));
            }
            vertices.push(rowVertices);
        }

        // Generate cell quadrilaterals
        const cells = [];
        for (let row = 0; row < rows; row++) {
            for (let col = 0; col < cols; col++) {
                cells.push([
                    vertices[row][col],         // Top-left
                    vertices[row][col + 1],     // Top-right
                    vertices[row + 1][col + 1], // Bottom-right
                    vertices[row + 1][col]      // Bottom-left
                ]);
            }
        }

        return cells;
    }

    /**
     * Subdivide a quadrilateral into a grid with margin
     *
     * @param {Point[]} quad - 4-vertex quadrilateral
     * @param {number} cols - Number of columns
     * @param {number} rows - Number of rows
     * @param {number} margin - Gap between cells (0-1 of cell size)
     * @param {number} [chaos=0] - Amount of randomness in grid lines
     * @returns {Point[][]} Array of cell quadrilaterals with margins
     */
    static gridWithMargin(quad, cols, rows, margin, chaos = 0) {
        const cells = Cutter.grid(quad, cols, rows, chaos);

        if (margin <= 0) {
            return cells;
        }

        // Shrink each cell by margin
        return cells.map(cell => {
            const center = new Point(
                (cell[0].x + cell[1].x + cell[2].x + cell[3].x) / 4,
                (cell[0].y + cell[1].y + cell[2].y + cell[3].y) / 4
            );

            return cell.map(p => Point.lerp(p, center, margin));
        });
    }

    /**
     * Cut a polygon with a line
     *
     * @param {Point[]} poly - Polygon to cut
     * @param {Point} linePoint - Point on the cutting line
     * @param {Point} lineDir - Direction of the cutting line
     * @returns {Point[][]} Array of resulting polygons (1 if no cut, 2 if cut)
     */
    static cutWithLine(poly, linePoint, lineDir) {
        const n = poly.length;
        const intersections = [];

        // Find intersection points
        for (let i = 0; i < n; i++) {
            const p1 = poly[i];
            const p2 = poly[(i + 1) % n];
            const edgeDir = p2.subtract(p1);

            const cross = lineDir.cross(edgeDir);
            if (Math.abs(cross) < 0.0001) {
                continue; // Parallel
            }

            const d = p1.subtract(linePoint);
            const t = lineDir.cross(d) / cross;

            if (t > 0.0001 && t < 0.9999) {
                const intersection = Point.lerp(p1, p2, t);
                intersections.push({
                    point: intersection,
                    edgeIndex: i,
                    t: t
                });
            }
        }

        // Need exactly 2 intersections for a clean cut
        if (intersections.length !== 2) {
            return [poly.slice()];
        }

        // Sort by edge index
        intersections.sort((a, b) => {
            if (a.edgeIndex !== b.edgeIndex) {
                return a.edgeIndex - b.edgeIndex;
            }
            return a.t - b.t;
        });

        const [int1, int2] = intersections;

        // Build two resulting polygons
        const poly1 = [];
        const poly2 = [];

        for (let i = 0; i <= int1.edgeIndex; i++) {
            poly1.push(poly[i]);
        }
        poly1.push(int1.point);
        poly1.push(int2.point);
        for (let i = int2.edgeIndex + 1; i < n; i++) {
            poly1.push(poly[i]);
        }

        poly2.push(int1.point);
        for (let i = int1.edgeIndex + 1; i <= int2.edgeIndex; i++) {
            poly2.push(poly[i]);
        }
        poly2.push(int2.point);

        return [poly1, poly2];
    }
}
