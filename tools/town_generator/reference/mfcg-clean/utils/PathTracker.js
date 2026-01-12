/**
 * PathTracker.js - Track position along a polyline path
 *
 * Provides utilities for walking along a path and getting positions
 * at specific distances.
 */

import { Point } from '../core/Point.js';

/**
 * Tracks position along a polyline path
 */
export class PathTracker {
    /**
     * Create a path tracker
     * @param {Point[]} path - Array of points forming the path
     */
    constructor(path) {
        if (path.length < 2) {
            throw new Error('Path must have at least 2 points');
        }

        /** @type {Point[]} */
        this.path = path;

        /** @type {number} */
        this.size = path.length;

        this.reset();
    }

    /**
     * Reset tracker to start of path
     */
    reset() {
        /** @type {number} Current segment index */
        this.curIndex = 0;

        /** @type {number} Distance along path to start of current segment */
        this.offset = 0;

        /** @type {Point} Current segment direction vector */
        this.curVector = this.path[1].subtract(this.path[0]);

        /** @type {number} Length of current segment */
        this.curLength = this.curVector.length;
    }

    /**
     * Get position at a specific distance along the path
     *
     * @param {number} distance - Distance along the path
     * @returns {Point|null} Position, or null if past end of path
     */
    getPos(distance) {
        // If distance is before current position, reset
        if (distance < this.offset) {
            this.reset();
        }

        // Advance through segments until we find the right one
        while (distance > this.offset + this.curLength) {
            this.curIndex++;

            if (this.curIndex >= this.size - 1) {
                this.reset();
                return null; // Past end of path
            }

            this.offset += this.curLength;
            this.curVector = this.path[this.curIndex + 1].subtract(this.path[this.curIndex]);
            this.curLength = this.curVector.length;
        }

        // Interpolate within current segment
        const t = (distance - this.offset) / this.curLength;
        return Point.lerp(this.path[this.curIndex], this.path[this.curIndex + 1], t);
    }

    /**
     * Get a segment of the path between two distances
     *
     * @param {number} startDist - Start distance along path
     * @param {number} endDist - End distance along path
     * @returns {Point[]} Array of points forming the segment
     */
    getSegment(startDist, endDist) {
        const startPos = this.getPos(startDist);
        const startIdx = this.curIndex + 1;

        const endPos = this.getPos(endDist);

        // Get all intermediate points
        const result = this.path.slice(startIdx, this.curIndex + 1);

        // Add start and end positions
        result.unshift(startPos);
        result.push(endPos);

        return result;
    }

    /**
     * Get total length of the path
     * @returns {number}
     */
    get totalLength() {
        let total = 0;
        for (let i = 0; i < this.path.length - 1; i++) {
            total += Point.distance(this.path[i], this.path[i + 1]);
        }
        return total;
    }

    /**
     * Get current tangent (direction) vector
     * @returns {Point}
     */
    get tangent() {
        return this.curVector;
    }

    /**
     * Get normalized tangent vector
     * @returns {Point}
     */
    get tangentNormalized() {
        return this.curVector.normalized();
    }

    /**
     * Get normal (perpendicular) vector at current position
     * @returns {Point}
     */
    get normal() {
        const t = this.tangentNormalized;
        return new Point(-t.y, t.x);
    }

    /**
     * Sample points evenly along the path
     *
     * @param {number} count - Number of samples
     * @param {number} [startOffset=0] - Distance to start from
     * @param {number} [endOffset=0] - Distance to stop before end
     * @returns {Point[]} Array of evenly spaced points
     */
    sample(count, startOffset = 0, endOffset = 0) {
        const length = this.totalLength;
        const usableLength = length - startOffset - endOffset;

        if (count <= 1 || usableLength <= 0) {
            return [this.getPos(startOffset)];
        }

        const step = usableLength / (count - 1);
        const result = [];

        for (let i = 0; i < count; i++) {
            const pos = this.getPos(startOffset + i * step);
            if (pos) {
                result.push(pos);
            }
        }

        return result;
    }

    /**
     * Get points along path at regular intervals
     *
     * @param {number} spacing - Distance between points
     * @returns {Point[]} Array of points
     */
    sampleSpaced(spacing) {
        const length = this.totalLength;
        const count = Math.floor(length / spacing) + 1;
        const result = [];

        for (let i = 0; i < count; i++) {
            const pos = this.getPos(i * spacing);
            if (pos) {
                result.push(pos);
            }
        }

        return result;
    }
}
