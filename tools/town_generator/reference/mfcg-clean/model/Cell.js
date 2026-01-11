/**
 * Cell.js - City cell/patch
 *
 * Represents a single cell in the city's Voronoi-based layout.
 * Cells are the fundamental building blocks of the city structure.
 */

export class Cell {
    /**
     * Create a cell
     * @param {import('../geometry/DCEL.js').Face} face - DCEL face for this cell
     */
    constructor(face) {
        /** @type {import('../geometry/DCEL.js').Face} */
        this.face = face;

        /** @type {import('../core/Point.js').Point[]} */
        this.shape = face.data ? face.data.shape : face.getPoly();

        /** @type {boolean} */
        this.waterbody = false;

        /** @type {boolean} */
        this.withinCity = false;

        /** @type {boolean} */
        this.withinWalls = false;

        /** @type {boolean} */
        this.landing = false;

        /** @type {import('./Ward.js').Ward|null} */
        this.ward = null;

        /** @type {import('./District.js').District|null} */
        this.district = null;

        /** @type {number} */
        this.seed = 0;

        // Link face data to this cell
        if (face.data) {
            face.data = this;
        } else {
            face.data = this;
        }
    }

    /**
     * Check if this cell borders a set of edges
     * @param {import('../geometry/DCEL.js').HalfEdge[]} edges
     * @returns {boolean}
     */
    bordersInside(edges) {
        if (!edges || edges.length === 0) return false;

        for (const edge of this.face.edges()) {
            for (const targetEdge of edges) {
                if (edge.origin === targetEdge.origin) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Get all neighbor cells
     * @returns {Cell[]}
     */
    getNeighbors() {
        const neighbors = [];
        for (const edge of this.face.edges()) {
            if (edge.twin && edge.twin.face.data instanceof Cell) {
                neighbors.push(edge.twin.face.data);
            }
        }
        return neighbors;
    }

    /**
     * Check if cell shares an edge with another cell
     * @param {Cell} other
     * @returns {boolean}
     */
    isAdjacentTo(other) {
        for (const edge of this.face.edges()) {
            if (edge.twin && edge.twin.face.data === other) {
                return true;
            }
        }
        return false;
    }
}
