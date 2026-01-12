/**
 * Markov.js - Markov chain text generation
 *
 * Generates procedural names using Markov chains trained on
 * phoneme sequences from source words.
 */

import { Random } from '../core/Random.js';
import { Syllables, VOWELS, CONSONANTS } from './Syllables.js';

/**
 * All phonemes (vowels + consonants, sorted by length)
 */
let phonemes = null;

/**
 * Get phonemes list (lazy initialization)
 * @returns {string[]}
 */
function getPhonemes() {
    if (phonemes === null) {
        phonemes = [...VOWELS, ...CONSONANTS];
        phonemes.sort((a, b) => b.length - a.length);
    }
    return phonemes;
}

/**
 * Markov chain name generator
 */
export class Markov {
    /**
     * Create a Markov chain from source words
     *
     * @param {string[]} source - Array of source words to learn from
     */
    constructor(source) {
        /** @type {string[]} Original source words */
        this.source = source;

        /** @type {Map<string, string[]>} Transition map: prefix -> possible next phonemes */
        this.map = new Map();

        // Train on source words
        for (const word of source) {
            if (word === '') continue;

            const phonemeList = Markov.split(word.toLowerCase());
            const history = [];

            for (const phoneme of phonemeList) {
                const key = history.join('');

                if (!this.map.has(key)) {
                    this.map.set(key, []);
                }
                this.map.get(key).push(phoneme);

                history.push(phoneme);
                if (history.length > 2) {
                    history.shift();
                }
            }

            // Add end-of-word marker
            const finalKey = history.join('');
            if (!this.map.has(finalKey)) {
                this.map.set(finalKey, []);
            }
            this.map.get(finalKey).push('');
        }
    }

    /**
     * Split a word into phonemes
     *
     * @param {string} word - Word to split
     * @returns {string[]} Array of phonemes
     */
    static split(word) {
        const result = [];
        const ph = getPhonemes();
        let remaining = word;

        while (remaining !== '') {
            let found = false;

            // Try to match phonemes from the end
            for (const phoneme of ph) {
                if (remaining.endsWith(phoneme)) {
                    result.unshift(phoneme);
                    remaining = remaining.slice(0, remaining.length - phoneme.length);
                    found = true;
                    break;
                }
            }

            // If no phoneme matched, skip last character
            if (!found) {
                remaining = remaining.slice(0, -1);
            }
        }

        return result;
    }

    /**
     * Generate a new word
     *
     * @param {number} [maxSyllables=-1] - Maximum syllables (-1 = no limit)
     * @returns {string} Generated word
     */
    generate(maxSyllables = -1) {
        for (let attempts = 0; attempts < 100; attempts++) {
            let word = '';
            const history = [];

            // Start with a random starting phoneme
            let next = Random.pick(this.map.get('') || ['']);

            while (next !== '') {
                word += next;
                history.push(next);

                if (history.length > 2) {
                    history.shift();
                }

                const key = history.join('');
                const options = this.map.get(key);

                if (!options || options.length === 0) {
                    break;
                }

                next = Random.pick(options);
            }

            // Check syllable constraint
            if (maxSyllables === -1 || Syllables.splitWord(word).length <= maxSyllables) {
                // Ensure it's not a copy of a source word
                if (this.source.indexOf(word) === -1) {
                    return word;
                }
            }
        }

        // Fallback: return a random source word
        return Random.pick(this.source);
    }

    /**
     * Generate a word with minimum length
     *
     * @param {number} minLength - Minimum word length
     * @param {number} [maxSyllables=-1] - Maximum syllables
     * @returns {string} Generated word
     */
    generateMinLength(minLength, maxSyllables = -1) {
        for (let attempts = 0; attempts < 50; attempts++) {
            const word = this.generate(maxSyllables);
            if (word.length >= minLength) {
                return word;
            }
        }
        return this.generate(maxSyllables);
    }

    /**
     * Create a Markov chain from a space-separated word list
     *
     * @param {string} text - Space-separated words
     * @returns {Markov}
     */
    static fromText(text) {
        return new Markov(text.split(' ').filter(w => w.length > 0));
    }
}
