/**
 * Castle.js - Castle/citadel ward
 *
 * Represents the ruler's castle or citadel at the edge of the city.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { Ward } from './Ward.js';
import { CurtainWall } from '../model/CurtainWall.js';
import { Building } from '../model/Building.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { GeomUtils } from '../geometry/GeomUtils.js';

/**
 * Castle ward with fortified walls
 */
export class Castle extends Ward {
    /**
     * Create a castle ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        // Find vertices that face outside city
        const excludePoints = [];
        for (const vertex of patch.shape) {
            const cells = model.cellsByVertex(model.dcel.vertices.get(vertex));
            const hasOutside = cells.some(c => !c.withinCity);
            if (hasOutside) {
                excludePoints.push(vertex);
            }
        }

        /** @type {CurtainWall} */
        this.wall = new CurtainWall(true, model, [patch], excludePoints);

        /** @type {Point[]} */
        this.building = [];

        // Adjust shape for better castle footprint
        this.adjustShape(model);
    }

    /**
     * Adjust the castle shape for better proportions
     * @param {import('../model/City.js').City} model
     */
    adjustShape(model) {
        const shape = this.patch.shape;
        const center = PolygonUtils.centroid(shape);

        // Calculate min and max radius
        let minRadius = Infinity;
        let maxRadius = 0;

        for (const p of shape) {
            const dist = Point.distance(p, center);
            if (dist < minRadius) minRadius = dist;
            if (dist > maxRadius) maxRadius = dist;
        }

        // Expand if too small
        while (minRadius < 10) {
            const expandFactor = Math.pow(2 * Math.max(15, maxRadius), -0.25);

            for (const vertex of model.dcel.vertices.values()) {
                const dist = Point.distance(vertex.point, center);
                if (dist < 2 * Math.max(15, maxRadius)) {
                    const dir = vertex.point.subtract(center);
                    const scale = Math.pow(dist / (2 * Math.max(15, maxRadius)), expandFactor);
                    vertex.point.copyFrom(center.add(dir.scale(scale)));
                }
            }

            // Recalculate
            minRadius = Infinity;
            maxRadius = 0;
            for (const p of shape) {
                const dist = Point.distance(p, center);
                if (dist < minRadius) minRadius = dist;
                if (dist > maxRadius) maxRadius = dist;
            }
        }

        // Equalize shape to be more circular
        const gateVertex = this.wall.gates[0];
        const anchors = [gateVertex.point];

        // Add adjacent vertices to anchors
        for (const edge of gateVertex.edges) {
            if (edge.next) anchors.push(edge.next.origin.point);
        }

        let compactness = PolygonUtils.compactness(shape);
        while (compactness < 0.75) {
            this.equalize(center, 0.2, anchors);

            const newCompactness = PolygonUtils.compactness(shape);
            if (Math.abs(newCompactness - compactness) < 0.001) {
                break; // Not improving
            }
            compactness = newCompactness;
        }
    }

    /**
     * Equalize shape toward circular
     * @param {Point} center
     * @param {number} factor
     * @param {Point[]} anchors - Points to keep fixed
     */
    equalize(center, factor, anchors) {
        const shape = this.patch.shape;
        const n = shape.length;

        // Compute average rotated vector
        const avg = new Point(0, 0);
        for (let i = 0; i < n; i++) {
            const v = shape[i].subtract(center);
            const angle = -2 * Math.PI * i / n;
            const rotated = v.rotate(angle);
            avg.x += rotated.x;
            avg.y += rotated.y;
        }
        avg.x /= n;
        avg.y /= n;

        // Move vertices toward their ideal positions
        for (let i = 0; i < n; i++) {
            if (anchors.includes(shape[i])) continue;

            const angle = 2 * Math.PI * i / n;
            const ideal = center.add(avg.rotate(angle));
            const newPos = GeomUtils.lerp(shape[i], ideal, factor);
            shape[i].copyFrom(newPos);
        }
    }

    /**
     * Create geometry for the castle
     */
    createGeometry() {
        Random.restore(this.patch.seed);

        // Create building inside walls
        const innerShape = PolygonUtils.shrink(
            this.patch.shape,
            CurtainWall.THICKNESS + 2
        );

        if (innerShape) {
            const rect = PolygonUtils.lira(innerShape);
            const building = Building.create(
                rect,
                PolygonUtils.area(this.patch.shape) / 25,
                false,
                true,
                0.4
            );

            this.building = building || rect;
        }
    }

    /**
     * Get label for this ward
     * @returns {string}
     */
    getLabel() {
        return 'Castle';
    }

    /**
     * Handle context menu
     * @param {*} menu
     * @param {number} x
     * @param {number} y
     */
    onContext(menu, x, y) {
        const point = new Point(x, y);
        if (PolygonUtils.containsPoint(this.building, point)) {
            // Could add "Open in Dwellings" option
        }
    }
}
