/**
 * City.js - Main city generator
 *
 * The core class that generates a complete medieval city.
 */

import { Point } from '../core/Point.js';
import { Random } from '../core/Random.js';
import { DCEL, EdgeChain } from '../geometry/DCEL.js';
import { Voronoi } from '../geometry/Voronoi.js';
import { PolygonUtils } from '../geometry/PolygonUtils.js';
import { PolygonSmoother } from '../geometry/Chaikin.js';
import { Noise } from '../utils/Noise.js';
import { Cell } from './Cell.js';
import { Blueprint } from './Blueprint.js';

/**
 * Edge types for road/wall classification
 */
export const EdgeType = {
    NONE: 0,
    COAST: 1,
    ROAD: 2,
    WALL: 3,
    CANAL: 4,
    HORIZON: 5
};

/**
 * City generator class
 */
export class City {
    /** @type {City|null} */
    static instance = null;

    /**
     * Create a city
     * @param {Blueprint} blueprint
     */
    constructor(blueprint) {
        /** @type {Blueprint} */
        this.bp = blueprint;

        /** @type {string} */
        this.name = '';

        /** @type {number} */
        this.nPatches = blueprint.size;

        // Topology
        /** @type {DCEL} */
        this.dcel = null;

        /** @type {Cell[]} */
        this.cells = [];

        /** @type {Cell[]} */
        this.inner = [];

        /** @type {Point} */
        this.center = new Point(0, 0);

        // Special cells
        /** @type {Cell|null} */
        this.plaza = null;

        /** @type {Cell|null} */
        this.citadel = null;

        // Domains
        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.horizonE = [];

        /** @type {Point[]} */
        this.horizon = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.earthEdgeE = [];

        /** @type {Point[]} */
        this.earthEdge = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.waterEdgeE = [];

        /** @type {Point[]} */
        this.waterEdge = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[]} */
        this.shoreE = [];

        /** @type {Point[]} */
        this.shore = [];

        /** @type {Point[]} */
        this.ocean = null;

        // Infrastructure
        /** @type {import('../geometry/DCEL.js').HalfEdge[][]} */
        this.streets = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[][]} */
        this.roads = [];

        /** @type {import('../geometry/DCEL.js').HalfEdge[][]} */
        this.arteries = [];

        /** @type {import('./CurtainWall.js').CurtainWall[]} */
        this.walls = [];

        /** @type {import('./CurtainWall.js').CurtainWall|null} */
        this.wall = null;

        /** @type {import('./CurtainWall.js').CurtainWall|null} */
        this.border = null;

        /** @type {import('../geometry/DCEL.js').Vertex[]} */
        this.gates = [];

        /** @type {import('./Canal.js').Canal[]} */
        this.canals = [];

        /** @type {import('./District.js').District[]} */
        this.districts = [];

        /** @type {import('./Landmark.js').Landmark[]} */
        this.landmarks = [];

        // Bounds
        /** @type {{x: number, y: number, width: number, height: number}} */
        this.bounds = { x: 0, y: 0, width: 0, height: 0 };

        /** @type {number} */
        this.north = 0;

        /** @type {number} */
        this.maxDocks = 3;

        // Set as singleton
        City.instance = this;
    }

    // Feature flags based on blueprint

    get plazaNeeded() {
        return this.bp.plaza;
    }

    get citadelNeeded() {
        return this.bp.citadel;
    }

    get stadtburgNeeded() {
        return this.bp.inner && this.bp.citadel;
    }

    get wallsNeeded() {
        return this.bp.walls;
    }

    get templeNeeded() {
        return this.bp.temple;
    }

    get coastNeeded() {
        return this.bp.coast;
    }

    get riverNeeded() {
        return this.bp.river && this.coastNeeded;
    }

    get shantyNeeded() {
        return this.bp.shanty;
    }

    /**
     * Build the city
     */
    build() {
        Random.seed = this.bp.seed;

        this.streets = [];
        this.roads = [];
        this.walls = [];
        this.landmarks = [];
        this.north = 0;

        console.log('Building patches...');
        this.buildPatches();

        console.log('Optimizing junctions...');
        this.optimizeJunctions();

        console.log('Building domains...');
        this.buildDomains();

        console.log('Building walls...');
        this.buildWalls();

        console.log('Building streets...');
        this.buildStreets();

        console.log('Building canals...');
        this.buildCanals();

        console.log('Creating wards...');
        this.createWards();

        console.log('Building city towers...');
        this.buildCityTowers();

        console.log('Building geometry...');
        this.buildGeometry();

        this.updateDimensions();
    }

    /**
     * Build the Voronoi patches that form the city base
     */
    buildPatches() {
        const sites = [new Point(0, 0)];
        let maxRadius = 0;

        // Generate spiral of points
        const startAngle = Random.next() * 2 * Math.PI;

        for (let i = 1; i < 8 * this.nPatches; i++) {
            const r = 10 + i * (2 + Random.next());
            const a = startAngle + 5 * Math.sqrt(i);
            sites.push(Point.polar(r, a));
            if (r > maxRadius) maxRadius = r;
        }

        // Adjust for plaza if needed
        if (this.plazaNeeded) {
            Random.save();
            const plazaSize = 8 + Random.next() * 8;
            const plazaRatio = plazaSize * (1 + Random.next());
            maxRadius = Math.max(maxRadius, plazaRatio);

            sites[1] = Point.polar(plazaSize, startAngle);
            sites[2] = Point.polar(plazaRatio, startAngle + Math.PI / 2);
            sites[3] = Point.polar(plazaSize, startAngle + Math.PI);
            sites[4] = Point.polar(plazaRatio, startAngle + 3 * Math.PI / 2);
            Random.restore();
        }

        // Add boundary points
        const boundary = PolygonUtils.regular(6, 2 * maxRadius);
        sites.push(...boundary);

        // Generate Voronoi diagram
        const voronoi = new Voronoi(sites);
        const cellPolys = voronoi.getVoronoi();

        // Filter out cells that extend too far
        const validPolys = [];
        for (const [site, poly] of cellPolys) {
            let valid = true;
            for (const p of poly) {
                if (Point.distance(p, new Point(0, 0)) > maxRadius * 1.5) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                validPolys.push(poly);
            }
        }

        // Build DCEL from polygons
        this.dcel = new DCEL(validPolys);
        this.cells = [];
        this.inner = [];

        // Create cells from faces, sorted by distance from center
        const cellsWithDist = [];
        for (const face of this.dcel.faces) {
            const cell = new Cell(face);
            const centroid = PolygonUtils.centroid(cell.shape);
            cellsWithDist.push({
                cell,
                dist: centroid.x * centroid.x + centroid.y * centroid.y
            });
        }
        cellsWithDist.sort((a, b) => a.dist - b.dist);
        this.cells = cellsWithDist.map(c => c.cell);

        // Mark coastal cells if needed
        if (this.coastNeeded) {
            this.markCoastalCells(maxRadius);
        }

        // Mark inner city cells
        let innerCount = 0;
        for (const cell of this.cells) {
            if (!cell.waterbody && innerCount < this.nPatches) {
                cell.withinCity = true;
                cell.withinWalls = this.wallsNeeded;
                this.inner.push(cell);
                innerCount++;
            }
        }

        // Find center
        if (this.inner.length > 0) {
            let minDist = Infinity;
            for (const p of this.inner[0].shape) {
                const dist = p.x * p.x + p.y * p.y;
                if (dist < minDist) {
                    minDist = dist;
                    this.center = p;
                }
            }
        }

        // Set up plaza
        if (this.plazaNeeded && this.inner.length > 0) {
            this.plaza = this.inner[0];
            // Market ward will be created later
        }

        // Set up citadel
        if (this.citadelNeeded && this.inner.length > 0) {
            if (this.stadtburgNeeded) {
                // Find a cell on the edge for urban castle
                const candidates = this.inner.filter(cell => {
                    if (cell === this.plaza) return false;
                    return this.isEdgeCell(cell);
                });

                this.citadel = candidates.length > 0
                    ? Random.pick(candidates)
                    : this.inner[this.inner.length - 1];
            } else {
                this.citadel = this.inner[this.inner.length - 1];
            }

            this.citadel.withinCity = true;
            this.citadel.withinWalls = true;

            const citadelIdx = this.inner.indexOf(this.citadel);
            if (citadelIdx >= 0) {
                this.inner.splice(citadelIdx, 1);
            }
        }
    }

    /**
     * Mark cells as water based on coastline
     * @param {number} maxRadius
     */
    markCoastalCells(maxRadius) {
        Random.save();

        const noise = Noise.fractal(6);
        const coastOffset = 20 + Random.next() * 40;
        const coastCenter = 0.3 * maxRadius * Random.gaussian();
        const coastRadius = maxRadius * (0.2 + Math.abs(Random.gaussian()));

        if (this.bp.coastDir === null) {
            this.bp.coastDir = Math.floor(Random.next() * 20) / 10;
        }

        Random.restore();

        const coastAngle = this.bp.coastDir * Math.PI;
        const cosA = Math.cos(coastAngle);
        const sinA = Math.sin(coastAngle);

        const oceanCenter = new Point(coastRadius + coastOffset, coastCenter);

        for (const cell of this.cells) {
            const centroid = PolygonUtils.centroid(cell.shape);

            // Rotate centroid to coast direction
            const rotated = new Point(
                centroid.x * cosA - centroid.y * sinA,
                centroid.y * cosA + centroid.x * sinA
            );

            let distToOcean = Point.distance(oceanCenter, rotated) - coastRadius;

            // Add noise
            if (rotated.x > oceanCenter.x) {
                distToOcean = Math.min(distToOcean, Math.abs(rotated.y - coastCenter) - coastRadius);
            }

            const noiseValue = noise.get(
                (rotated.x + maxRadius) / (2 * maxRadius),
                (rotated.y + maxRadius) / (2 * maxRadius)
            ) * coastRadius * Math.sqrt(rotated.length / maxRadius);

            if (distToOcean + noiseValue < 0) {
                cell.waterbody = true;
            }
        }
    }

    /**
     * Check if cell is on the edge of the city
     * @param {Cell} cell
     * @returns {boolean}
     */
    isEdgeCell(cell) {
        for (const neighbor of cell.getNeighbors()) {
            if (!neighbor.waterbody && !neighbor.withinCity) {
                return true;
            }
        }
        return false;
    }

    /**
     * Optimize junctions by collapsing short edges
     */
    optimizeJunctions() {
        const minEdgeLength = 3 * 2.5; // 3 * tower radius

        let changed = true;
        while (changed) {
            changed = false;

            for (const face of this.dcel.faces) {
                const shape = face.data.shape;
                if (shape.length <= 4) continue;

                const avgEdge = PolygonUtils.perimeter(shape) / shape.length;
                const threshold = Math.max(minEdgeLength, avgEdge / 3);

                for (const edge of face.edges()) {
                    if (!edge.twin) continue;
                    if (edge.twin.face.data.shape.length <= 4) continue;

                    const edgeLen = edge.length;
                    if (edgeLen < threshold) {
                        const result = this.dcel.collapseEdge(edge);

                        // Update face shapes
                        for (const affectedEdge of result.edges) {
                            if (affectedEdge && affectedEdge.face) {
                                affectedEdge.face.data.shape = affectedEdge.face.getPoly();
                            }
                        }

                        changed = true;
                        break;
                    }
                }

                if (changed) break;
            }
        }

        // Recalculate center if needed
        if (!this.dcel.vertices.has(this.center)) {
            let minDist = Infinity;
            for (const p of this.inner[0].shape) {
                const dist = p.x * p.x + p.y * p.y;
                if (dist < minDist) {
                    minDist = dist;
                    this.center = p;
                }
            }
        }
    }

    /**
     * Build domain boundaries (horizon, earth edge, water edge)
     */
    buildDomains() {
        // Find horizon (outer boundary)
        const boundaryEdge = this.dcel.edges.find(e => !e.twin);
        if (boundaryEdge) {
            this.horizonE = DCEL.circumference(boundaryEdge, this.dcel.faces);
            EdgeChain.assignData(this.horizonE, EdgeType.HORIZON);
            this.horizon = EdgeChain.toPoly(this.horizonE);
        }

        if (this.coastNeeded) {
            // Split faces into water and land
            const waterFaces = [];
            const landFaces = [];

            for (const face of this.dcel.faces) {
                if (face.data.waterbody) {
                    waterFaces.push(face);
                } else {
                    landFaces.push(face);
                }
            }

            // Find largest connected components
            const landGroups = DCEL.split(landFaces);
            const waterGroups = DCEL.split(waterFaces);

            const mainLand = landGroups.reduce((a, b) => a.length > b.length ? a : b, []);
            const mainWater = waterGroups.reduce((a, b) => a.length > b.length ? a : b, []);

            this.earthEdgeE = DCEL.circumference(null, mainLand);
            this.earthEdge = EdgeChain.toPoly(this.earthEdgeE);

            this.waterEdgeE = DCEL.circumference(null, mainWater);
            this.waterEdge = EdgeChain.toPoly(this.waterEdgeE);

            // Smooth water edge
            const smoothed = PolygonSmoother.smooth(
                this.waterEdge, null,
                Math.floor(1 + Random.next() * 3)
            );
            PolygonUtils.set(this.waterEdge, smoothed);

            // Find shore (part of earth edge that touches water)
            this.shoreE = [];
            this.shore = [];

            let onShore = false;
            for (const edge of this.earthEdgeE) {
                if (!edge.twin) {
                    onShore = false;
                } else {
                    onShore = true;
                }

                if (onShore) {
                    this.shoreE.push(edge);
                    this.shore.push(edge.origin.point);
                }
            }

            EdgeChain.assignData(this.shoreE, EdgeType.COAST);
        } else {
            this.earthEdgeE = this.horizonE;
            this.earthEdge = this.horizon;
            this.waterEdgeE = [];
            this.waterEdge = [];
            this.shoreE = [];
            this.shore = [];
        }
    }

    /**
     * Build city walls
     */
    buildWalls() {
        Random.save();

        const excludePoints = this.waterEdge.slice();
        if (this.citadel) {
            excludePoints.push(...this.citadel.shape);
        }

        // Create border wall (may be virtual if walls not needed)
        // Note: CurtainWall class will be defined separately
        // this.border = new CurtainWall(this.wallsNeeded, this, this.inner, excludePoints);

        if (this.wallsNeeded) {
            // this.wall = this.border;
            // this.walls.push(this.wall);
        }

        // this.gates = this.border.gates.slice();

        // Create castle wall if citadel exists
        if (this.citadel) {
            // const castle = new Castle(this, this.citadel);
            // this.walls.push(castle.wall);
            // this.gates.push(...castle.wall.gates);
        }

        Random.restore();
    }

    /**
     * Build street network
     */
    buildStreets() {
        // Build topology graph for pathfinding
        // Connect gates to center, build roads to exterior
        // This is simplified - full implementation uses Topology class
    }

    /**
     * Build canals/rivers
     */
    buildCanals() {
        Random.save();

        if (this.riverNeeded) {
            // Create river through city
            // this.canals = [Canal.createRiver(this)];
        } else {
            this.canals = [];
        }

        Random.restore();
    }

    /**
     * Create ward types for each cell
     */
    createWards() {
        // Create wards for inner cells
        // Parks, cathedral, alleys, etc.
        // This requires Ward subclasses

        // Create farms and wilderness for outer cells
        this.buildFarms();
    }

    /**
     * Build city wall towers
     */
    buildCityTowers() {
        if (!this.wall) return;

        // Determine which wall segments should have towers
        // Build towers at corners and gates
    }

    /**
     * Build final geometry
     */
    buildGeometry() {
        // Update canals
        for (const canal of this.canals) {
            // canal.updateState();
        }

        // Generate city name
        this.name = this.bp.name || this.generateName();

        // Build district structure
        // this.districts = new DistrictBuilder(this).build();

        // Create geometry for each ward
        for (const cell of this.cells) {
            if (cell.ward) {
                // cell.ward.createGeometry();
            }
        }
    }

    /**
     * Build farms in outer area
     */
    buildFarms() {
        // Create farms around the city
        // Pattern based on noise and distance from center
    }

    /**
     * Update city bounding box
     */
    updateDimensions() {
        let minX = Infinity, maxX = -Infinity;
        let minY = Infinity, maxY = -Infinity;

        for (const cell of this.cells) {
            for (const p of cell.shape) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
        }

        this.bounds = {
            x: minX,
            y: minY,
            width: maxX - minX,
            height: maxY - minY
        };
    }

    /**
     * Generate a city name
     * @returns {string}
     */
    generateName() {
        // Placeholder - full implementation would use Namer class
        const prefixes = ['North', 'South', 'East', 'West', 'New', 'Old', 'Great', 'Little'];
        const roots = ['burg', 'ton', 'ford', 'haven', 'port', 'bridge', 'field', 'vale'];
        const names = ['wood', 'brook', 'stone', 'dale', 'hill', 'water', 'river', 'castle'];

        if (Random.bool(0.3)) {
            return Random.pick(prefixes) + Random.pick(names) + Random.pick(roots);
        } else {
            return Random.pick(names).charAt(0).toUpperCase() + Random.pick(names).slice(1) + Random.pick(roots);
        }
    }

    /**
     * Get cells that share a vertex
     * @param {import('../geometry/DCEL.js').Vertex} vertex
     * @returns {Cell[]}
     */
    cellsByVertex(vertex) {
        const cells = [];
        for (const edge of vertex.edges) {
            if (edge.face.data instanceof Cell) {
                cells.push(edge.face.data);
            }
        }
        return cells;
    }

    /**
     * Get cell containing a point
     * @param {Point} point
     * @returns {Cell|null}
     */
    getCell(point) {
        for (const cell of this.cells) {
            if (PolygonUtils.containsPoint(cell.shape, point)) {
                return cell;
            }
        }
        return null;
    }

    /**
     * Count buildings in city
     * @returns {number}
     */
    countBuildings() {
        let count = 0;
        for (const district of this.districts) {
            for (const group of district.groups || []) {
                for (const block of group.blocks || []) {
                    count += block.lots?.length || 0;
                }
            }
        }
        return count;
    }
}
