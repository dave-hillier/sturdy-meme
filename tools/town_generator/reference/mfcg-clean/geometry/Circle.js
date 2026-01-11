/**
 * Circle.js - Circle geometry class
 */

import { Point } from '../core/Point.js';

export class Circle {
    /**
     * Create a circle
     * @param {Point} center - Center point
     * @param {number} radius - Circle radius
     */
    constructor(center, radius) {
        this.c = center;
        this.r = radius;
    }

    /**
     * Get center point
     * @returns {Point}
     */
    get center() {
        return this.c;
    }

    /**
     * Get radius
     * @returns {number}
     */
    get radius() {
        return Math.abs(this.r);
    }

    /**
     * Check if point is inside circle
     * @param {Point} point
     * @returns {boolean}
     */
    contains(point) {
        return Point.distanceSquared(this.c, point) <= this.r * this.r;
    }

    /**
     * Get point on circumference at given angle
     * @param {number} angle - Angle in radians
     * @returns {Point}
     */
    pointAt(angle) {
        return this.c.add(Point.polar(this.r, angle));
    }

    /**
     * Get circumference
     * @returns {number}
     */
    get circumference() {
        return 2 * Math.PI * Math.abs(this.r);
    }

    /**
     * Get area
     * @returns {number}
     */
    get area() {
        return Math.PI * this.r * this.r;
    }
}
