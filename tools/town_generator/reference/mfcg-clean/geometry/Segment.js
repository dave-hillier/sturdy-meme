/**
 * Segment.js - Line segment class
 */

import { Point } from '../core/Point.js';

export class Segment {
    /**
     * Create a line segment
     * @param {Point} start - Start point
     * @param {Point} end - End point
     */
    constructor(start, end) {
        this.start = start;
        this.end = end;
    }

    /**
     * Get length of segment
     * @returns {number}
     */
    get length() {
        return Point.distance(this.start, this.end);
    }

    /**
     * Get midpoint
     * @returns {Point}
     */
    get midpoint() {
        return Point.midpoint(this.start, this.end);
    }

    /**
     * Get direction vector (normalized)
     * @returns {Point}
     */
    get direction() {
        return this.end.subtract(this.start).normalized();
    }

    /**
     * Get normal vector (perpendicular, normalized)
     * @returns {Point}
     */
    get normal() {
        const dir = this.direction;
        return new Point(-dir.y, dir.x);
    }

    /**
     * Get point at parameter t along segment
     * @param {number} t - Parameter (0 = start, 1 = end)
     * @returns {Point}
     */
    pointAt(t) {
        return Point.lerp(this.start, this.end, t);
    }

    /**
     * Get closest point on segment to given point
     * @param {Point} point
     * @returns {Point}
     */
    closestPoint(point) {
        const v = this.end.subtract(this.start);
        const u = point.subtract(this.start);
        const t = Math.max(0, Math.min(1, u.dot(v) / v.dot(v)));
        return this.pointAt(t);
    }

    /**
     * Get distance from point to segment
     * @param {Point} point
     * @returns {number}
     */
    distanceToPoint(point) {
        return Point.distance(point, this.closestPoint(point));
    }

    /**
     * Check if segment intersects another segment
     * @param {Segment} other
     * @returns {Point|null} Intersection point or null
     */
    intersects(other) {
        const p = this.start;
        const r = this.end.subtract(this.start);
        const q = other.start;
        const s = other.end.subtract(other.start);

        const rxs = r.cross(s);
        if (Math.abs(rxs) < 0.0001) {
            return null; // Parallel or collinear
        }

        const qmp = q.subtract(p);
        const t = qmp.cross(s) / rxs;
        const u = qmp.cross(r) / rxs;

        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
            return this.pointAt(t);
        }
        return null;
    }

    /**
     * Clone this segment
     * @returns {Segment}
     */
    clone() {
        return new Segment(this.start.clone(), this.end.clone());
    }
}
