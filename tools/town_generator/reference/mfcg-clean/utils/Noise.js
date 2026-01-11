/**
 * Noise.js - Noise generation utilities
 *
 * Implements Perlin noise and fractal noise for terrain generation.
 */

import { Random } from '../core/Random.js';

/**
 * Perlin noise generator
 */
export class Perlin {
    /**
     * Create Perlin noise generator
     * @param {number} [seed] - Random seed
     */
    constructor(seed) {
        this.perm = new Array(512);
        this.gradP = new Array(512);

        this.seed(seed || Random.seed);
    }

    /**
     * Seed the noise generator
     * @param {number} seed
     */
    seed(seed) {
        // Generate permutation table
        const p = [];
        for (let i = 0; i < 256; i++) {
            p[i] = i;
        }

        // Shuffle using seed
        let s = seed;
        for (let i = 255; i > 0; i--) {
            s = (s * 48271) % 2147483647;
            const j = s % (i + 1);
            [p[i], p[j]] = [p[j], p[i]];
        }

        // Double the permutation table
        for (let i = 0; i < 512; i++) {
            this.perm[i] = p[i & 255];
            this.gradP[i] = GRAD3[this.perm[i] % 12];
        }
    }

    /**
     * 2D Perlin noise
     * @param {number} x
     * @param {number} y
     * @returns {number} Value in [-1, 1]
     */
    noise2D(x, y) {
        // Find unit grid cell
        let X = Math.floor(x);
        let Y = Math.floor(y);

        // Get relative position within cell
        x -= X;
        y -= Y;

        // Wrap to 0..255
        X &= 255;
        Y &= 255;

        // Calculate gradient contributions
        const n00 = this.gradP[X + this.perm[Y]].dot2(x, y);
        const n01 = this.gradP[X + this.perm[Y + 1]].dot2(x, y - 1);
        const n10 = this.gradP[X + 1 + this.perm[Y]].dot2(x - 1, y);
        const n11 = this.gradP[X + 1 + this.perm[Y + 1]].dot2(x - 1, y - 1);

        // Compute fade curve
        const u = fade(x);
        const v = fade(y);

        // Interpolate
        return lerp(
            lerp(n00, n10, u),
            lerp(n01, n11, u),
            v
        );
    }

    /**
     * Get noise value at position
     * @param {number} x
     * @param {number} y
     * @returns {number}
     */
    get(x, y) {
        return (this.noise2D(x, y) + 1) / 2; // Map to [0, 1]
    }
}

/**
 * Fractal noise (FBM - Fractal Brownian Motion)
 */
export class Noise {
    /**
     * Create fractal noise generator
     * @param {number} octaves - Number of octaves
     * @param {number} [persistence=0.5] - Amplitude falloff per octave
     * @param {number} [lacunarity=2] - Frequency multiplier per octave
     */
    constructor(octaves, persistence = 0.5, lacunarity = 2) {
        this.octaves = octaves;
        this.persistence = persistence;
        this.lacunarity = lacunarity;
        this.perlin = new Perlin();
    }

    /**
     * Get fractal noise value
     * @param {number} x
     * @param {number} y
     * @returns {number} Value in [0, 1]
     */
    get(x, y) {
        let total = 0;
        let amplitude = 1;
        let frequency = 1;
        let maxValue = 0;

        for (let i = 0; i < this.octaves; i++) {
            total += this.perlin.noise2D(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= this.persistence;
            frequency *= this.lacunarity;
        }

        return (total / maxValue + 1) / 2; // Map to [0, 1]
    }

    /**
     * Create fractal noise generator with default settings
     * @param {number} octaves
     * @returns {Noise}
     */
    static fractal(octaves) {
        return new Noise(octaves);
    }
}

// Gradient vectors for 3D noise (used for 2D as well)
const GRAD3 = [
    new Grad(1, 1, 0), new Grad(-1, 1, 0), new Grad(1, -1, 0), new Grad(-1, -1, 0),
    new Grad(1, 0, 1), new Grad(-1, 0, 1), new Grad(1, 0, -1), new Grad(-1, 0, -1),
    new Grad(0, 1, 1), new Grad(0, -1, 1), new Grad(0, 1, -1), new Grad(0, -1, -1)
];

function Grad(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
}

Grad.prototype.dot2 = function(x, y) {
    return this.x * x + this.y * y;
};

Grad.prototype.dot3 = function(x, y, z) {
    return this.x * x + this.y * y + this.z * z;
};

// Fade function (6t^5 - 15t^4 + 10t^3)
function fade(t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

// Linear interpolation
function lerp(a, b, t) {
    return a + t * (b - a);
}
