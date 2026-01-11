/**
 * WardGroup.js - Group of connected alley wards
 *
 * Manages a connected group of Alleys wards, handling
 * block generation and geometry for the group as a whole.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { DCEL, EdgeChain } from '../geometry/DCEL.js';
import { Circle } from '../geometry/Circle.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { Triangulation } from '../geometry/Voronoi.js';
import { Ward } from './Ward.js';
import { Block } from './Block.js';
import { CurtainWall } from '../model/CurtainWall.js';

/**
 * Group of connected alley wards
 */
export class WardGroup {
    /**
     * Create a ward group
     * @param {import('../geometry/DCEL.js').Face[]} faces - DCEL faces in this group
     */
    constructor(faces) {
        /** @type {import('../geometry/DCEL.js').Face[]} */
        this.faces = faces;

        // Link alleys to this group
        for (const face of faces) {
            const ward = face.data?.ward;
            if (ward && ward.group !== undefined) {
                ward.group = this;
            }
        }

        /** @type {import('../geometry/DCEL.js').Face} */
        this.core = faces[0];

        /** @type {import('../model/City.js').City} */
        this.model = this.core.data?.ward?.model;

        /** @type {import('../model/District.js').District} */
        this.district = this.core.data?.district;

        /** @type {Point[]} */
        this.shape = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.border = [];

        /** @type {import('../geometry/DCEL.js').Vertex[]} */
        this.inner = [];

        /** @type {Map<Point, number>} */
        this.blockM = new Map();

        /** @type {boolean} */
        this.urban = false;

        /** @type {Block[]} */
        this.blocks = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[][]} */
        this.alleys = [];

        /** @type {Block|null} */
        this.church = null;

        /** @type {Array<[Point, Point, Point]>|null} */
        this.tris = null;

        // Compute border
        if (faces.length === 1) {
            this.shape = this.core.data.shape;
            this.border = [];
            for (const edge of this.core.edges()) {
                this.border.push(edge);
            }
        } else {
            this.border = faces.length < this.district?.faces?.length
                ? DCEL.circumference(null, faces)
                : this.district?.border || [];
            this.shape = EdgeChain.toPoly(this.border);
        }

        // Classify vertices
        for (const edge of this.border) {
            const vertex = edge.origin;

            if (!edge.face.data.withinWalls || this.isInnerVertex(vertex)) {
                this.inner.push(vertex);
                this.blockM.set(vertex.point, 1);
            } else {
                this.blockM.set(vertex.point, 9);
            }
        }

        this.urban = this.inner.length === this.border.length;
    }

    /**
     * Check if vertex is surrounded by city cells
     * @param {import('../geometry/DCEL.js').Vertex} vertex
     * @returns {boolean}
     */
    isInnerVertex(vertex) {
        for (const edge of vertex.edges) {
            const cell = edge.face.data;
            if (!cell.withinCity && !cell.waterbody) {
                return false;
            }
        }
        return true;
    }

    /**
     * Create geometry for all blocks in the group
     */
    createGeometry() {
        Random.restore(this.core.data.seed);

        const available = this.getAvailable();
        if (!available) {
            console.warn('Failed to calculate available area');
            this.alleys = [];
            this.blocks = [];
            return;
        }

        let attempts = 20;
        do {
            this.blocks = [];
            this.alleys = [];
            this.church = null;

            const shape = available.slice();
            const area = Math.abs(PolygonUtils.area(shape));
            const config = this.district?.alleys;

            const threshold = config
                ? config.minSq * Math.pow(2, config.sizeChaos * (2 * Random.next() - 1)) * config.blockSize
                : 400;

            if (area > threshold) {
                this.createAlleys(shape);
            } else {
                this.createBlock(shape);
            }

            if (!this.urban) {
                this.filter();
            }

            attempts--;
        } while (this.blocks.length === 0 && attempts > 0);

        if (this.blocks.length === 0) {
            console.warn('Failed to create non-empty alleys group');
        }
    }

    /**
     * Get available area for block generation
     * @returns {Point[]|null}
     */
    getAvailable() {
        const n = this.shape.length;
        const towerRadii = [];
        const edgeInsets = [];

        for (const edge of this.border) {
            // Tower radius
            let radius = 0;
            if (this.model) {
                for (const wall of this.model.walls) {
                    const r = wall.getTowerRadius(edge.origin);
                    if (r > 0) radius = Math.max(radius, r + 1.2);
                }
            }
            towerRadii.push(radius);

            // Edge inset
            let inset = 0;
            if (edge.data === null) {
                if (edge.twin?.face.data === this.model?.plaza) {
                    inset = 1;
                } else {
                    inset = 0.6;
                }
            } else {
                switch (edge.data) {
                    case 1: // COAST
                        inset = edge.face.data.landing ? 2 : 1.2;
                        break;
                    case 2: // ROAD
                        inset = 1;
                        break;
                    case 3: // WALL
                        inset = CurtainWall.THICKNESS / 2 + 1.2;
                        break;
                    case 4: // CANAL
                        inset = (this.model?.canals[0]?.width || 4) / 2 + 1.2;
                        break;
                    default:
                        inset = 0;
                }
            }
            edgeInsets.push(inset);
        }

        return Ward.inset(this.shape, edgeInsets, towerRadii);
    }

    /**
     * Create alley network by recursive bisection
     * @param {Point[]} shape
     */
    createAlleys(shape) {
        const config = this.district?.alleys;
        const minArea = config ? config.minSq * config.blockSize : 400;

        // Simple bisection
        const parts = this.bisect(shape, minArea);

        for (const part of parts) {
            const area = Math.abs(PolygonUtils.area(part));
            const threshold = config
                ? config.minSq * Math.pow(2, config.sizeChaos * (2 * Random.next() - 1))
                : 100;

            if (area < threshold) {
                this.createBlock(part, true);
            } else if (!this.church && area <= 4 * threshold) {
                this.createChurch(part);
            } else {
                this.createBlock(part);
            }
        }
    }

    /**
     * Bisect a polygon recursively
     * @param {Point[]} shape
     * @param {number} minArea
     * @returns {Point[][]}
     */
    bisect(shape, minArea) {
        const area = Math.abs(PolygonUtils.area(shape));
        if (area < minArea * 2) {
            return [shape];
        }

        // Find longest edge and cut perpendicular
        const idx = PolygonUtils.longestEdge(shape);
        const p1 = shape[idx];
        const p2 = shape[(idx + 1) % shape.length];

        const mid = GeomUtils.lerp(p1, p2, 0.3 + Random.next() * 0.4);
        const dir = p2.subtract(p1).perpendicular();

        // For now, return as-is (full implementation would cut polygon)
        return [shape];
    }

    /**
     * Create a block
     * @param {Point[]} shape
     * @param {boolean} [small=false]
     */
    createBlock(shape, small = false) {
        const block = new Block(this, shape, small);
        if (block.lots.length > 0) {
            this.blocks.push(block);
        }
    }

    /**
     * Create a church block
     * @param {Point[]} shape
     */
    createChurch(shape) {
        const block = new Block(this, shape, true);
        this.church = block;
        this.blocks.push(block);
    }

    /**
     * Filter blocks based on edge proximity
     */
    filter() {
        // Remove lots that are too close to edges
        const sqrtFaces = Math.sqrt(this.faces.length);

        for (const block of this.blocks) {
            block.lots = block.lots.filter(lot => {
                const center = PolygonUtils.center(lot);
                const edgeWeight = this.interpolate(center, this.blockM);

                if (isNaN(edgeWeight)) return false;

                const threshold = edgeWeight * sqrtFaces - 0.5;
                return Random.next() >= threshold;
            });
        }

        // Remove empty blocks
        this.blocks = this.blocks.filter(b => b.lots.length > 0);
    }

    /**
     * Triangulate group shape
     * @returns {Array<[Point, Point, Point]>}
     */
    getTris() {
        if (!this.tris) {
            const indices = Triangulation.earcut(this.shape);
            this.tris = indices.map(([a, b, c]) => [
                this.shape[a],
                this.shape[b],
                this.shape[c]
            ]);
        }
        return this.tris;
    }

    /**
     * Interpolate value at point using barycentric coords
     * @param {Point} point
     * @param {Map<Point, number>} values
     * @returns {number}
     */
    interpolate(point, values) {
        for (const [a, b, c] of this.getTris()) {
            const bary = GeomUtils.barycentric(a, b, c, point);

            if (bary.x >= 0 && bary.y >= 0 && bary.z >= 0) {
                return bary.x * (values.get(a) || 0) +
                       bary.y * (values.get(b) || 0) +
                       bary.z * (values.get(c) || 0);
            }
        }
        return NaN;
    }

    /**
     * Get circle that passes through point with tangent directions
     * @param {Point} p1
     * @param {Point} d1 - Direction at p1
     * @param {Point} p2
     * @param {Point} d2 - Direction at p2
     * @returns {Circle}
     */
    static getCircle(p1, d1, p2, d2) {
        const intersection = GeomUtils.intersectLines(
            p1.x, p1.y, -d1.y, d1.x,
            p2.x, p2.y, -d2.y, d2.x
        );

        const center = new Point(p1.x - d1.y * intersection.x, p1.y + d1.x * intersection.x);
        const radius = d1.length * intersection.x;

        return new Circle(center, radius);
    }

    /**
     * Get arc points between two angles
     * @param {Circle} circle
     * @param {number} startAngle
     * @param {number} endAngle
     * @param {number} spacing
     * @returns {Point[]|null}
     */
    static getArc(circle, startAngle, endAngle, spacing) {
        // Normalize angle difference
        if (startAngle - endAngle > Math.PI) {
            startAngle -= 2 * Math.PI;
        } else if (endAngle - startAngle > Math.PI) {
            endAngle -= 2 * Math.PI;
        }

        const radius = Math.abs(circle.radius);
        const arcLen = Math.abs(startAngle - endAngle) * radius;
        const numPoints = Math.floor(arcLen / spacing);

        if (numPoints < 2) return null;

        const points = [];
        for (let i = 0; i < numPoints; i++) {
            const t = i / (numPoints - 1);
            const angle = startAngle + (endAngle - startAngle) * t;
            points.push(circle.center.add(Point.polar(radius, angle)));
        }

        return points;
    }
}
