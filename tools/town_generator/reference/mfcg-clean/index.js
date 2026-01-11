/**
 * MFCG Clean - Medieval Fantasy City Generator
 *
 * A clean refactoring of the original MFCG JavaScript code.
 * Converted from the original Haxe-compiled JS to modern ES6 modules.
 *
 * Original: https://watabou.itch.io/medieval-fantasy-city-generator
 * by Oleg Dolya (watabou)
 */

// Core utilities
export { Random } from './core/Random.js';
export { Point } from './core/Point.js';
export { MathUtils } from './core/MathUtils.js';

// Geometry
export { Circle } from './geometry/Circle.js';
export { Segment } from './geometry/Segment.js';
export { GeomUtils } from './geometry/GeomUtils.js';
export { PolygonUtils } from './geometry/PolygonUtils.js';
export { DCEL, Vertex, HalfEdge, Face, EdgeChain } from './geometry/DCEL.js';
export { Voronoi, Triangulation } from './geometry/Voronoi.js';
export { Chaikin, PolygonSmoother } from './geometry/Chaikin.js';

// Utilities
export { Noise, Perlin } from './utils/Noise.js';

// Model
export { Cell } from './model/Cell.js';
export { Blueprint } from './model/Blueprint.js';
export { Building } from './model/Building.js';
export { City, EdgeType } from './model/City.js';
export { CurtainWall } from './model/CurtainWall.js';
export { Canal } from './model/Canal.js';
export { District, DistrictBuilder, AlleyConfig } from './model/District.js';

// Wards
export { Ward } from './wards/Ward.js';
export { Alleys } from './wards/Alleys.js';
export { Block } from './wards/Block.js';
export { Castle } from './wards/Castle.js';
export { Cathedral } from './wards/Cathedral.js';
export { Farm } from './wards/Farm.js';
export { Harbour } from './wards/Harbour.js';
export { Market } from './wards/Market.js';
export { Park } from './wards/Park.js';
export { WardGroup } from './wards/WardGroup.js';
export { Wilderness } from './wards/Wilderness.js';

/**
 * Generate a city with the given parameters
 * @param {Object} options - Generation options
 * @param {number} [options.size=15] - City size (number of patches, 6-60)
 * @param {number} [options.seed] - Random seed (auto-generated if not provided)
 * @param {boolean} [options.walls=true] - Generate city walls
 * @param {boolean} [options.citadel=true] - Generate citadel/castle
 * @param {boolean} [options.plaza=true] - Generate central plaza
 * @param {boolean} [options.temple=true] - Generate temple/cathedral
 * @param {boolean} [options.coast=true] - Generate coastline
 * @param {boolean} [options.river=true] - Generate river
 * @param {boolean} [options.shanty=false] - Generate shanty towns
 * @param {string} [options.name] - City name (auto-generated if not provided)
 * @returns {City}
 */
export function generateCity(options = {}) {
    const {
        size = 15,
        seed = Math.floor(Math.random() * 2147483647),
        walls,
        citadel,
        plaza,
        temple,
        coast,
        river,
        shanty,
        name
    } = options;

    // Create blueprint
    const blueprint = Blueprint.create(size, seed);

    // Override random settings if specified
    if (walls !== undefined) blueprint.walls = walls;
    if (citadel !== undefined) blueprint.citadel = citadel;
    if (plaza !== undefined) blueprint.plaza = plaza;
    if (temple !== undefined) blueprint.temple = temple;
    if (coast !== undefined) blueprint.coast = coast;
    if (river !== undefined) blueprint.river = river;
    if (shanty !== undefined) blueprint.shanty = shanty;
    if (name) blueprint.name = name;

    // Generate city
    const city = new City(blueprint);
    city.build();

    return city;
}

// Re-export for convenience
import { Blueprint } from './model/Blueprint.js';
import { City } from './model/City.js';
