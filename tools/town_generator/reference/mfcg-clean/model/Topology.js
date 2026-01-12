/**
 * Topology.js - Road network pathfinding
 *
 * Builds a graph from DCEL edges for pathfinding through the city.
 */

import { Point } from '../core/Point.js';
import { Graph, Node } from '../geometry/Graph.js';
import { EdgeChain } from '../geometry/DCEL.js';

/**
 * Topology - road network graph for pathfinding
 */
export class Topology {
    /**
     * Create topology from city cells
     * @param {import('./Cell.js').Cell[]} cells - City cells to include
     */
    constructor(cells) {
        /** @type {Graph} */
        this.graph = new Graph();

        /** @type {Map<import('../geometry/DCEL.js').Vertex, Node>} */
        this.pt2node = new Map();

        // Build graph from cell edges
        for (const cell of cells) {
            const face = cell.face;
            if (!face) continue;

            for (const edge of face.edges()) {
                // Skip edges with special data (walls, rivers, etc.)
                if (edge.data !== null && edge.data !== undefined) continue;

                const node1 = this.getNode(edge.origin);
                const node2 = this.getNode(edge.next.origin);

                const distance = Point.distance(
                    edge.origin.point,
                    edge.next.origin.point
                );

                node1.link(node2, distance, false);
            }
        }
    }

    /**
     * Get or create a node for a vertex
     * @param {import('../geometry/DCEL.js').Vertex} vertex
     * @returns {Node}
     */
    getNode(vertex) {
        if (this.pt2node.has(vertex)) {
            return this.pt2node.get(vertex);
        }

        const node = this.graph.createNode(vertex);
        this.pt2node.set(vertex, node);
        return node;
    }

    /**
     * Find path between two vertices
     * @param {import('../geometry/DCEL.js').Vertex} start
     * @param {import('../geometry/DCEL.js').Vertex} end
     * @returns {import('../geometry/DCEL.js').Vertex[]|null}
     */
    buildPath(start, end) {
        const startNode = this.pt2node.get(start);
        const endNode = this.pt2node.get(end);

        if (!startNode || !endNode) return null;

        const nodePath = this.graph.aStar(startNode, endNode);
        if (!nodePath) return null;

        return nodePath.map(node => node.data);
    }

    /**
     * Exclude certain points from pathfinding
     * @param {import('../geometry/DCEL.js').Vertex[]} points
     */
    excludePoints(points) {
        for (const point of points) {
            const node = this.pt2node.get(point);
            if (node) {
                node.unlinkAll();
            }
        }
    }

    /**
     * Exclude edges of a polygon from pathfinding
     * @param {import('../geometry/DCEL.js').HalfEdge[]} edges
     */
    excludePolygon(edges) {
        for (const edge of edges) {
            const node1 = this.pt2node.get(edge.origin);
            const node2 = this.pt2node.get(edge.next.origin);

            if (node1 && node2) {
                node1.unlink(node2);
            }
        }
    }

    /**
     * Find path from a vertex to the closest gate
     * @param {import('../geometry/DCEL.js').Vertex} start
     * @param {import('../geometry/DCEL.js').Vertex[]} gates
     * @returns {{path: import('../geometry/DCEL.js').Vertex[], gate: import('../geometry/DCEL.js').Vertex}|null}
     */
    pathToNearestGate(start, gates) {
        let bestPath = null;
        let bestGate = null;
        let bestDist = Infinity;

        for (const gate of gates) {
            const path = this.buildPath(start, gate);
            if (path) {
                // Calculate total path length
                let dist = 0;
                for (let i = 0; i < path.length - 1; i++) {
                    dist += Point.distance(path[i].point, path[i + 1].point);
                }

                if (dist < bestDist) {
                    bestDist = dist;
                    bestPath = path;
                    bestGate = gate;
                }
            }
        }

        if (bestPath) {
            return { path: bestPath, gate: bestGate };
        }

        return null;
    }

    /**
     * Build main arteries connecting gates through center
     * @param {import('../geometry/DCEL.js').Vertex} center - City center vertex
     * @param {import('../geometry/DCEL.js').Vertex[]} gates - Gate vertices
     * @returns {import('../geometry/DCEL.js').Vertex[][]} - Array of paths
     */
    buildArteries(center, gates) {
        const arteries = [];

        for (const gate of gates) {
            const path = this.buildPath(gate, center);
            if (path) {
                arteries.push(path);
            }
        }

        return arteries;
    }

    /**
     * Convert a vertex path to edge chain
     * @param {import('../geometry/DCEL.js').Vertex[]} vertices
     * @returns {import('../geometry/DCEL.js').HalfEdge[]}
     */
    pathToEdges(vertices) {
        const edges = [];

        for (let i = 0; i < vertices.length - 1; i++) {
            const v1 = vertices[i];
            const v2 = vertices[i + 1];

            // Find edge connecting v1 to v2
            for (const edge of v1.edges) {
                if (edge.next.origin === v2) {
                    edges.push(edge);
                    break;
                }
            }
        }

        return edges;
    }
}
