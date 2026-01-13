#pragma once

#include "town_generator/geom/DCEL.h"
#include <vector>

namespace town_generator {
namespace geom {

/**
 * EdgeChain - Utilities for working with sequences of half-edges
 *
 * Faithful port of mfcg-clean/geometry/DCEL.js EdgeChain class.
 */
class EdgeChain {
public:
    /**
     * Convert edge chain to polygon points
     *
     * @param chain Ordered list of half-edges
     * @return Point array (origin of each edge)
     */
    static std::vector<Point> toPoly(const std::vector<HalfEdgePtr>& chain);

    /**
     * Convert edge chain to polyline (includes last point)
     *
     * @param chain Ordered list of half-edges
     * @return Point array including destination of last edge
     */
    static std::vector<Point> toPolyline(const std::vector<HalfEdgePtr>& chain);

    /**
     * Convert edge chain to polygon with shared PointPtrs
     *
     * Preserves reference semantics for point mutations.
     *
     * @param chain Ordered list of half-edges
     * @return PointPtr array (origin point of each edge)
     */
    static std::vector<PointPtr> toPolyPtrs(const std::vector<HalfEdgePtr>& chain);

    /**
     * Get all vertices from edge chain
     *
     * @param chain Ordered list of half-edges
     * @return Vertex pointers (origin of each edge)
     */
    static std::vector<VertexPtr> vertices(const std::vector<HalfEdgePtr>& chain);

    /**
     * Assign data to all edges in chain
     *
     * @param chain Edges to modify
     * @param data Value to assign
     * @param overwrite If false, only assign to edges without data
     */
    template<typename T>
    static void assignData(std::vector<HalfEdgePtr>& chain, T data, bool overwrite = true) {
        for (auto& edge : chain) {
            if (edge && (overwrite || !edge->hasData())) {
                edge->setData(data);
            }
        }
    }

    /**
     * Find edge by origin vertex
     *
     * @param chain Edges to search
     * @param vertex Origin vertex to find
     * @return Edge with matching origin, or nullptr
     */
    static HalfEdgePtr edgeByOrigin(const std::vector<HalfEdgePtr>& chain, const VertexPtr& vertex);

    /**
     * Find edge by origin point (by pointer identity)
     *
     * @param chain Edges to search
     * @param point Origin point to find
     * @return Edge with matching origin point, or nullptr
     */
    static HalfEdgePtr edgeByOriginPoint(const std::vector<HalfEdgePtr>& chain, const PointPtr& point);

    /**
     * Get total length of edge chain
     *
     * @param chain Edges to measure
     * @return Sum of edge lengths
     */
    static double length(const std::vector<HalfEdgePtr>& chain);

    /**
     * Check if chain forms a closed loop
     *
     * @param chain Edges to check
     * @return True if last edge's destination equals first edge's origin
     */
    static bool isClosed(const std::vector<HalfEdgePtr>& chain);

    /**
     * Reverse the direction of an edge chain
     *
     * Returns a new chain using twin edges in reverse order.
     * Note: Requires all edges to have twins.
     *
     * @param chain Edges to reverse
     * @return Reversed chain (or empty if any twin is missing)
     */
    static std::vector<HalfEdgePtr> reverse(const std::vector<HalfEdgePtr>& chain);
};

} // namespace geom
} // namespace town_generator
