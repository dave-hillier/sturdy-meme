/**
 * Graph.js - Graph data structure with pathfinding
 *
 * Used for road network topology and A* pathfinding.
 */

/**
 * Graph node with arbitrary data
 */
export class Node {
    /**
     * Create a node
     * @param {*} data - Node data
     */
    constructor(data) {
        /** @type {*} */
        this.data = data;

        /** @type {Map<Node, number>} */
        this.links = new Map();
    }

    /**
     * Link this node to another with a weight
     * @param {Node} other - Target node
     * @param {number} [weight=1] - Edge weight
     * @param {boolean} [bidirectional=true] - Create reverse link
     */
    link(other, weight = 1, bidirectional = true) {
        this.links.set(other, weight);
        if (bidirectional) {
            other.links.set(this, weight);
        }
    }

    /**
     * Remove link to another node
     * @param {Node} other - Target node
     * @param {boolean} [bidirectional=true] - Remove reverse link
     */
    unlink(other, bidirectional = true) {
        this.links.delete(other);
        if (bidirectional) {
            other.links.delete(this);
        }
    }

    /**
     * Remove all links from this node
     */
    unlinkAll() {
        for (const other of this.links.keys()) {
            other.links.delete(this);
        }
        this.links.clear();
    }

    /**
     * Get all linked nodes
     * @returns {Node[]}
     */
    neighbors() {
        return Array.from(this.links.keys());
    }

    /**
     * Get link weight to a node
     * @param {Node} other
     * @returns {number|undefined}
     */
    weightTo(other) {
        return this.links.get(other);
    }
}

/**
 * Graph with A* pathfinding
 */
export class Graph {
    constructor() {
        /** @type {Node[]} */
        this.nodes = [];
    }

    /**
     * Add a node to the graph
     * @param {Node} node
     * @returns {Node}
     */
    add(node) {
        this.nodes.push(node);
        return node;
    }

    /**
     * Create a new node with data and add it
     * @param {*} data
     * @returns {Node}
     */
    createNode(data) {
        const node = new Node(data);
        return this.add(node);
    }

    /**
     * Remove a node from the graph
     * @param {Node} node
     */
    remove(node) {
        node.unlinkAll();
        const idx = this.nodes.indexOf(node);
        if (idx !== -1) {
            this.nodes.splice(idx, 1);
        }
    }

    /**
     * Find shortest path using A* algorithm
     * @param {Node} start - Start node
     * @param {Node} goal - Goal node
     * @param {Node[]} [excluded=[]] - Nodes to exclude from path
     * @returns {Node[]|null} - Path from start to goal, or null if not found
     */
    aStar(start, goal, excluded = []) {
        if (start === goal) return [start];

        const openSet = [start];
        const cameFrom = new Map();
        const gScore = new Map();
        gScore.set(start, 0);

        const closedSet = new Set(excluded);

        while (openSet.length > 0) {
            // Get node with lowest gScore
            let current = openSet[0];
            let currentG = gScore.get(current);

            for (let i = 1; i < openSet.length; i++) {
                const g = gScore.get(openSet[i]);
                if (g < currentG) {
                    current = openSet[i];
                    currentG = g;
                }
            }

            if (current === goal) {
                return this.reconstructPath(cameFrom, current);
            }

            // Move current from open to closed
            const idx = openSet.indexOf(current);
            openSet.splice(idx, 1);
            closedSet.add(current);

            // Check neighbors
            for (const [neighbor, weight] of current.links) {
                if (closedSet.has(neighbor)) continue;

                const tentativeG = currentG + weight;

                if (!openSet.includes(neighbor)) {
                    openSet.push(neighbor);
                } else if (tentativeG >= gScore.get(neighbor)) {
                    continue;
                }

                cameFrom.set(neighbor, current);
                gScore.set(neighbor, tentativeG);
            }
        }

        return null; // No path found
    }

    /**
     * Reconstruct path from cameFrom map
     * @param {Map<Node, Node>} cameFrom
     * @param {Node} current
     * @returns {Node[]}
     */
    reconstructPath(cameFrom, current) {
        const path = [current];
        while (cameFrom.has(current)) {
            current = cameFrom.get(current);
            path.push(current);
        }
        return path.reverse();
    }

    /**
     * Find all nodes reachable from a start node
     * @param {Node} start
     * @returns {Node[]}
     */
    reachable(start) {
        const visited = new Set();
        const queue = [start];

        while (queue.length > 0) {
            const current = queue.shift();
            if (visited.has(current)) continue;

            visited.add(current);

            for (const neighbor of current.links.keys()) {
                if (!visited.has(neighbor)) {
                    queue.push(neighbor);
                }
            }
        }

        return Array.from(visited);
    }

    /**
     * Get connected components
     * @returns {Node[][]}
     */
    components() {
        const result = [];
        const unvisited = new Set(this.nodes);

        while (unvisited.size > 0) {
            const start = unvisited.values().next().value;
            const component = this.reachable(start);
            result.push(component);

            for (const node of component) {
                unvisited.delete(node);
            }
        }

        return result;
    }
}
