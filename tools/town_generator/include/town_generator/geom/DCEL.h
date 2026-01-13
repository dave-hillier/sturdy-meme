#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <any>
#include <functional>

namespace town_generator {

// Forward declarations for building namespace
namespace building {
    class Cell;
}

namespace geom {

// Forward declarations
class Vertex;
class HalfEdge;
class Face;
class DCEL;

// Smart pointer types for DCEL elements
using VertexPtr = std::shared_ptr<Vertex>;
using HalfEdgePtr = std::shared_ptr<HalfEdge>;
using FacePtr = std::shared_ptr<Face>;

/**
 * Vertex - DCEL vertex with position and incident edges
 *
 * Key design: References existing PointPtr (not owning) to enable
 * shared point mutation across Cell boundaries, matching Haxe/JS semantics.
 */
class Vertex : public std::enable_shared_from_this<Vertex> {
public:
    PointPtr point;                              // Shared pointer to position
    std::vector<std::weak_ptr<HalfEdge>> edges;  // Outgoing half-edges (weak to avoid cycles)

    explicit Vertex(PointPtr p) : point(std::move(p)) {}

    double x() const { return point->x; }
    double y() const { return point->y; }

    // Equality by pointer identity (same underlying point)
    bool operator==(const Vertex& other) const { return point == other.point; }
    bool operator!=(const Vertex& other) const { return point != other.point; }

    // Get active (non-expired) outgoing edges
    std::vector<HalfEdgePtr> getEdges() const {
        std::vector<HalfEdgePtr> result;
        for (const auto& weak : edges) {
            if (auto edge = weak.lock()) {
                result.push_back(edge);
            }
        }
        return result;
    }

    // Add edge reference
    void addEdge(const HalfEdgePtr& edge) {
        edges.push_back(edge);
    }

    // Remove expired weak references
    void cleanupEdges() {
        edges.erase(
            std::remove_if(edges.begin(), edges.end(),
                [](const std::weak_ptr<HalfEdge>& w) { return w.expired(); }),
            edges.end()
        );
    }
};

/**
 * HalfEdge - Directed edge in DCEL
 *
 * Uses shared_ptr for forward references, weak_ptr for back-references.
 * Faithful to mfcg-clean/geometry/DCEL.js HalfEdge class.
 */
class HalfEdge : public std::enable_shared_from_this<HalfEdge> {
public:
    VertexPtr origin;                     // Start vertex (shared)
    std::weak_ptr<HalfEdge> twin;         // Opposite direction edge (weak, may be null)
    HalfEdgePtr next;                     // Next edge in face cycle (shared, forms ownership chain)
    std::weak_ptr<HalfEdge> prev;         // Previous edge (weak to break cycle)
    std::weak_ptr<Face> face;             // Incident face (weak back-reference)

    // Generic edge data storage (like mfcg.js edge.data)
    std::any data;

    // Derived accessors
    VertexPtr destination() const {
        return next ? next->origin : nullptr;
    }

    double length() const;

    // Get face (locks weak pointer)
    FacePtr getFace() const { return face.lock(); }

    // Get twin (locks weak pointer)
    HalfEdgePtr getTwin() const { return twin.lock(); }

    // Get prev (locks weak pointer)
    HalfEdgePtr getPrev() const { return prev.lock(); }

    // Get edge data as specific type (returns default if not set or wrong type)
    template<typename T>
    T getData() const {
        if (data.has_value()) {
            try {
                return std::any_cast<T>(data);
            } catch (const std::bad_any_cast&) {
                return T{};
            }
        }
        return T{};
    }

    template<typename T>
    void setData(T value) {
        data = std::move(value);
    }

    bool hasData() const {
        return data.has_value();
    }

    void clearData() {
        data.reset();
    }
};

/**
 * Face - Polygonal face in DCEL
 *
 * Stores a void* data pointer for back-reference to Cell or other structures.
 * Faithful to mfcg-clean Face class with data field.
 */
class Face : public std::enable_shared_from_this<Face> {
public:
    HalfEdgePtr halfEdge;        // One half-edge on the boundary (shared)
    void* data = nullptr;        // Custom data (e.g., Cell*)

    // Get polygon representation as Point values
    std::vector<Point> getPoly() const;

    // Get polygon as shared PointPtrs (preserves reference semantics)
    std::vector<PointPtr> getPolyPtrs() const;

    // Get all vertices of this face
    std::vector<VertexPtr> vertices() const;

    // Iterator for range-based for over edges
    class EdgeIterator {
    public:
        EdgeIterator(HalfEdgePtr start, HalfEdgePtr current, bool done)
            : start_(start), current_(current), done_(done) {}

        HalfEdgePtr operator*() const { return current_; }

        EdgeIterator& operator++() {
            if (current_) {
                current_ = current_->next;
                if (current_ == start_) {
                    done_ = true;
                }
            }
            return *this;
        }

        bool operator!=(const EdgeIterator& other) const {
            return !done_ || !other.done_;
        }

    private:
        HalfEdgePtr start_;
        HalfEdgePtr current_;
        bool done_;
    };

    class EdgeRange {
    public:
        explicit EdgeRange(HalfEdgePtr start) : start_(std::move(start)) {}

        EdgeIterator begin() const {
            return EdgeIterator(start_, start_, !start_);
        }

        EdgeIterator end() const {
            return EdgeIterator(start_, start_, true);
        }

    private:
        HalfEdgePtr start_;
    };

    // Iterate over edges: for (auto edge : face.edges()) { ... }
    EdgeRange edges() const {
        return EdgeRange(halfEdge);
    }

    // Count edges in face
    size_t edgeCount() const;

    // Computed properties
    double area() const;
    Point centroid() const;
};

/**
 * Result of edge collapse operation
 */
struct CollapseResult {
    VertexPtr vertex;                          // Merged vertex
    std::vector<HalfEdgePtr> affectedEdges;    // Edges that need face shape update
};

/**
 * DCEL - Doubly Connected Edge List
 *
 * Topological data structure for planar subdivision.
 * Faithful port of mfcg-clean/geometry/DCEL.js.
 *
 * Uses shared_ptr for all elements with weak_ptr for back-references
 * to handle cyclic dependencies properly.
 */
class DCEL {
public:
    // Comparator for PointPtr map key (by pointer identity)
    struct PointPtrCompare {
        bool operator()(const PointPtr& a, const PointPtr& b) const {
            return a.get() < b.get();
        }
    };

    // Storage with shared ownership
    std::map<PointPtr, VertexPtr, PointPtrCompare> vertices;
    std::vector<HalfEdgePtr> edges;
    std::vector<FacePtr> faces;

    // Constructors
    DCEL() = default;
    explicit DCEL(const std::vector<Polygon>& polygons);

    // Allow copying and moving
    DCEL(const DCEL&) = default;
    DCEL& operator=(const DCEL&) = default;
    DCEL(DCEL&&) noexcept = default;
    DCEL& operator=(DCEL&&) noexcept = default;

    /**
     * Build DCEL from polygon array
     *
     * Each polygon becomes a Face. Shared vertices are detected by PointPtr identity.
     * Twin edges are linked where polygons share edges (reversed direction).
     */
    void buildFromPolygons(const std::vector<Polygon>& polygons);

    /**
     * Vertex access
     */
    VertexPtr getVertex(const PointPtr& point) const;
    VertexPtr getOrCreateVertex(PointPtr point);

    /**
     * Check if DCEL contains a vertex for this point
     */
    bool hasVertex(const PointPtr& point) const {
        return vertices.find(point) != vertices.end();
    }

    // =========================================================================
    // Topology Operations (static methods for operating on face subsets)
    // =========================================================================

    /**
     * Find circumference edges of a set of faces
     *
     * Returns boundary edges in traversal order. An edge is on the boundary if:
     * - Its twin is nullptr (external boundary), OR
     * - Its twin's face is not in the face set
     *
     * Faithful to mfcg-clean DCEL.circumference()
     *
     * @param startEdge Optional starting edge (uses first boundary edge if null)
     * @param faceList Faces to find circumference of
     * @return Ordered list of boundary half-edges
     */
    static std::vector<HalfEdgePtr> circumference(const HalfEdgePtr& startEdge,
                                                   const std::vector<FacePtr>& faceList);

    /**
     * Split faces into connected components
     *
     * Uses BFS through twin edges to find connected regions.
     * Faithful to mfcg-clean DCEL.split()
     *
     * @param faceList Faces to split
     * @return Vector of connected components (each is a vector of faces)
     */
    static std::vector<std::vector<FacePtr>> split(const std::vector<FacePtr>& faceList);

    // =========================================================================
    // Edge Operations
    // =========================================================================

    /**
     * Collapse an edge, merging its endpoints
     *
     * - Moves destination vertex to midpoint
     * - Reassigns all destination's edges to origin
     * - Removes collapsed edge from face cycles
     * - Updates face.halfEdge pointers if needed
     *
     * Faithful to mfcg-clean DCEL.collapseEdge()
     *
     * @param edge Edge to collapse
     * @return CollapseResult with merged vertex and affected edges
     */
    CollapseResult collapseEdge(const HalfEdgePtr& edge);

    /**
     * Split an edge at its midpoint
     *
     * Inserts a new vertex at the midpoint and creates new half-edges.
     * Faithful to mfcg-clean DCEL.splitEdge()
     *
     * @param edge Edge to split
     * @return Newly created vertex at midpoint
     */
    VertexPtr splitEdge(const HalfEdgePtr& edge);

    /**
     * Convert vertex path to edge chain
     *
     * Finds half-edges connecting consecutive vertices.
     * Faithful to mfcg-clean DCEL.vertices2chain()
     *
     * @param verts Ordered list of vertices
     * @return List of half-edges connecting them
     */
    std::vector<HalfEdgePtr> vertices2chain(const std::vector<VertexPtr>& verts);

    /**
     * Find half-edge from one vertex to another
     *
     * @param from Origin vertex
     * @param to Destination vertex
     * @return Half-edge or nullptr if not found
     */
    HalfEdgePtr findEdge(const VertexPtr& from, const VertexPtr& to);

    /**
     * Clean up expired weak references in all vertices
     */
    void cleanup();
};

} // namespace geom
} // namespace town_generator
