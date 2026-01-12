/**
 * Namer.js - Town, street, and district name generation
 *
 * Generates procedural names for cities, districts, and streets
 * using Markov chains and grammar templates.
 */

import { Random } from '../core/Random.js';
import { Markov } from './Markov.js';
import { Syllables } from './Syllables.js';

/**
 * Default English word corpus for general names
 */
const ENGLISH_WORDS = `
    stone river bridge castle tower gate wall port harbor market
    temple church abbey cross crown royal king queen prince lord
    silver gold iron copper brass bronze steel oak elm ash
    north south east west upper lower old new high low
    green red white black blue grey dark light bright fair
    hill dale vale glen wood ford brook creek lake pond
    smith baker miller wright mason potter weaver fisher hunter
`.trim().split(/\s+/);

/**
 * Fantasy/Elven style words
 */
const ELVEN_WORDS = `
    ara ari eli eri ala ali ela eli ila ili ola oli ula uli
    thal thel thil thol thul dor dol del dil dul
    mir mal mel mil mul nar nal nel nil nul
    gor gol gel gil gul vor vol vel vil vul
    wen wan win won wun ren ran rin ron run
    las los les lis lus kas kos kes kis kus
    far fal fel fil ful tar tal tel til tul
`.trim().split(/\s+/);

/**
 * Male given names
 */
const MALE_NAMES = `
    john william james robert michael david richard joseph thomas charles
    edward henry george albert frederick arthur ernest walter harold
    edmund edgar alfred raymond leonard stanley herbert gilbert norman
`.trim().split(/\s+/);

/**
 * Female given names
 */
const FEMALE_NAMES = `
    mary elizabeth sarah anne margaret jane catherine alice rose emily
    charlotte victoria emma isabella sophia grace hannah rachel helen
    eleanor beatrice florence harriet louisa matilda agnes edith dorothy
`.trim().split(/\s+/);

/**
 * Lazy-loaded Markov chains
 */
let englishMarkov = null;
let elvenMarkov = null;
let maleMarkov = null;
let femaleMarkov = null;

/**
 * Name generator for cities, districts, and streets
 */
export class Namer {
    /**
     * Reset/initialize the name generator
     */
    static reset() {
        englishMarkov = new Markov(ENGLISH_WORDS);
        elvenMarkov = new Markov(ELVEN_WORDS);
        maleMarkov = new Markov(MALE_NAMES);
        femaleMarkov = new Markov(FEMALE_NAMES);
    }

    /**
     * Generate a given name (male or female)
     * @returns {string}
     */
    static given() {
        if (!maleMarkov) Namer.reset();

        if (Random.bool()) {
            return Namer.capitalize(maleMarkov.generate(4));
        } else {
            return Namer.capitalize(femaleMarkov.generate(4));
        }
    }

    /**
     * Generate an English-style word
     *
     * @param {number} [minLength=4] - Minimum word length
     * @param {number} [maxSyllables=3] - Maximum syllables
     * @returns {string}
     */
    static word(minLength = 4, maxSyllables = 3) {
        if (!englishMarkov) Namer.reset();
        return englishMarkov.generateMinLength(minLength, maxSyllables);
    }

    /**
     * Generate a fantasy/elven style word
     * @returns {string}
     */
    static fantasy() {
        if (!elvenMarkov) Namer.reset();
        return Namer.capitalize(elvenMarkov.generate());
    }

    /**
     * Generate a city name
     *
     * @param {import('../model/City.js').City} city - The city (for context)
     * @returns {string}
     */
    static cityName(city) {
        const bp = city.bp;

        // Choose name style based on city features
        const patterns = [];

        // Basic patterns
        patterns.push(() => Namer.capitalize(Namer.word()) + Namer.citySuffix());
        patterns.push(() => Namer.fantasy());

        // Feature-based patterns
        if (bp.coast) {
            patterns.push(() => Namer.capitalize(Namer.word()) + ' Harbor');
            patterns.push(() => Namer.capitalize(Namer.word()) + ' Port');
        }

        if (bp.river) {
            patterns.push(() => Namer.capitalize(Namer.word()) + 'ford');
            patterns.push(() => Namer.capitalize(Namer.word()) + ' Bridge');
        }

        if (bp.citadel) {
            patterns.push(() => Namer.capitalize(Namer.word()) + ' Castle');
            patterns.push(() => 'Castle ' + Namer.capitalize(Namer.word()));
        }

        if (bp.walls) {
            patterns.push(() => Namer.capitalize(Namer.word()) + 'burg');
            patterns.push(() => Namer.capitalize(Namer.word()) + 'fort');
        }

        return Random.pick(patterns)();
    }

    /**
     * Get a city name suffix
     * @returns {string}
     */
    static citySuffix() {
        const suffixes = [
            'ton', 'ville', 'burg', 'berg', 'ford', 'bridge', 'port',
            'wick', 'ham', 'stead', 'field', 'wood', 'dale', 'vale'
        ];
        return Random.pick(suffixes);
    }

    /**
     * Name all districts in a city
     *
     * @param {import('../model/City.js').City} city
     */
    static nameDistricts(city) {
        if (city.districts.length === 1) {
            city.districts[0].name = city.name;
            return;
        }

        for (const district of city.districts) {
            district.name = Namer.districtName(district);
        }
    }

    /**
     * Generate a district name
     *
     * @param {import('../model/District.js').District} district
     * @returns {string}
     */
    static districtName(district) {
        const patterns = [
            () => Namer.direction() + ' ' + Namer.districtNoun(district),
            () => 'The ' + Namer.capitalize(Namer.word()) + ' ' + Namer.districtNoun(district),
            () => Namer.capitalize(Namer.word()) + "'s " + Namer.districtNoun(district),
            () => Namer.given() + "'s " + Namer.districtNoun(district),
            () => 'Old ' + Namer.capitalize(Namer.word()),
            () => 'New ' + Namer.capitalize(Namer.word())
        ];

        return Random.pick(patterns)();
    }

    /**
     * Get a district noun based on size
     *
     * @param {import('../model/District.js').District} district
     * @returns {string}
     */
    static districtNoun(district) {
        const size = district.faces ? district.faces.length : 1;
        const noise = Math.floor(Random.next() * 3);
        const total = size + noise;

        if (total <= 2) return 'Quarter';
        if (total < 6) return 'Ward';
        if (total < 12) return 'District';
        return 'Town';
    }

    /**
     * Get a cardinal direction
     * @returns {string}
     */
    static direction() {
        const directions = ['North', 'South', 'East', 'West', 'Upper', 'Lower'];
        return Random.pick(directions);
    }

    /**
     * Generate a street name
     * @returns {string}
     */
    static streetName() {
        const patterns = [
            () => Namer.capitalize(Namer.word()) + ' Street',
            () => Namer.capitalize(Namer.word()) + ' Road',
            () => Namer.capitalize(Namer.word()) + ' Lane',
            () => Namer.capitalize(Namer.word()) + ' Way',
            () => Namer.capitalize(Namer.word()) + ' Avenue',
            () => Namer.given() + ' Street',
            () => Namer.given() + "'s Way",
            () => 'The ' + Namer.capitalize(Namer.word())
        ];

        return Random.pick(patterns)();
    }

    /**
     * Capitalize first letter
     *
     * @param {string} str
     * @returns {string}
     */
    static capitalize(str) {
        if (!str) return str;
        return str.charAt(0).toUpperCase() + str.slice(1);
    }

    /**
     * Capitalize all words
     *
     * @param {string} str
     * @returns {string}
     */
    static capitalizeAll(str) {
        return str.split(' ')
            .map(word => Namer.capitalize(word))
            .join(' ');
    }

    /**
     * Merge multi-word names if short enough
     *
     * @param {string} name - Name to potentially merge
     * @returns {string}
     */
    static merge(name) {
        const words = name.split(' ');
        if (words.length === 1) return name;

        let syllableCount = 0;
        for (const word of words) {
            syllableCount += Syllables.splitWord(word).length;
        }

        // Merge if total syllables <= 2
        if (syllableCount <= 2) {
            return words.join('');
        }

        return name;
    }
}
