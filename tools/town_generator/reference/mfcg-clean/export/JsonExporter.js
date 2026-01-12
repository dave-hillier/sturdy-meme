/**
 * JsonExporter.js - GeoJSON export for city data
 *
 * Exports city data to GeoJSON format for use with mapping tools.
 */

import { Point } from '../core/Point.js';
import { EdgeChain } from '../geometry/DCEL.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';

/**
 * GeoJSON helper utilities
 */
export class GeoJSON {
    /** @type {number} Scale factor for coordinates */
    static SCALE = 4;

    /**
     * Create a Point geometry
     * @param {string|null} id
     * @param {Point} point
     * @returns {object}
     */
    static point(id, point) {
        return {
            type: 'Point',
            id,
            coordinates: [point.x * GeoJSON.SCALE, point.y * GeoJSON.SCALE]
        };
    }

    /**
     * Create a MultiPoint geometry
     * @param {string|null} id
     * @param {Point[]} points
     * @returns {object}
     */
    static multiPoint(id, points) {
        return {
            type: 'MultiPoint',
            id,
            coordinates: points.map(p => [p.x * GeoJSON.SCALE, p.y * GeoJSON.SCALE])
        };
    }

    /**
     * Create a LineString geometry
     * @param {string|null} id
     * @param {Point[]} points
     * @returns {object}
     */
    static lineString(id, points) {
        return {
            type: 'LineString',
            id,
            coordinates: points.map(p => [p.x * GeoJSON.SCALE, p.y * GeoJSON.SCALE]),
            props: null
        };
    }

    /**
     * Create a Polygon geometry
     * @param {string|null} id
     * @param {Point[]} ring
     * @returns {object}
     */
    static polygon(id, ring) {
        const coords = ring.map(p => [p.x * GeoJSON.SCALE, p.y * GeoJSON.SCALE]);
        // Close the ring
        if (coords.length > 0) {
            coords.push(coords[0]);
        }
        return {
            type: 'Polygon',
            id,
            coordinates: [coords],
            props: null
        };
    }

    /**
     * Create a MultiPolygon geometry
     * @param {string|null} id
     * @param {Point[][]} polygons
     * @returns {object}
     */
    static multiPolygon(id, polygons) {
        const coordinates = polygons.map(ring => {
            const coords = ring.map(p => [p.x * GeoJSON.SCALE, p.y * GeoJSON.SCALE]);
            if (coords.length > 0) {
                coords.push(coords[0]);
            }
            return [coords];
        });

        return {
            type: 'MultiPolygon',
            id,
            coordinates
        };
    }

    /**
     * Create a Feature
     * @param {string|null} id
     * @param {object} properties
     * @returns {object}
     */
    static feature(id, properties) {
        return {
            type: 'Feature',
            id,
            properties: properties || {},
            geometry: null
        };
    }

    /**
     * Create a GeometryCollection
     * @param {string|null} id
     * @param {object[]} geometries
     * @returns {object}
     */
    static geometryCollection(id, geometries) {
        return {
            type: 'GeometryCollection',
            id,
            geometries
        };
    }

    /**
     * Create a FeatureCollection
     * @param {object[]} features
     * @returns {object}
     */
    static featureCollection(features) {
        return {
            type: 'FeatureCollection',
            features,
            stringify() {
                return JSON.stringify(this, null, 2);
            }
        };
    }
}

/**
 * JSON/GeoJSON exporter for city data
 */
export class JsonExporter {
    /**
     * Export city to GeoJSON
     * @param {import('../model/City.js').City} city
     * @returns {object}
     */
    static export(city) {
        GeoJSON.SCALE = 4;

        const buildings = [];
        const prisms = [];  // Monuments
        const squares = []; // Market squares
        const greens = [];  // Parks
        const fields = [];  // Farm plots
        const piers = new Map();
        const trees = [];

        // Collect geometry from cells
        for (const cell of city.cells) {
            if (!cell.ward) continue;

            const ward = cell.ward;
            const wardType = ward.constructor.name;

            switch (wardType) {
                case 'Alleys':
                    if (ward.group && ward.group.blocks) {
                        for (const block of ward.group.blocks) {
                            if (block.lots) {
                                buildings.push(...block.lots);
                            }
                        }
                    }
                    break;

                case 'Castle':
                    if (ward.building) {
                        buildings.push(ward.building);
                    }
                    break;

                case 'Cathedral':
                    if (ward.building) {
                        if (Array.isArray(ward.building)) {
                            buildings.push(...ward.building);
                        } else {
                            buildings.push(ward.building);
                        }
                    }
                    break;

                case 'Farm':
                    if (ward.buildings) {
                        buildings.push(...ward.buildings);
                    }
                    if (ward.subPlots) {
                        fields.push(...ward.subPlots);
                    }
                    break;

                case 'Harbour':
                    if (ward.piers) {
                        for (const pier of ward.piers) {
                            piers.set(pier, 1.2);
                        }
                    }
                    break;

                case 'Market':
                    if (ward.space) {
                        squares.push(ward.space);
                    }
                    if (ward.monument) {
                        prisms.push(ward.monument);
                    }
                    break;

                case 'Park':
                    if (ward.green) {
                        greens.push(ward.green);
                    }
                    break;
            }

            // Collect trees
            if (ward.spawnTrees) {
                const wardTrees = ward.spawnTrees();
                if (wardTrees) {
                    trees.push(...wardTrees);
                }
            }
        }

        // Build district geometries
        const districtGeoms = [];
        for (const district of city.districts) {
            const poly = GeoJSON.polygon(null, EdgeChain.toPoly(district.border));
            poly.props = { name: district.name };
            districtGeoms.push(poly);
        }

        // Build road geometries
        const roadGeoms = [];
        for (const artery of city.arteries) {
            const line = GeoJSON.lineString(null, EdgeChain.toPolyline(artery));
            line.props = { width: 2 * GeoJSON.SCALE };
            roadGeoms.push(line);
        }

        // Build wall geometries
        const wallGeoms = [];
        for (const wall of city.walls) {
            if (wall.shape) {
                const poly = GeoJSON.polygon(null, wall.shape);
                poly.props = { width: wall.thickness * GeoJSON.SCALE };
                wallGeoms.push(poly);
            }
        }

        // Build river geometries
        const riverGeoms = [];
        for (const canal of city.canals) {
            if (canal.course) {
                const line = GeoJSON.lineString(null, EdgeChain.toPolyline(canal.course));
                line.props = { width: canal.width * GeoJSON.SCALE };
                riverGeoms.push(line);
            }
        }

        // Build pier geometries
        const pierGeoms = [];
        for (const [pier, width] of piers) {
            const line = GeoJSON.lineString(null, pier);
            line.props = { width: width * GeoJSON.SCALE };
            pierGeoms.push(line);
        }

        // Create feature collection
        const features = [
            // Metadata
            GeoJSON.feature('values', {
                roadWidth: 2 * GeoJSON.SCALE,
                towerRadius: 2.5 * GeoJSON.SCALE,
                wallThickness: 1.2 * GeoJSON.SCALE,
                generator: 'mfcg-clean',
                version: '1.0.0',
                cityName: city.name,
                riverWidth: city.canals.length > 0 ? city.canals[0].width * GeoJSON.SCALE : 0
            }),

            // Land boundary
            city.earthEdge.length > 0 ? GeoJSON.polygon('earth', city.earthEdge) : null,

            // Infrastructure
            GeoJSON.geometryCollection('districts', districtGeoms),
            GeoJSON.geometryCollection('roads', roadGeoms),
            GeoJSON.geometryCollection('walls', wallGeoms),
            GeoJSON.geometryCollection('rivers', riverGeoms),
            GeoJSON.geometryCollection('planks', pierGeoms),

            // Buildings and features
            GeoJSON.multiPolygon('buildings', buildings),
            GeoJSON.multiPolygon('prisms', prisms),
            GeoJSON.multiPolygon('squares', squares),
            GeoJSON.multiPolygon('greens', greens),
            GeoJSON.multiPolygon('fields', fields),

            // Trees
            GeoJSON.multiPoint('trees', trees)
        ].filter(f => f !== null);

        // Add water if present
        if (city.waterEdge && city.waterEdge.length > 0) {
            features.push(GeoJSON.multiPolygon('water', [city.waterEdge]));
        }

        return GeoJSON.featureCollection(features);
    }

    /**
     * Export city to JSON string
     * @param {import('../model/City.js').City} city
     * @returns {string}
     */
    static exportString(city) {
        return JsonExporter.export(city).stringify();
    }
}
