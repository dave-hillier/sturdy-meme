/**
 * Point.js - 2D Point/Vector class
 *
 * Represents a 2D point or vector with x, y coordinates.
 * Provides common geometric operations.
 */

export class Point {
    /**
     * Create a new Point
     * @param {number} x - X coordinate
     * @param {number} y - Y coordinate
     */
    constructor(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }

    /**
     * Get the length/magnitude of this vector
     */
    get length() {
        return Math.sqrt(this.x * this.x + this.y * this.y);
    }

    /**
     * Clone this point
     * @returns {Point}
     */
    clone() {
        return new Point(this.x, this.y);
    }

    /**
     * Set this point's coordinates to match another point
     * @param {Point} other - Point to copy from
     */
    copyFrom(other) {
        this.x = other.x;
        this.y = other.y;
        return this;
    }

    /**
     * Set coordinates
     * @param {number} x
     * @param {number} y
     */
    setTo(x, y) {
        this.x = x;
        this.y = y;
        return this;
    }

    /**
     * Add another point/vector
     * @param {Point} other
     * @returns {Point} New point
     */
    add(other) {
        return new Point(this.x + other.x, this.y + other.y);
    }

    /**
     * Subtract another point/vector
     * @param {Point} other
     * @returns {Point} New point
     */
    subtract(other) {
        return new Point(this.x - other.x, this.y - other.y);
    }

    /**
     * Scale this vector
     * @param {number} scalar
     * @returns {Point} New point
     */
    scale(scalar) {
        return new Point(this.x * scalar, this.y * scalar);
    }

    /**
     * Normalize this vector to unit length
     * @param {number} [length=1] - Target length
     * @returns {Point} This point (mutated)
     */
    normalize(length = 1) {
        const len = this.length;
        if (len > 0) {
            const scale = length / len;
            this.x *= scale;
            this.y *= scale;
        }
        return this;
    }

    /**
     * Get normalized copy
     * @returns {Point} New normalized point
     */
    normalized() {
        return this.clone().normalize();
    }

    /**
     * Dot product with another vector
     * @param {Point} other
     * @returns {number}
     */
    dot(other) {
        return this.x * other.x + this.y * other.y;
    }

    /**
     * Cross product (z-component of 3D cross)
     * @param {Point} other
     * @returns {number}
     */
    cross(other) {
        return this.x * other.y - this.y * other.x;
    }

    /**
     * Rotate this vector by angle
     * @param {number} angle - Angle in radians
     * @returns {Point} New rotated point
     */
    rotate(angle) {
        const cos = Math.cos(angle);
        const sin = Math.sin(angle);
        return new Point(
            this.x * cos - this.y * sin,
            this.y * cos + this.x * sin
        );
    }

    /**
     * Get perpendicular vector (rotated 90 degrees CCW)
     * @returns {Point}
     */
    perpendicular() {
        return new Point(-this.y, this.x);
    }

    /**
     * Linear interpolation to another point
     * @param {Point} other - Target point
     * @param {number} t - Interpolation factor (0-1)
     * @returns {Point}
     */
    lerp(other, t) {
        return new Point(
            this.x + (other.x - this.x) * t,
            this.y + (other.y - this.y) * t
        );
    }

    /**
     * Check if equal to another point
     * @param {Point} other
     * @param {number} [epsilon=0.0001] - Tolerance
     */
    equals(other, epsilon = 0.0001) {
        return Math.abs(this.x - other.x) < epsilon &&
               Math.abs(this.y - other.y) < epsilon;
    }

    /**
     * Distance to another point
     * @param {Point} other
     * @returns {number}
     */
    distanceTo(other) {
        return Point.distance(this, other);
    }

    /**
     * Distance squared to another point (faster, no sqrt)
     * @param {Point} other
     * @returns {number}
     */
    distanceSquaredTo(other) {
        const dx = this.x - other.x;
        const dy = this.y - other.y;
        return dx * dx + dy * dy;
    }

    /**
     * Angle from this point to another
     * @param {Point} other
     * @returns {number} Angle in radians
     */
    angleTo(other) {
        return Math.atan2(other.y - this.y, other.x - this.x);
    }

    toString() {
        return `Point(${this.x.toFixed(2)}, ${this.y.toFixed(2)})`;
    }

    // Static methods

    /**
     * Create point from polar coordinates
     * @param {number} r - Radius
     * @param {number} theta - Angle in radians
     */
    static polar(r, theta) {
        return new Point(r * Math.cos(theta), r * Math.sin(theta));
    }

    /**
     * Distance between two points
     * @param {Point} a
     * @param {Point} b
     */
    static distance(a, b) {
        const dx = a.x - b.x;
        const dy = a.y - b.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    /**
     * Distance squared between two points
     * @param {Point} a
     * @param {Point} b
     */
    static distanceSquared(a, b) {
        const dx = a.x - b.x;
        const dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    /**
     * Midpoint between two points
     * @param {Point} a
     * @param {Point} b
     */
    static midpoint(a, b) {
        return new Point((a.x + b.x) / 2, (a.y + b.y) / 2);
    }

    /**
     * Linear interpolation between two points
     * @param {Point} a - Start point
     * @param {Point} b - End point
     * @param {number} [t=0.5] - Interpolation factor
     */
    static lerp(a, b, t = 0.5) {
        return new Point(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t
        );
    }
}
