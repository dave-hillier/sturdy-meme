/**
 * Voronoi.js - Voronoi diagram generator
 *
 * Generates Voronoi diagrams from point sets.
 * Used for creating city patches.
 */

import { Point } from '../core/Point.js';

/**
 * Simple Voronoi diagram generator using Fortune's algorithm approximation
 */
export class Voronoi {
    /**
     * Create Voronoi generator
     * @param {Point[]} sites - Input points
     */
    constructor(sites) {
        this.sites = sites;
        this.cells = new Map();
    }

    /**
     * Compute Voronoi diagram
     * @returns {Map<Point, Point[]>} Map of site to cell polygon
     */
    getVoronoi() {
        if (this.sites.length === 0) return this.cells;

        // Use Bowyer-Watson algorithm for Delaunay triangulation
        // then compute Voronoi as dual

        const triangles = this.delaunay();
        this.computeVoronoiFromDelaunay(triangles);

        return this.cells;
    }

    /**
     * Compute Delaunay triangulation using Bowyer-Watson algorithm
     * @returns {Array<{a: Point, b: Point, c: Point}>}
     */
    delaunay() {
        // Create super-triangle containing all points
        const bounds = this.getBounds();
        const margin = Math.max(bounds.width, bounds.height) * 10;

        const superA = new Point(bounds.x - margin, bounds.y - margin);
        const superB = new Point(bounds.x + bounds.width + margin, bounds.y - margin);
        const superC = new Point(bounds.x + bounds.width / 2, bounds.y + bounds.height + margin);

        let triangles = [{ a: superA, b: superB, c: superC }];

        // Add each point one at a time
        for (const site of this.sites) {
            const badTriangles = [];

            // Find triangles whose circumcircle contains the point
            for (const tri of triangles) {
                if (this.inCircumcircle(tri, site)) {
                    badTriangles.push(tri);
                }
            }

            // Find boundary of polygonal hole
            const polygon = [];
            for (const tri of badTriangles) {
                const edges = [
                    { a: tri.a, b: tri.b },
                    { a: tri.b, b: tri.c },
                    { a: tri.c, b: tri.a }
                ];

                for (const edge of edges) {
                    // Check if edge is shared with another bad triangle
                    let shared = false;
                    for (const other of badTriangles) {
                        if (other === tri) continue;
                        if (this.hasEdge(other, edge.a, edge.b)) {
                            shared = true;
                            break;
                        }
                    }
                    if (!shared) {
                        polygon.push(edge);
                    }
                }
            }

            // Remove bad triangles
            triangles = triangles.filter(t => !badTriangles.includes(t));

            // Create new triangles from boundary edges to new point
            for (const edge of polygon) {
                triangles.push({ a: edge.a, b: edge.b, c: site });
            }
        }

        // Remove triangles containing super-triangle vertices
        triangles = triangles.filter(tri =>
            tri.a !== superA && tri.a !== superB && tri.a !== superC &&
            tri.b !== superA && tri.b !== superB && tri.b !== superC &&
            tri.c !== superA && tri.c !== superB && tri.c !== superC
        );

        return triangles;
    }

    /**
     * Compute Voronoi cells from Delaunay triangulation
     * @param {Array} triangles
     */
    computeVoronoiFromDelaunay(triangles) {
        // Map sites to their adjacent triangles
        const siteTriangles = new Map();
        for (const site of this.sites) {
            siteTriangles.set(site, []);
        }

        for (const tri of triangles) {
            for (const site of this.sites) {
                if (tri.a === site || tri.b === site || tri.c === site) {
                    siteTriangles.get(site).push(tri);
                }
            }
        }

        // For each site, create cell from circumcenters of adjacent triangles
        for (const site of this.sites) {
            const adjTriangles = siteTriangles.get(site);
            if (adjTriangles.length === 0) continue;

            // Get circumcenters
            const centers = adjTriangles.map(tri => this.circumcenter(tri));

            // Sort centers by angle around site
            centers.sort((a, b) => {
                const angleA = Math.atan2(a.y - site.y, a.x - site.x);
                const angleB = Math.atan2(b.y - site.y, b.x - site.x);
                return angleA - angleB;
            });

            this.cells.set(site, centers);
        }
    }

    /**
     * Get bounding box of sites
     */
    getBounds() {
        let minX = Infinity, maxX = -Infinity;
        let minY = Infinity, maxY = -Infinity;

        for (const p of this.sites) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }

        return {
            x: minX,
            y: minY,
            width: maxX - minX,
            height: maxY - minY
        };
    }

    /**
     * Check if point is inside circumcircle of triangle
     */
    inCircumcircle(tri, point) {
        const ax = tri.a.x - point.x;
        const ay = tri.a.y - point.y;
        const bx = tri.b.x - point.x;
        const by = tri.b.y - point.y;
        const cx = tri.c.x - point.x;
        const cy = tri.c.y - point.y;

        const det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                   (bx * bx + by * by) * (ax * cy - cx * ay) +
                   (cx * cx + cy * cy) * (ax * by - bx * ay);

        return det > 0;
    }

    /**
     * Check if triangle has edge between two points
     */
    hasEdge(tri, a, b) {
        return (tri.a === a && tri.b === b) || (tri.b === a && tri.a === b) ||
               (tri.b === a && tri.c === b) || (tri.c === a && tri.b === b) ||
               (tri.c === a && tri.a === b) || (tri.a === a && tri.c === b);
    }

    /**
     * Compute circumcenter of triangle
     */
    circumcenter(tri) {
        const ax = tri.a.x, ay = tri.a.y;
        const bx = tri.b.x, by = tri.b.y;
        const cx = tri.c.x, cy = tri.c.y;

        const d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (Math.abs(d) < 0.0001) {
            return new Point((ax + bx + cx) / 3, (ay + by + cy) / 3);
        }

        const ux = ((ax * ax + ay * ay) * (by - cy) +
                   (bx * bx + by * by) * (cy - ay) +
                   (cx * cx + cy * cy) * (ay - by)) / d;
        const uy = ((ax * ax + ay * ay) * (cx - bx) +
                   (bx * bx + by * by) * (ax - cx) +
                   (cx * cx + cy * cy) * (bx - ax)) / d;

        return new Point(ux, uy);
    }
}

/**
 * Triangulation utilities
 */
export class Triangulation {
    /**
     * Ear-cut triangulation of simple polygon
     * @param {Point[]} polygon
     * @returns {Array<[number, number, number]>} Triangle indices
     */
    static earcut(polygon) {
        const n = polygon.length;
        if (n < 3) return [];

        // Build list of vertex indices
        const indices = [];
        for (let i = 0; i < n; i++) {
            indices.push(i);
        }

        const triangles = [];

        while (indices.length > 3) {
            let earFound = false;

            for (let i = 0; i < indices.length; i++) {
                const prev = indices[(i + indices.length - 1) % indices.length];
                const curr = indices[i];
                const next = indices[(i + 1) % indices.length];

                const a = polygon[prev];
                const b = polygon[curr];
                const c = polygon[next];

                // Check if this is a convex vertex
                if (!isConvex(a, b, c)) continue;

                // Check if no other vertices are inside this triangle
                let isEar = true;
                for (const idx of indices) {
                    if (idx === prev || idx === curr || idx === next) continue;
                    if (pointInTriangle(polygon[idx], a, b, c)) {
                        isEar = false;
                        break;
                    }
                }

                if (isEar) {
                    triangles.push([prev, curr, next]);
                    indices.splice(i, 1);
                    earFound = true;
                    break;
                }
            }

            if (!earFound) {
                // Fallback: just take any triangle
                if (indices.length >= 3) {
                    triangles.push([indices[0], indices[1], indices[2]]);
                    indices.splice(1, 1);
                } else {
                    break;
                }
            }
        }

        if (indices.length === 3) {
            triangles.push([indices[0], indices[1], indices[2]]);
        }

        return triangles;
    }
}

// Helper functions

function isConvex(a, b, c) {
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) > 0;
}

function pointInTriangle(p, a, b, c) {
    const d1 = sign(p, a, b);
    const d2 = sign(p, b, c);
    const d3 = sign(p, c, a);

    const hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

function sign(p1, p2, p3) {
    return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}
