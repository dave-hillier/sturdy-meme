/**
 * Farm.js - Farmland ward
 *
 * Represents agricultural areas outside the city walls.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';
import { Building } from '../model/Building.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { GeomUtils } from '../geometry/GeomUtils.js';
import { Segment } from '../geometry/Segment.js';
import { CurtainWall } from '../model/CurtainWall.js';

/**
 * Farm ward with fields and farmhouses
 */
export class Farm extends Ward {
    /** Minimum furrow spacing */
    static MIN_FURROW = 2;

    /** Minimum subplot area */
    static MIN_SUBPLOT = 150;

    /**
     * Create a farm ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {Point[][]} */
        this.subPlots = [];

        /** @type {Segment[]} */
        this.furrows = [];

        /** @type {Point[][]} */
        this.buildings = [];

        /** @type {Point[]|null} */
        this.trees = null;
    }

    /**
     * Get available area (farm-specific insets)
     * @returns {Point[]|null}
     */
    getAvailable() {
        const shape = this.patch.shape;
        const n = shape.length;

        // Calculate insets - farms have larger buffers near walls
        const towerRadii = [];
        const edgeInsets = [];

        let i = 0;
        for (const edge of this.patch.face.edges()) {
            let radius = 0;
            for (const wall of this.model.walls) {
                radius = Math.max(radius, wall.getTowerRadius(edge.origin));
            }
            towerRadii.push(radius);

            // Edge insets based on neighbor type
            if (edge.data === null) {
                const neighbor = edge.twin?.face.data;
                if (neighbor?.ward instanceof Farm) {
                    edgeInsets.push(1);
                } else {
                    edgeInsets.push(0);
                }
            } else {
                switch (edge.data) {
                    case 2: // ROAD
                        edgeInsets.push(3);
                        break;
                    case 3: // WALL
                        edgeInsets.push(2 * CurtainWall.THICKNESS);
                        break;
                    case 4: // CANAL
                        edgeInsets.push(this.model.canals[0]?.width / 2 + 1.2 || 2);
                        break;
                    default:
                        edgeInsets.push(2);
                }
            }
            i++;
        }

        return Ward.inset(shape, edgeInsets, towerRadii);
    }

    /**
     * Create farm geometry
     */
    createGeometry() {
        Random.restore(this.patch.seed);

        const available = this.getAvailable();
        this.furrows = [];
        this.subPlots = this.splitField(available);

        // Filter out subplots that touch city areas
        const cityEdges = [];
        for (const edge of this.patch.face.edges()) {
            if (edge.twin?.face.data?.ward instanceof Ward &&
                !(edge.twin.face.data.ward instanceof Farm)) {
                cityEdges.push(edge);
            }
        }

        if (cityEdges.length > 0) {
            this.subPlots = this.subPlots.filter(plot => {
                for (let i = 0; i < plot.length; i++) {
                    const p1 = plot[i];
                    const p2 = plot[(i + 1) % plot.length];

                    for (const edge of cityEdges) {
                        if (GeomUtils.converge(p1, p2, edge.origin.point, edge.destination.point)) {
                            return false;
                        }
                    }
                }
                return true;
            });
        }

        // Round corners and add furrows
        for (let i = 0; i < this.subPlots.length; i++) {
            const plot = this.subPlots[i];
            const obb = PolygonUtils.obb(plot);
            const rounded = this.roundCorners(plot);
            this.subPlots[i] = rounded;

            // Add furrow lines
            const width = Point.distance(obb[0], obb[1]);
            const numFurrows = Math.ceil(width / Farm.MIN_FURROW);

            for (let f = 0; f < numFurrows; f++) {
                const t = (f + 0.5) / numFurrows;
                const start = GeomUtils.lerp(obb[0], obb[1], t);
                const end = GeomUtils.lerp(obb[3], obb[2], t);

                // Clip to plot boundary (simplified)
                if (Point.distance(start, end) > 1.2) {
                    this.furrows.push(new Segment(start, end));
                }
            }
        }

        // Add farmhouses to some plots
        this.buildings = [];
        for (const plot of this.subPlots) {
            if (Random.bool(0.2)) {
                const house = this.createFarmhouse(plot);
                if (house) {
                    this.buildings.push(house);
                }
            }
        }

        this.trees = null;
    }

    /**
     * Split field into subplots
     * @param {Point[]} field
     * @returns {Point[][]}
     */
    splitField(field) {
        if (!field) return [];

        const area = Math.abs(PolygonUtils.area(field));
        const threshold = Farm.MIN_SUBPLOT * (1 + Math.abs(Random.gaussian()));

        if (area < threshold) {
            return [field];
        }

        // Split along longest axis
        const obb = PolygonUtils.obb(field);
        const width = Point.distance(obb[1], obb[0]);
        const height = Point.distance(obb[2], obb[1]);

        const splitAxis = width > height ? 0 : 1;
        const splitT = 0.5 + 0.2 * Random.gaussian();

        // Create cutting line
        let cutStart, cutEnd;
        if (splitAxis === 0) {
            cutStart = GeomUtils.lerp(obb[0], obb[1], splitT);
            const dir = obb[1].subtract(obb[0]);
            const perp = new Point(-dir.y, dir.x);
            cutEnd = cutStart.add(perp);
        } else {
            cutStart = GeomUtils.lerp(obb[1], obb[2], splitT);
            const dir = obb[2].subtract(obb[1]);
            const perp = new Point(-dir.y, dir.x);
            cutEnd = cutStart.add(perp);
        }

        // Cut the polygon (simplified - would need full polygon cutting)
        // For now, return as single plot
        const results = [];
        for (const part of [field]) {  // Would be split parts
            results.push(...this.splitField(part));
        }

        return results.length > 0 ? results : [field];
    }

    /**
     * Round corners of plot
     * @param {Point[]} plot
     * @returns {Point[]}
     */
    roundCorners(plot) {
        const result = [];
        const n = plot.length;

        for (let i = 0; i < n; i++) {
            const curr = plot[i];
            const next = plot[(i + 1) % n];
            const dist = Point.distance(curr, next);

            if (dist < 2 * Farm.MIN_FURROW) {
                result.push(GeomUtils.lerp(curr, next));
            } else {
                result.push(GeomUtils.lerp(curr, next, Farm.MIN_FURROW / dist));
                result.push(GeomUtils.lerp(next, curr, Farm.MIN_FURROW / dist));
            }
        }

        return result;
    }

    /**
     * Create a farmhouse building
     * @param {Point[]} plot
     * @returns {Point[]|null}
     */
    createFarmhouse(plot) {
        const width = 4 + Random.next();
        const height = 2 + Random.next();
        const rect = PolygonUtils.rectangle(width, height);

        // Find longest edge
        const edgeIdx = PolygonUtils.longestEdge(plot);
        const p1 = plot[edgeIdx];
        const p2 = plot[(edgeIdx + 1) % plot.length];

        // Calculate rotation
        const dir = p2.subtract(p1).normalized();
        const angle = Math.atan2(dir.y, dir.x);

        PolygonUtils.rotate(rect, angle);

        // Position along edge
        const t = Random.bool() ? 0.25 : 0.75;
        const pos = GeomUtils.lerp(p1, p2, t);

        // Offset perpendicular
        const normal = new Point(-dir.y, dir.x);
        const offset = normal.scale(height / 2);
        pos.x += offset.x;
        pos.y += offset.y;

        PolygonUtils.translateBy(rect, pos);

        return Building.create(rect, 4 + Random.next()) || rect;
    }

    /**
     * Spawn trees along field edges
     * @returns {Point[]}
     */
    spawnTrees() {
        if (this.trees !== null) return this.trees;

        this.trees = [];
        const maxDist = Math.max(this.model.bounds.width, this.model.bounds.height) * Random.sum(3);

        for (const plot of this.subPlots) {
            const n = plot.length;
            for (let i = 0; i < n; i++) {
                const p1 = plot[i];
                const p2 = plot[(i + 1) % n];

                // Density decreases with distance from center
                const midpoint = GeomUtils.lerp(p1, p2);
                const density = 1 - midpoint.length / maxDist;

                // Add trees along edge
                const edgeLen = Point.distance(p1, p2);
                const numTrees = Math.floor(edgeLen * density / 3);

                for (let t = 0; t < numTrees; t++) {
                    const pos = GeomUtils.lerp(p1, p2, (t + 0.5) / numTrees);
                    // Add some randomness
                    pos.x += Random.gaussian() * 0.5;
                    pos.y += Random.gaussian() * 0.5;
                    this.trees.push(pos);
                }
            }
        }

        return this.trees;
    }

    /**
     * Get label for this ward
     * @returns {string}
     */
    getLabel() {
        return 'Farmland';
    }
}
