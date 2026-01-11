/**
 * Harbour.js - Harbour/dock ward
 *
 * Represents waterfront areas with docks and piers.
 */

import { Point } from '../core/Point.js';
import { Ward } from './Ward.js';
import { Segment } from '../geometry/Segment.js';
import { GeomUtils } from '../geometry/GeomUtils.js';

/**
 * Harbour ward with piers
 */
export class Harbour extends Ward {
    /**
     * Create a harbour ward
     * @param {import('../model/City.js').City} model
     * @param {import('../model/Cell.js').Cell} patch
     */
    constructor(model, patch) {
        super(model, patch);

        /** @type {Array<[Point, Point]>} */
        this.piers = [];
    }

    /**
     * Create harbour geometry
     */
    createGeometry() {
        // Find canal origins to avoid
        const canalOrigins = [];
        for (const canal of this.model.canals) {
            canalOrigins.push(canal.course[0].origin);
        }

        // Find edges that face landing areas
        const landingEdges = [];
        let edge = this.patch.face.halfEdge;

        do {
            const neighbor = this.model.getNeighbour ?
                this.model.getNeighbour(this.patch, edge.origin) :
                edge.twin?.face.data;

            if (neighbor && neighbor.landing) {
                const p1 = edge.origin.point;
                const p2 = edge.next.origin.point;

                // Avoid canal origins
                if (canalOrigins.includes(edge.origin)) {
                    landingEdges.push(new Segment(
                        GeomUtils.lerp(p1, p2, 0.5),
                        p2
                    ));
                } else if (canalOrigins.includes(edge.next.origin)) {
                    landingEdges.push(new Segment(
                        p1,
                        GeomUtils.lerp(p1, p2, 0.5)
                    ));
                } else {
                    landingEdges.push(new Segment(p1, p2));
                }
            }

            edge = edge.next;
        } while (edge !== this.patch.face.halfEdge);

        if (landingEdges.length === 0) {
            this.piers = [];
            return;
        }

        // Find longest landing edge for piers
        let longestEdge = landingEdges[0];
        for (const e of landingEdges) {
            if (e.length > longestEdge.length) {
                longestEdge = e;
            }
        }

        // Create piers along the edge
        const edgeLen = longestEdge.length;
        const numPiers = Math.floor(edgeLen / 6);

        if (numPiers === 0) {
            this.piers = [];
            return;
        }

        const pierSpacing = 6 * (numPiers - 1);
        const startT = (1 - pierSpacing / edgeLen) / 2;
        const stepT = pierSpacing / (numPiers - 1) / edgeLen;

        this.piers = [];
        for (let i = 0; i < numPiers; i++) {
            const t = startT + i * stepT;
            const base = longestEdge.pointAt(t);

            // Pier extends perpendicular to edge
            const normal = longestEdge.normal;
            const pierEnd = base.add(normal.scale(8));

            this.piers.push([base, pierEnd]);
        }
    }

    /**
     * Get label for this ward
     * @returns {string}
     */
    getLabel() {
        return 'Harbour';
    }
}
