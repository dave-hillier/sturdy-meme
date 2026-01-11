/**
 * MathUtils.js - Mathematical utility functions
 */

export class MathUtils {
    /**
     * Clamp a value between min and max
     * @param {number} value
     * @param {number} min
     * @param {number} max
     */
    static clamp(value, min, max) {
        return Math.max(min, Math.min(max, value));
    }

    /**
     * Clamp integer value between min and max
     * @param {number} value
     * @param {number} min
     * @param {number} max
     */
    static clampi(value, min, max) {
        return Math.max(min, Math.min(max, Math.floor(value)));
    }

    /**
     * Linear interpolation
     * @param {number} a - Start value
     * @param {number} b - End value
     * @param {number} t - Interpolation factor (0-1)
     */
    static lerp(a, b, t) {
        return a + (b - a) * t;
    }

    /**
     * Inverse linear interpolation
     * @param {number} a - Start value
     * @param {number} b - End value
     * @param {number} v - Value between a and b
     * @returns {number} t such that lerp(a, b, t) = v
     */
    static inverseLerp(a, b, v) {
        return (v - a) / (b - a);
    }

    /**
     * Remap value from one range to another
     * @param {number} value
     * @param {number} inMin
     * @param {number} inMax
     * @param {number} outMin
     * @param {number} outMax
     */
    static remap(value, inMin, inMax, outMin, outMax) {
        const t = MathUtils.inverseLerp(inMin, inMax, value);
        return MathUtils.lerp(outMin, outMax, t);
    }

    /**
     * Smoothstep interpolation
     * @param {number} edge0
     * @param {number} edge1
     * @param {number} x
     */
    static smoothstep(edge0, edge1, x) {
        const t = MathUtils.clamp((x - edge0) / (edge1 - edge0), 0, 1);
        return t * t * (3 - 2 * t);
    }

    /**
     * Convert degrees to radians
     * @param {number} degrees
     */
    static toRadians(degrees) {
        return degrees * Math.PI / 180;
    }

    /**
     * Convert radians to degrees
     * @param {number} radians
     */
    static toDegrees(radians) {
        return radians * 180 / Math.PI;
    }

    /**
     * Normalize angle to range [-PI, PI]
     * @param {number} angle - Angle in radians
     */
    static normalizeAngle(angle) {
        while (angle > Math.PI) angle -= 2 * Math.PI;
        while (angle < -Math.PI) angle += 2 * Math.PI;
        return angle;
    }

    /**
     * Sign function
     * @param {number} x
     * @returns {number} -1, 0, or 1
     */
    static sign(x) {
        return x > 0 ? 1 : x < 0 ? -1 : 0;
    }

    /**
     * Modulo that always returns positive
     * @param {number} a
     * @param {number} b
     */
    static mod(a, b) {
        return ((a % b) + b) % b;
    }

    /**
     * Check if two floats are approximately equal
     * @param {number} a
     * @param {number} b
     * @param {number} [epsilon=0.0001]
     */
    static approxEqual(a, b, epsilon = 0.0001) {
        return Math.abs(a - b) < epsilon;
    }
}
