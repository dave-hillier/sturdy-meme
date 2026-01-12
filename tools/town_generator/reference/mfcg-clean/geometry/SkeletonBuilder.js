/**
 * SkeletonBuilder.js - Straight skeleton algorithm
 *
 * Generates a straight skeleton for polygon roof generation.
 * The skeleton consists of ribs (edges) and bones (the skeleton edges).
 */

import { Point } from '../core/Point.js';
import { GeomUtils } from './GeomUtils.js';

/**
 * Node in the skeleton tree
 */
class SkeletonNode {
    /**
     * Create a skeleton node
     * @param {Point} point - Position
     * @param {number} [height=0] - Height at this node
     * @param {Rib} [child1=null] - First child rib
     * @param {Rib} [child2=null] - Second child rib
     */
    constructor(point, height = 0, child1 = null, child2 = null) {
        /** @type {Point} */
        this.point = point;

        /** @type {number} */
        this.height = height;

        /** @type {Rib|null} */
        this.child1 = child1;

        /** @type {Rib|null} */
        this.child2 = child2;

        /** @type {Rib|null} */
        this.parent = null;

        if (child1) {
            child1.b = this;
        }
        if (child2) {
            child2.b = this;
        }
    }
}

/**
 * Segment of the polygon boundary
 */
class SkeletonSegment {
    /**
     * Create a segment
     * @param {Point} p0 - Start point
     * @param {Point} p1 - End point
     */
    constructor(p0, p1) {
        /** @type {Point} */
        this.p0 = p0;

        /** @type {Point} */
        this.p1 = p1;

        /** @type {Point} */
        this.dir = p1.subtract(p0).normalized();

        /** @type {number} */
        this.len = Point.distance(p0, p1);

        /** @type {Rib|null} */
        this.lRib = null;

        /** @type {Rib|null} */
        this.rRib = null;
    }
}

/**
 * Rib of the skeleton (an edge from a node along a bisector)
 */
export class Rib {
    /**
     * Create a rib
     * @param {SkeletonNode} a - Start node
     * @param {SkeletonSegment} left - Left segment
     * @param {SkeletonSegment} right - Right segment
     */
    constructor(a, left, right) {
        /** @type {SkeletonNode} */
        this.a = a;
        this.a.parent = this;

        /** @type {SkeletonNode|null} */
        this.b = null;

        /** @type {SkeletonSegment} */
        this.left = left;

        /** @type {SkeletonSegment} */
        this.right = right;

        left.lRib = this;
        right.rRib = this;

        // Calculate bisector direction (slope)
        const leftDir = left.dir;
        const rightDir = right.dir;
        const dot = leftDir.x * rightDir.x + leftDir.y * rightDir.y;

        if (dot > 0.99999) {
            // Nearly parallel - perpendicular bisector
            this.slope = new Point(-leftDir.y, leftDir.x);
        } else {
            const c = Math.sqrt((1 + dot) / 2);
            this.slope = rightDir.subtract(leftDir);
            this.slope = this.slope.normalized().scale(1 / c);

            // Ensure correct orientation for reflex vertices
            if (!this.a.child1) {
                const cross = leftDir.x * rightDir.y - leftDir.y * rightDir.x;
                if (cross < 0) {
                    this.slope = this.slope.scale(-1);
                }
            }
        }
    }
}

/**
 * Straight skeleton builder
 */
export class SkeletonBuilder {
    /**
     * Create a skeleton builder
     * @param {Point[]} polygon - Input polygon
     * @param {boolean} [autoRun=false] - Automatically build skeleton
     */
    constructor(polygon, autoRun = false) {
        /** @type {number} */
        this.height = 0;

        /** @type {Point[]} */
        this.poly = polygon;

        const n = polygon.length;

        // Create segments
        /** @type {SkeletonSegment[]} */
        this.segments = [];
        for (let i = 0; i < n; i++) {
            this.segments.push(new SkeletonSegment(
                polygon[i],
                polygon[(i + 1) % n]
            ));
        }

        // Create leaf nodes and initial ribs
        /** @type {Map<Point, SkeletonNode>} */
        this.leaves = new Map();

        /** @type {Rib[]} */
        this.ribs = [];

        for (let i = 0; i < n; i++) {
            const node = new SkeletonNode(polygon[i]);
            this.leaves.set(polygon[i], node);

            const rib = new Rib(
                node,
                this.segments[i],
                this.segments[(i + n - 1) % n]
            );
            this.ribs.push(rib);
        }

        /** @type {Rib[]} */
        this.bones = [];

        /** @type {Rib|null} */
        this.root = null;

        /** @type {SkeletonSegment[]} */
        this.gables = [];

        if (autoRun) {
            this.run();
        }
    }

    /**
     * Find intersection of two ribs
     * @param {Rib} a
     * @param {Rib} b
     * @returns {Point|null} - Intersection parameters (x=t for a, y=t for b)
     */
    static intersect(a, b) {
        return GeomUtils.intersectLines(
            a.a.point.x, a.a.point.y, a.slope.x, a.slope.y,
            b.a.point.x, b.a.point.y, b.slope.x, b.slope.y
        );
    }

    /**
     * Run the skeleton algorithm to completion
     */
    run() {
        while (this.step()) {}
    }

    /**
     * Perform one step of the algorithm
     * @returns {boolean} - True if more steps needed
     */
    step() {
        if (this.ribs.length <= 2) {
            if (this.ribs.length === 2) {
                this.root = this.ribs[0];
                this.root.b = this.ribs[1].a;
                this.root.b.parent = this.root;
                this.bones.push(this.root);
                this.ribs = [];
            }
            return false;
        }

        // Find earliest event
        let minHeight = Infinity;
        let bestRib1 = null;
        let bestRib2 = null;
        let bestIntersection = null;

        for (const rib of this.ribs) {
            // Check intersection with right neighbor
            let candidate = null;
            let candidateRib = null;
            let candidateT = Infinity;

            const rightNeighbor = rib.right.lRib;
            const rightInt = SkeletonBuilder.intersect(rib, rightNeighbor);
            if (rightInt && rightInt.x >= 0 && rightInt.y >= 0) {
                const h = rightInt.x + rib.a.height;
                if (h < candidateT) {
                    candidateT = h;
                    candidate = rightInt;
                    candidateRib = rightNeighbor;
                }
            }

            // Check intersection with left neighbor
            const leftNeighbor = rib.left.rRib;
            const leftInt = SkeletonBuilder.intersect(rib, leftNeighbor);
            if (leftInt && leftInt.x >= 0 && leftInt.y >= 0) {
                const h = leftInt.x + rib.a.height;
                if (h < candidateT) {
                    candidateT = h;
                    candidate = leftInt;
                    candidateRib = leftNeighbor;
                }
            }

            if (candidate) {
                const height = candidate.y + candidateRib.a.height;
                if (height < minHeight) {
                    minHeight = height;
                    bestRib1 = rib;
                    bestRib2 = candidateRib;
                    bestIntersection = candidate;
                }
            }
        }

        if (!bestIntersection) return false;

        this.height = minHeight;

        // Calculate intersection point
        const t = bestIntersection.x;
        const intersectPoint = new Point(
            bestRib1.a.point.x + bestRib1.slope.x * t,
            bestRib1.a.point.y + bestRib1.slope.y * t
        );

        this.merge(bestRib1, bestRib2, intersectPoint);
        return true;
    }

    /**
     * Merge two ribs at an intersection point
     * @param {Rib} rib1
     * @param {Rib} rib2
     * @param {Point} point
     * @returns {Rib}
     */
    merge(rib1, rib2, point) {
        const node = new SkeletonNode(point, this.height, rib1, rib2);

        let newRib;
        if (rib1.right === rib2.left) {
            newRib = new Rib(node, rib1.left, rib2.right);
        } else {
            newRib = new Rib(node, rib2.left, rib1.right);
        }

        this.ribs.push(newRib);

        // Add to bones and remove from active ribs
        this.bones.push(rib1);
        const idx1 = this.ribs.indexOf(rib1);
        if (idx1 !== -1) this.ribs.splice(idx1, 1);

        this.bones.push(rib2);
        const idx2 = this.ribs.indexOf(rib2);
        if (idx2 !== -1) this.ribs.splice(idx2, 1);

        return newRib;
    }

    /**
     * Add gables (horizontal roof edges on longest sides)
     */
    addGables() {
        this.gables = [];

        for (const segment of this.segments) {
            const node1 = this.leaves.get(segment.p0);
            const node2 = this.leaves.get(segment.p1);

            if (!node1 || !node2 || !node1.parent || !node2.parent) continue;

            const rib1 = node1.parent;
            const rib2 = node2.parent;

            if (rib1.b === rib2.b) {
                const apex = rib1.b;
                const apexPoint = apex.point;

                // Find the sibling rib
                let siblingRib;
                if (rib1 === this.root) {
                    siblingRib = rib2 === apex.child1 ? apex.child2 : apex.child1;
                } else if (rib2 === this.root) {
                    siblingRib = rib1 === apex.child1 ? apex.child2 : apex.child1;
                } else if (apex.parent) {
                    siblingRib = apex.parent;
                } else {
                    continue;
                }

                if (!siblingRib) continue;

                const intersection = GeomUtils.intersectLines(
                    segment.p0.x, segment.p0.y, segment.dir.x, segment.dir.y,
                    apexPoint.x, apexPoint.y, siblingRib.slope.x, siblingRib.slope.y
                );

                if (intersection && intersection.x > 0 && intersection.x < segment.len) {
                    const gablePoint = new Point(
                        segment.p0.x + segment.dir.x * intersection.x,
                        segment.p0.y + segment.dir.y * intersection.x
                    );
                    apexPoint.x = gablePoint.x;
                    apexPoint.y = gablePoint.y;
                    this.gables.push(segment);
                }
            }
        }
    }

    /**
     * Get path between two leaf nodes through the skeleton
     * @param {Point} p1
     * @param {Point} p2
     * @returns {SkeletonNode[]}
     */
    getPath(p1, p2) {
        const node1 = this.leaves.get(p1);
        const node2 = this.leaves.get(p2);

        if (!node1 || !node2) return [];
        if (node1 === node2) return [node1];

        const path1 = this.getPathToRoot(node1);
        const path2 = this.getPathToRoot(node2);

        // Find common ancestor
        if (path1[path1.length - 1] === path2[path2.length - 1]) {
            while (path1[path1.length - 1] === path2[path2.length - 1]) {
                path1.pop();
                path2.pop();
            }
            path1.push(path1[path1.length - 1].parent.b);
        }

        return path1.concat(path2.reverse());
    }

    /**
     * Get path from a node to the root
     * @param {SkeletonNode} node
     * @returns {SkeletonNode[]}
     */
    getPathToRoot(node) {
        const path = [node];

        while (this.root && this.root.a !== node && this.root.b !== node) {
            node = node.parent.b;
            path.push(node);
        }

        return path;
    }
}
