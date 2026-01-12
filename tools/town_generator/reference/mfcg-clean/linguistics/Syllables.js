/**
 * Syllables.js - Syllable parsing for name generation
 *
 * Provides utilities for splitting words into syllables,
 * used by the Markov chain name generator.
 */

/**
 * Common English vowel patterns (sorted by length for matching)
 */
export const VOWELS = [
    'you', 'yo', 'ya', 'oo', 'ea', 'ey', 'ou', 'ay', 'au', 'oy', 'ue', 'ua',
    'u', 'o', 'a', 'e', 'i', 'y'
];

/**
 * Common English consonant patterns (sorted by length for matching)
 */
export const CONSONANTS = [
    'wh', 'th', 'ch', 'sh', 'qu',
    'b', 'c', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'm', 'n', 'p', 'q', 'r', 's', 't', 'v', 'w', 'x', 'z'
];

/**
 * Syllable utilities
 */
export class Syllables {
    /**
     * Split text into syllables
     *
     * @param {string} text - Text to split (may contain spaces)
     * @returns {string[]} Array of syllables
     */
    static split(text) {
        const result = [];

        for (const word of text.split(' ')) {
            if (word !== '') {
                result.push(...Syllables.splitWord(word));
            }
        }

        return result;
    }

    /**
     * Split a single word into syllables
     *
     * @param {string} word - Word to split
     * @returns {string[]} Array of syllables
     */
    static splitWord(word) {
        const syllables = [];
        let remaining = word;

        while (remaining.length > 0) {
            let syllable;

            // Special case: trailing 'e' after consonant
            if (syllables.length === 0 && remaining.endsWith('e')) {
                syllable = Syllables.pinch(remaining.slice(0, -1)) + 'e';
            } else {
                syllable = Syllables.pinch(remaining);
            }

            syllables.unshift(syllable);
            remaining = remaining.slice(0, remaining.length - syllable.length);

            // If no vowels remain, prepend to first syllable
            if (!Syllables.hasVowel(remaining)) {
                syllables[0] = remaining + syllables[0];
                remaining = '';
            }
        }

        return syllables;
    }

    /**
     * Extract a syllable from the end of a word
     *
     * @param {string} word - Word to extract from
     * @returns {string} The extracted syllable
     */
    static pinch(word) {
        // Find last vowel position
        let lastVowelIdx = -1;
        for (let i = word.length - 1; i >= 0; i--) {
            if (VOWELS.includes(word.charAt(i))) {
                lastVowelIdx = i;
                break;
            }
        }

        if (lastVowelIdx < 0) {
            return word;
        }

        // Check for multi-character vowel patterns
        for (const vowel of VOWELS) {
            const start = lastVowelIdx - (vowel.length - 1);
            if (start >= 0 && word.substr(start, vowel.length) === vowel) {
                lastVowelIdx = start - 1;
                break;
            }
        }

        if (lastVowelIdx < 0) {
            return word;
        }

        // Check for consonant clusters before vowel
        for (const consonant of CONSONANTS) {
            const start = lastVowelIdx - (consonant.length - 1);
            if (start >= 0 && word.substr(start, consonant.length) === consonant) {
                return word.substr(start);
            }
        }

        return word.substr(lastVowelIdx + 1);
    }

    /**
     * Check if string contains a vowel
     *
     * @param {string} str - String to check
     * @returns {boolean}
     */
    static hasVowel(str) {
        for (const vowel of VOWELS) {
            if (str.includes(vowel)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Count syllables in a word
     *
     * @param {string} word - Word to count
     * @returns {number} Syllable count
     */
    static count(word) {
        return Syllables.splitWord(word).length;
    }
}
