/**
 * Random.js - Seeded random number generator
 *
 * A deterministic random number generator using a linear congruential generator (LCG).
 * Uses the same algorithm as the original MFCG to ensure reproducible city generation.
 */

export class Random {
    static seed = 1;
    static savedSeed = 1;

    /**
     * Get next random number in [0, 1)
     * Uses LCG: seed = (48271 * seed) % 2147483647
     */
    static next() {
        Random.seed = (48271 * Random.seed % 2147483647) | 0;
        return Random.seed / 2147483647;
    }

    /**
     * Get random integer in range [0, max)
     * @param {number} max - Upper bound (exclusive)
     */
    static int(max) {
        return Math.floor(Random.next() * max);
    }

    /**
     * Get random float in range [min, max)
     * @param {number} min - Lower bound
     * @param {number} max - Upper bound
     */
    static float(min, max) {
        return min + Random.next() * (max - min);
    }

    /**
     * Get random boolean with given probability
     * @param {number} probability - Probability of true (default 0.5)
     */
    static bool(probability = 0.5) {
        return Random.next() < probability;
    }

    /**
     * Get gaussian-distributed random number using Box-Muller transform
     * Approximated using sum of 3 uniform randoms
     */
    static gaussian() {
        return (Random.next() + Random.next() + Random.next()) / 3 * 2 - 1;
    }

    /**
     * Get sum of n uniform random numbers, normalized
     * @param {number} n - Number of samples to sum
     */
    static sum(n) {
        let total = 0;
        for (let i = 0; i < n; i++) {
            total += Random.next();
        }
        return total / n;
    }

    /**
     * Save current seed state
     */
    static save() {
        Random.savedSeed = Random.seed;
    }

    /**
     * Restore previously saved seed state
     * @param {number} [seed] - Optional specific seed to restore to
     */
    static restore(seed) {
        if (seed !== undefined) {
            Random.seed = seed;
        } else {
            Random.seed = Random.savedSeed;
        }
    }

    /**
     * Pick random element from array
     * @param {Array} array - Array to pick from
     */
    static pick(array) {
        if (!array || array.length === 0) return null;
        return array[Random.int(array.length)];
    }

    /**
     * Pick random element with weighted probability
     * @param {Array} items - Items to pick from
     * @param {Array<number>} weights - Weights for each item
     */
    static weighted(items, weights) {
        const total = weights.reduce((sum, w) => sum + w, 0);
        let threshold = Random.next() * total;
        for (let i = 0; i < items.length; i++) {
            threshold -= weights[i];
            if (threshold <= 0) {
                return items[i];
            }
        }
        return items[items.length - 1];
    }

    /**
     * Shuffle array in place using Fisher-Yates
     * @param {Array} array - Array to shuffle
     */
    static shuffle(array) {
        for (let i = array.length - 1; i > 0; i--) {
            const j = Random.int(i + 1);
            [array[i], array[j]] = [array[j], array[i]];
        }
        return array;
    }
}
