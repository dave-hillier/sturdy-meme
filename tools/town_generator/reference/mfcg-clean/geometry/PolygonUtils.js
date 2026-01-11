/**
 * PolygonUtils.js - Polygon utility functions
 *
 * Combines functionality from PolyCore, PolyBounds, PolyCreate, PolyCut, PolyTransform, PolyAccess
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';

export class PolygonUtils {
    // ========== PolyCore functions ==========

    /**
     * Calculate signed area of polygon
     * @param {Point[]} poly
     * @returns {number}
     */
    static area(poly) {
        let area = 0;
        const n = poly.length;
        for (let i = 0; i < n; i++) {
            const j = (i + 1) % n;
            area += poly[i].x * poly[j].y;
            area -= poly[j].x * poly[i].y;
        }
        return area / 2;
    }

    /**
     * Calculate perimeter of polygon
     * @param {Point[]} poly
     * @returns {number}
     */
    static perimeter(poly) {
        let perimeter = 0;
        const n = poly.length;
        for (let i = 0; i < n; i++) {
            perimeter += Point.distance(poly[i], poly[(i + 1) % n]);
        }
        return perimeter;
    }

    /**
     * Calculate centroid of polygon
     * @param {Point[]} poly
     * @returns {Point}
     */
    static centroid(poly) {
        let cx = 0, cy = 0;
        let area = 0;
        const n = poly.length;

        for (let i = 0; i < n; i++) {
            const j = (i + 1) % n;
            const cross = poly[i].x * poly[j].y - poly[j].x * poly[i].y;
            cx += (poly[i].x + poly[j].x) * cross;
            cy += (poly[i].y + poly[j].y) * cross;
            area += cross;
        }

        area /= 2;
        cx /= (6 * area);
        cy /= (6 * area);

        return new Point(cx, cy);
    }

    /**
     * Calculate center (average of vertices)
     * @param {Point[]} poly
     * @returns {Point}
     */
    static center(poly) {
        let x = 0, y = 0;
        for (const p of poly) {
            x += p.x;
            y += p.y;
        }
        return new Point(x / poly.length, y / poly.length);
    }

    /**
     * Calculate compactness (isoperimetric quotient)
     * @param {Point[]} poly
     * @returns {number} 0-1, where 1 is a perfect circle
     */
    static compactness(poly) {
        const area = Math.abs(PolygonUtils.area(poly));
        const perim = PolygonUtils.perimeter(poly);
        return (4 * Math.PI * area) / (perim * perim);
    }

    /**
     * Set polygon vertices to new values
     * @param {Point[]} poly - Polygon to modify
     * @param {Point[]} newPoly - New vertices
     */
    static set(poly, newPoly) {
        poly.length = 0;
        for (const p of newPoly) {
            poly.push(p);
        }
    }

    // ========== PolyBounds functions ==========

    /**
     * Get axis-aligned bounding box
     * @param {Point[]} poly
     * @returns {{x: number, y: number, width: number, height: number}}
     */
    static bounds(poly) {
        if (poly.length === 0) {
            return { x: 0, y: 0, width: 0, height: 0 };
        }

        let minX = poly[0].x, maxX = poly[0].x;
        let minY = poly[0].y, maxY = poly[0].y;

        for (const p of poly) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }

        return { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
    }

    /**
     * Get bounding rectangle as polygon
     * @param {Point[]} poly
     * @returns {Point[]}
     */
    static rect(poly) {
        const b = PolygonUtils.bounds(poly);
        return [
            new Point(b.x, b.y),
            new Point(b.x + b.width, b.y),
            new Point(b.x + b.width, b.y + b.height),
            new Point(b.x, b.y + b.height)
        ];
    }

    /**
     * Get oriented bounding box (minimum area bounding rectangle)
     * @param {Point[]} poly
     * @returns {Point[]} 4 corners of OBB
     */
    static obb(poly) {
        // Simplified: use AABB rotated to longest edge
        let longestIdx = 0;
        let longestLen = 0;
        const n = poly.length;

        for (let i = 0; i < n; i++) {
            const len = Point.distance(poly[i], poly[(i + 1) % n]);
            if (len > longestLen) {
                longestLen = len;
                longestIdx = i;
            }
        }

        const p1 = poly[longestIdx];
        const p2 = poly[(longestIdx + 1) % n];
        const dir = p2.subtract(p1).normalized();
        const perp = new Point(-dir.y, dir.x);

        // Project all points onto dir and perp
        let minD = Infinity, maxD = -Infinity;
        let minP = Infinity, maxP = -Infinity;

        for (const p of poly) {
            const v = p.subtract(p1);
            const d = v.dot(dir);
            const pp = v.dot(perp);
            if (d < minD) minD = d;
            if (d > maxD) maxD = d;
            if (pp < minP) minP = pp;
            if (pp > maxP) maxP = pp;
        }

        const origin = p1.add(dir.scale(minD)).add(perp.scale(minP));
        return [
            origin,
            origin.add(dir.scale(maxD - minD)),
            origin.add(dir.scale(maxD - minD)).add(perp.scale(maxP - minP)),
            origin.add(perp.scale(maxP - minP))
        ];
    }

    /**
     * Get largest inscribed rectangle approximation (LIRA)
     * @param {Point[]} poly
     * @returns {Point[]}
     */
    static lira(poly) {
        const obb = PolygonUtils.obb(poly);
        // Shrink slightly to ensure it's inside
        const center = PolygonUtils.center(obb);
        return obb.map(p => Point.lerp(p, center, 0.1));
    }

    /**
     * Check if point is inside polygon (ray casting)
     * @param {Point[]} poly
     * @param {Point} point
     * @returns {boolean}
     */
    static containsPoint(poly, point) {
        let inside = false;
        const n = poly.length;

        for (let i = 0, j = n - 1; i < n; j = i++) {
            const pi = poly[i], pj = poly[j];
            if ((pi.y > point.y) !== (pj.y > point.y) &&
                point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x) {
                inside = !inside;
            }
        }

        return inside;
    }

    /**
     * Check if vertex at index is convex
     * @param {Point[]} poly
     * @param {number} index
     * @returns {boolean}
     */
    static isConvexVertex(poly, index) {
        const n = poly.length;
        const prev = poly[(index + n - 1) % n];
        const curr = poly[index];
        const next = poly[(index + 1) % n];
        const cross = (curr.x - prev.x) * (next.y - curr.y) - (curr.y - prev.y) * (next.x - curr.x);
        return cross > 0;
    }

    // ========== PolyCreate functions ==========

    /**
     * Create regular polygon
     * @param {number} sides - Number of sides
     * @param {number} radius - Circumradius
     * @param {number} [startAngle=0] - Starting angle
     * @returns {Point[]}
     */
    static regular(sides, radius, startAngle = 0) {
        const poly = [];
        for (let i = 0; i < sides; i++) {
            const angle = startAngle + (2 * Math.PI * i) / sides;
            poly.push(Point.polar(radius, angle));
        }
        return poly;
    }

    /**
     * Create rectangle
     * @param {number} width
     * @param {number} height
     * @returns {Point[]}
     */
    static rectangle(width, height) {
        const hw = width / 2, hh = height / 2;
        return [
            new Point(-hw, -hh),
            new Point(hw, -hh),
            new Point(hw, hh),
            new Point(-hw, hh)
        ];
    }

    // ========== PolyTransform functions ==========

    /**
     * Translate polygon
     * @param {Point[]} poly
     * @param {number} dx
     * @param {number} dy
     */
    static translate(poly, dx, dy) {
        for (const p of poly) {
            p.x += dx;
            p.y += dy;
        }
        return poly;
    }

    /**
     * Translate polygon by point
     * @param {Point[]} poly
     * @param {Point} offset
     */
    static translateBy(poly, offset) {
        return PolygonUtils.translate(poly, offset.x, offset.y);
    }

    /**
     * Rotate polygon
     * @param {Point[]} poly
     * @param {number} angle - Angle in radians
     * @param {Point} [center] - Center of rotation
     */
    static rotate(poly, angle, center) {
        const cos = Math.cos(angle);
        const sin = Math.sin(angle);
        const cx = center ? center.x : 0;
        const cy = center ? center.y : 0;

        for (const p of poly) {
            const dx = p.x - cx;
            const dy = p.y - cy;
            p.x = cx + dx * cos - dy * sin;
            p.y = cy + dx * sin + dy * cos;
        }
        return poly;
    }

    /**
     * Rotate using sin/cos directly
     * @param {Point[]} poly
     * @param {number} sin
     * @param {number} cos
     */
    static rotateYX(poly, sin, cos) {
        for (const p of poly) {
            const x = p.x * cos - p.y * sin;
            const y = p.y * cos + p.x * sin;
            p.x = x;
            p.y = y;
        }
        return poly;
    }

    /**
     * Scale polygon
     * @param {Point[]} poly
     * @param {number} sx - X scale
     * @param {number} [sy=sx] - Y scale
     * @param {Point} [center] - Center of scaling
     */
    static scale(poly, sx, sy = sx, center) {
        const cx = center ? center.x : 0;
        const cy = center ? center.y : 0;

        for (const p of poly) {
            p.x = cx + (p.x - cx) * sx;
            p.y = cy + (p.y - cy) * sy;
        }
        return poly;
    }

    // ========== PolyAccess functions ==========

    /**
     * Find index of longest edge
     * @param {Point[]} poly
     * @returns {number}
     */
    static longestEdge(poly) {
        let maxLen = 0;
        let maxIdx = 0;
        const n = poly.length;

        for (let i = 0; i < n; i++) {
            const len = Point.distance(poly[i], poly[(i + 1) % n]);
            if (len > maxLen) {
                maxLen = len;
                maxIdx = i;
            }
        }
        return maxIdx;
    }

    /**
     * Find vertex closest to a point
     * @param {Point[]} poly
     * @param {Point} point
     * @returns {number} Index of closest vertex
     */
    static closestVertex(poly, point) {
        let minDist = Infinity;
        let minIdx = 0;

        for (let i = 0; i < poly.length; i++) {
            const dist = Point.distanceSquared(poly[i], point);
            if (dist < minDist) {
                minDist = dist;
                minIdx = i;
            }
        }
        return minIdx;
    }

    // ========== PolyCut functions ==========

    /**
     * Shrink polygon by uniform amount
     * @param {Point[]} poly
     * @param {number} amount
     * @returns {Point[]}
     */
    static shrink(poly, amount) {
        return PolygonUtils.inset(poly, Array(poly.length).fill(amount));
    }

    /**
     * Inset polygon with per-edge amounts
     * @param {Point[]} poly
     * @param {number[]} amounts - Inset amount for each edge
     * @returns {Point[]|null}
     */
    static inset(poly, amounts) {
        const n = poly.length;
        const result = [];

        for (let i = 0; i < n; i++) {
            const prev = (i + n - 1) % n;
            const next = (i + 1) % n;

            const v1 = poly[i].subtract(poly[prev]).normalized();
            const v2 = poly[next].subtract(poly[i]).normalized();

            const n1 = new Point(-v1.y, v1.x);
            const n2 = new Point(-v2.y, v2.x);

            const p1 = poly[prev].add(n1.scale(amounts[prev]));
            const p2 = poly[i].add(n1.scale(amounts[prev]));
            const p3 = poly[i].add(n2.scale(amounts[i]));
            const p4 = poly[next].add(n2.scale(amounts[i]));

            // Find intersection of the two offset edges
            const result1 = lineIntersection(p1, p2, p3, p4);
            if (result1) {
                result.push(result1);
            }
        }

        // Check if result is valid (not self-intersecting and positive area)
        if (result.length < 3 || PolygonUtils.area(result) <= 0) {
            return null;
        }

        return result;
    }

    /**
     * Cut polygon with a line
     * @param {Point[]} poly
     * @param {Point} linePoint - Point on line
     * @param {Point} lineDir - Direction of line
     * @returns {Point[][]} Array of resulting polygons
     */
    static cut(poly, linePoint, lineDir) {
        // Simplified implementation - returns original if no intersection
        // Full implementation would compute actual cut
        return [poly.slice()];
    }

    /**
     * Grid subdivision of a quadrilateral
     * @param {Point[]} quad - 4-vertex polygon
     * @param {number} cols - Number of columns
     * @param {number} rows - Number of rows
     * @param {number} [margin=0] - Gap between cells
     * @returns {Point[][]} Array of cell polygons
     */
    static grid(quad, cols, rows, margin = 0) {
        const cells = [];

        for (let row = 0; row < rows; row++) {
            for (let col = 0; col < cols; col++) {
                const t0 = col / cols;
                const t1 = (col + 1) / cols;
                const s0 = row / rows;
                const s1 = (row + 1) / rows;

                // Bilinear interpolation
                const p00 = bilinear(quad, t0, s0);
                const p10 = bilinear(quad, t1, s0);
                const p11 = bilinear(quad, t1, s1);
                const p01 = bilinear(quad, t0, s1);

                if (margin > 0) {
                    const center = PolygonUtils.center([p00, p10, p11, p01]);
                    cells.push([
                        Point.lerp(p00, center, margin),
                        Point.lerp(p10, center, margin),
                        Point.lerp(p11, center, margin),
                        Point.lerp(p01, center, margin)
                    ]);
                } else {
                    cells.push([p00, p10, p11, p01]);
                }
            }
        }

        return cells;
    }
}

// Helper functions

function lineIntersection(p1, p2, p3, p4) {
    const d1 = p2.subtract(p1);
    const d2 = p4.subtract(p3);
    const cross = d1.cross(d2);

    if (Math.abs(cross) < 0.0001) {
        return null; // Parallel lines
    }

    const d = p3.subtract(p1);
    const t = d.cross(d2) / cross;

    return new Point(p1.x + d1.x * t, p1.y + d1.y * t);
}

function bilinear(quad, s, t) {
    const top = Point.lerp(quad[0], quad[1], s);
    const bottom = Point.lerp(quad[3], quad[2], s);
    return Point.lerp(top, bottom, t);
}
