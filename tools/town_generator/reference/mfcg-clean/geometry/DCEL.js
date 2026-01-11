/**
 * DCEL.js - Doubly Connected Edge List
 *
 * A topological data structure for representing planar subdivisions.
 * Used for Voronoi diagrams and city patch topology.
 */

import { Point } from '../core/Point.js';

/**
 * Vertex in the DCEL
 */
export class Vertex {
    /**
     * @param {Point} point - Position of vertex
     */
    constructor(point) {
        this.point = point;
        /** @type {HalfEdge[]} */
        this.edges = []; // All half-edges originating from this vertex
    }

    get x() { return this.point.x; }
    get y() { return this.point.y; }
}

/**
 * Half-edge in the DCEL
 */
export class HalfEdge {
    constructor() {
        /** @type {Vertex} */
        this.origin = null;
        /** @type {HalfEdge} */
        this.twin = null;
        /** @type {HalfEdge} */
        this.next = null;
        /** @type {HalfEdge} */
        this.prev = null;
        /** @type {Face} */
        this.face = null;
        /** @type {*} */
        this.data = null; // Custom data (e.g., edge type)
    }

    /**
     * Get destination vertex
     * @returns {Vertex}
     */
    get destination() {
        return this.next ? this.next.origin : null;
    }

    /**
     * Get length of edge
     * @returns {number}
     */
    get length() {
        if (!this.origin || !this.destination) return 0;
        return Point.distance(this.origin.point, this.destination.point);
    }
}

/**
 * Face in the DCEL
 */
export class Face {
    constructor() {
        /** @type {HalfEdge} */
        this.halfEdge = null; // One half-edge on the boundary
        /** @type {*} */
        this.data = null; // Custom data (e.g., Cell)
    }

    /**
     * Get polygon for this face
     * @returns {Point[]}
     */
    getPoly() {
        const poly = [];
        let edge = this.halfEdge;
        if (!edge) return poly;

        do {
            poly.push(edge.origin.point);
            edge = edge.next;
        } while (edge && edge !== this.halfEdge);

        return poly;
    }

    /**
     * Iterate over edges of this face
     * @yields {HalfEdge}
     */
    *edges() {
        let edge = this.halfEdge;
        if (!edge) return;

        do {
            yield edge;
            edge = edge.next;
        } while (edge && edge !== this.halfEdge);
    }

    /**
     * Get all vertices of this face
     * @returns {Vertex[]}
     */
    vertices() {
        const verts = [];
        for (const edge of this.edges()) {
            verts.push(edge.origin);
        }
        return verts;
    }
}

/**
 * Doubly Connected Edge List
 */
export class DCEL {
    /**
     * Create DCEL from polygons
     * @param {Point[][]} polygons - Array of polygon point arrays
     */
    constructor(polygons = []) {
        /** @type {Map<Point, Vertex>} */
        this.vertices = new Map();
        /** @type {HalfEdge[]} */
        this.edges = [];
        /** @type {Face[]} */
        this.faces = [];

        if (polygons.length > 0) {
            this.buildFromPolygons(polygons);
        }
    }

    /**
     * Build DCEL from array of polygons
     * @param {Point[][]} polygons
     */
    buildFromPolygons(polygons) {
        // Create vertices
        for (const poly of polygons) {
            for (const point of poly) {
                if (!this.vertices.has(point)) {
                    this.vertices.set(point, new Vertex(point));
                }
            }
        }

        // Map for finding twin edges
        const edgeMap = new Map();

        const edgeKey = (p1, p2) => {
            const k1 = `${p1.x.toFixed(6)},${p1.y.toFixed(6)}`;
            const k2 = `${p2.x.toFixed(6)},${p2.y.toFixed(6)}`;
            return `${k1}->${k2}`;
        };

        // Create faces and edges
        for (const poly of polygons) {
            const face = new Face();
            this.faces.push(face);

            const faceEdges = [];
            const n = poly.length;

            for (let i = 0; i < n; i++) {
                const p1 = poly[i];
                const p2 = poly[(i + 1) % n];

                const edge = new HalfEdge();
                edge.origin = this.vertices.get(p1);
                edge.origin.edges.push(edge);
                edge.face = face;

                faceEdges.push(edge);
                this.edges.push(edge);

                // Store for twin lookup
                edgeMap.set(edgeKey(p1, p2), edge);
            }

            // Link edges in cycle
            for (let i = 0; i < faceEdges.length; i++) {
                faceEdges[i].next = faceEdges[(i + 1) % faceEdges.length];
                faceEdges[i].prev = faceEdges[(i + faceEdges.length - 1) % faceEdges.length];
            }

            face.halfEdge = faceEdges[0];
            face.data = { shape: poly };
        }

        // Link twins
        for (const poly of polygons) {
            const n = poly.length;
            for (let i = 0; i < n; i++) {
                const p1 = poly[i];
                const p2 = poly[(i + 1) % n];

                const edge = edgeMap.get(edgeKey(p1, p2));
                const twin = edgeMap.get(edgeKey(p2, p1));

                if (edge && twin) {
                    edge.twin = twin;
                    twin.twin = edge;
                }
            }
        }
    }

    /**
     * Get vertex for point (or create if not exists)
     * @param {Point} point
     * @returns {Vertex}
     */
    getVertex(point) {
        if (!this.vertices.has(point)) {
            this.vertices.set(point, new Vertex(point));
        }
        return this.vertices.get(point);
    }

    /**
     * Find circumference edges of a set of faces
     * @param {HalfEdge|null} startEdge - Optional start edge
     * @param {Face[]} faces - Faces to find circumference of
     * @returns {HalfEdge[]}
     */
    static circumference(startEdge, faces) {
        const faceSet = new Set(faces);
        const boundaryEdges = [];

        for (const face of faces) {
            for (const edge of face.edges()) {
                // Edge is on boundary if twin is null or twin's face is not in set
                if (!edge.twin || !faceSet.has(edge.twin.face)) {
                    boundaryEdges.push(edge);
                }
            }
        }

        if (boundaryEdges.length === 0) return [];

        // Sort edges into a cycle
        const result = [];
        let current = startEdge && boundaryEdges.includes(startEdge)
            ? startEdge
            : boundaryEdges[0];

        const visited = new Set();
        while (current && !visited.has(current)) {
            visited.add(current);
            result.push(current);

            // Find next boundary edge
            let next = current.next;
            while (next && !boundaryEdges.includes(next)) {
                if (next.twin) {
                    next = next.twin.next;
                } else {
                    break;
                }
            }
            current = boundaryEdges.includes(next) ? next : null;
        }

        return result;
    }

    /**
     * Convert vertex array to edge chain
     * @param {Vertex[]} vertices
     * @returns {HalfEdge[]}
     */
    vertices2chain(vertices) {
        const chain = [];
        for (let i = 0; i < vertices.length - 1; i++) {
            const v1 = vertices[i];
            const v2 = vertices[i + 1];

            // Find edge from v1 to v2
            for (const edge of v1.edges) {
                if (edge.destination === v2) {
                    chain.push(edge);
                    break;
                }
            }
        }
        return chain;
    }

    /**
     * Split faces into connected components
     * @param {Face[]} faces
     * @returns {Face[][]}
     */
    static split(faces) {
        const faceSet = new Set(faces);
        const visited = new Set();
        const components = [];

        for (const face of faces) {
            if (visited.has(face)) continue;

            const component = [];
            const queue = [face];

            while (queue.length > 0) {
                const current = queue.shift();
                if (visited.has(current)) continue;
                visited.add(current);
                component.push(current);

                // Add adjacent faces in the set
                for (const edge of current.edges()) {
                    if (edge.twin && faceSet.has(edge.twin.face) && !visited.has(edge.twin.face)) {
                        queue.push(edge.twin.face);
                    }
                }
            }

            components.push(component);
        }

        return components;
    }

    /**
     * Collapse an edge, merging its endpoints
     * @param {HalfEdge} edge
     * @returns {{vertex: Vertex, edges: HalfEdge[]}}
     */
    collapseEdge(edge) {
        const v1 = edge.origin;
        const v2 = edge.destination;
        const midpoint = Point.midpoint(v1.point, v2.point);

        // Update v1's position to midpoint
        v1.point.x = midpoint.x;
        v1.point.y = midpoint.y;

        // Move all v2's edges to v1
        for (const e of v2.edges) {
            e.origin = v1;
            v1.edges.push(e);
        }
        v2.edges = [];

        // Remove the collapsed edge from the cycle
        if (edge.prev) edge.prev.next = edge.next;
        if (edge.next) edge.next.prev = edge.prev;

        if (edge.twin) {
            if (edge.twin.prev) edge.twin.prev.next = edge.twin.next;
            if (edge.twin.next) edge.twin.next.prev = edge.twin.prev;
        }

        // Update face pointers if needed
        if (edge.face.halfEdge === edge) {
            edge.face.halfEdge = edge.next !== edge ? edge.next : null;
        }
        if (edge.twin && edge.twin.face.halfEdge === edge.twin) {
            edge.twin.face.halfEdge = edge.twin.next !== edge.twin ? edge.twin.next : null;
        }

        // Remove from edges list
        const idx = this.edges.indexOf(edge);
        if (idx >= 0) this.edges.splice(idx, 1);
        if (edge.twin) {
            const twinIdx = this.edges.indexOf(edge.twin);
            if (twinIdx >= 0) this.edges.splice(twinIdx, 1);
        }

        // Remove v2 from vertices
        this.vertices.delete(v2.point);

        // Return affected edges for face update
        const affectedEdges = [];
        if (edge.face) affectedEdges.push(edge.next || edge.prev);
        if (edge.twin && edge.twin.face) affectedEdges.push(edge.twin.next || edge.twin.prev);

        return { vertex: v1, edges: affectedEdges };
    }

    /**
     * Split an edge at its midpoint
     * @param {HalfEdge} edge
     * @returns {Vertex}
     */
    splitEdge(edge) {
        const midpoint = Point.midpoint(edge.origin.point, edge.destination.point);
        const newVertex = new Vertex(midpoint);
        this.vertices.set(midpoint, newVertex);

        const newEdge = new HalfEdge();
        newEdge.origin = newVertex;
        newEdge.face = edge.face;
        newEdge.next = edge.next;
        newEdge.prev = edge;
        if (edge.next) edge.next.prev = newEdge;
        edge.next = newEdge;

        newVertex.edges.push(newEdge);
        this.edges.push(newEdge);

        if (edge.twin) {
            const newTwin = new HalfEdge();
            newTwin.origin = newVertex;
            newTwin.face = edge.twin.face;
            newTwin.twin = edge;
            edge.twin.twin = newEdge;
            newEdge.twin = edge.twin;
            edge.twin = newTwin;

            newTwin.next = edge.twin.next;
            newTwin.prev = edge.twin;
            if (edge.twin.next) edge.twin.next.prev = newTwin;
            edge.twin.next = newTwin;

            newVertex.edges.push(newTwin);
            this.edges.push(newTwin);
        }

        return newVertex;
    }
}

/**
 * Edge chain utilities
 */
export class EdgeChain {
    /**
     * Convert edge chain to polygon
     * @param {HalfEdge[]} chain
     * @returns {Point[]}
     */
    static toPoly(chain) {
        return chain.map(edge => edge.origin.point);
    }

    /**
     * Convert edge chain to polyline (includes last point)
     * @param {HalfEdge[]} chain
     * @returns {Point[]}
     */
    static toPolyline(chain) {
        if (chain.length === 0) return [];
        const points = chain.map(edge => edge.origin.point);
        if (chain[chain.length - 1].destination) {
            points.push(chain[chain.length - 1].destination.point);
        }
        return points;
    }

    /**
     * Get all vertices in edge chain
     * @param {HalfEdge[]} chain
     * @returns {Vertex[]}
     */
    static vertices(chain) {
        return chain.map(edge => edge.origin);
    }

    /**
     * Assign data to all edges in chain
     * @param {HalfEdge[]} chain
     * @param {*} data
     * @param {boolean} [overwrite=true]
     */
    static assignData(chain, data, overwrite = true) {
        for (const edge of chain) {
            if (overwrite || edge.data === null) {
                edge.data = data;
            }
        }
    }

    /**
     * Find edge by origin vertex
     * @param {HalfEdge[]} chain
     * @param {Vertex} vertex
     * @returns {HalfEdge|null}
     */
    static edgeByOrigin(chain, vertex) {
        for (const edge of chain) {
            if (edge.origin === vertex) return edge;
        }
        return null;
    }
}
