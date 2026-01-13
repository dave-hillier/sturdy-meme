#include "town_generator/geom/DCEL.h"
#include "town_generator/geom/EdgeChain.h"
#include <algorithm>
#include <queue>
#include <cmath>

namespace town_generator {
namespace geom {

// =============================================================================
// HalfEdge Implementation
// =============================================================================

double HalfEdge::length() const {
    auto dest = destination();
    if (!origin || !dest) return 0.0;
    return Point::distance(*origin->point, *dest->point);
}

// =============================================================================
// Face Implementation
// =============================================================================

std::vector<Point> Face::getPoly() const {
    std::vector<Point> poly;
    if (!halfEdge) return poly;

    auto edge = halfEdge;
    do {
        poly.push_back(*edge->origin->point);
        edge = edge->next;
    } while (edge && edge != halfEdge);

    return poly;
}

std::vector<PointPtr> Face::getPolyPtrs() const {
    std::vector<PointPtr> poly;
    if (!halfEdge) return poly;

    auto edge = halfEdge;
    do {
        poly.push_back(edge->origin->point);
        edge = edge->next;
    } while (edge && edge != halfEdge);

    return poly;
}

std::vector<VertexPtr> Face::vertices() const {
    std::vector<VertexPtr> verts;
    if (!halfEdge) return verts;

    auto edge = halfEdge;
    do {
        verts.push_back(edge->origin);
        edge = edge->next;
    } while (edge && edge != halfEdge);

    return verts;
}

size_t Face::edgeCount() const {
    if (!halfEdge) return 0;

    size_t count = 0;
    auto edge = halfEdge;
    do {
        ++count;
        edge = edge->next;
    } while (edge && edge != halfEdge);

    return count;
}

double Face::area() const {
    // Shoelace formula
    auto poly = getPoly();
    if (poly.size() < 3) return 0.0;

    double sum = 0.0;
    size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Point& p1 = poly[i];
        const Point& p2 = poly[(i + 1) % n];
        sum += (p1.x * p2.y - p2.x * p1.y);
    }
    return std::abs(sum) / 2.0;
}

Point Face::centroid() const {
    auto poly = getPoly();
    if (poly.empty()) return Point(0, 0);

    double cx = 0, cy = 0;
    for (const auto& p : poly) {
        cx += p.x;
        cy += p.y;
    }
    return Point(cx / poly.size(), cy / poly.size());
}

// =============================================================================
// DCEL Implementation
// =============================================================================

DCEL::DCEL(const std::vector<Polygon>& polygons) {
    buildFromPolygons(polygons);
}

void DCEL::buildFromPolygons(const std::vector<Polygon>& polygons) {
    // Clear existing data
    vertices.clear();
    edges.clear();
    faces.clear();

    // Create vertices from polygon points (uses existing PointPtrs)
    for (const auto& poly : polygons) {
        for (size_t i = 0; i < poly.length(); ++i) {
            PointPtr p = poly.ptr(i);
            getOrCreateVertex(p);
        }
    }

    // Edge key for twin lookup: maps (p1, p2) -> HalfEdge*
    // Using raw pointers for map key since PointPtr comparison is by identity
    std::map<std::pair<Point*, Point*>, HalfEdgePtr> edgeMap;

    // Create faces and edges
    for (const auto& poly : polygons) {
        if (poly.length() < 3) continue;

        auto face = std::make_shared<Face>();
        std::vector<HalfEdgePtr> faceEdges;

        for (size_t i = 0; i < poly.length(); ++i) {
            PointPtr p1 = poly.ptr(i);
            PointPtr p2 = poly.ptr((i + 1) % poly.length());

            auto edge = std::make_shared<HalfEdge>();
            edge->origin = vertices[p1];
            edge->origin->addEdge(edge);
            edge->face = face;

            faceEdges.push_back(edge);
            edgeMap[{p1.get(), p2.get()}] = edge;
            edges.push_back(edge);
        }

        // Link edges in cycle (next/prev)
        // Note: next is shared_ptr (ownership chain), prev is weak_ptr (back-ref)
        for (size_t i = 0; i < faceEdges.size(); ++i) {
            faceEdges[i]->next = faceEdges[(i + 1) % faceEdges.size()];
            faceEdges[i]->prev = faceEdges[(i + faceEdges.size() - 1) % faceEdges.size()];
        }

        face->halfEdge = faceEdges[0];
        faces.push_back(face);
    }

    // Link twin edges (edges with reversed direction)
    for (const auto& [key, edge] : edgeMap) {
        auto twinKey = std::make_pair(key.second, key.first);
        auto it = edgeMap.find(twinKey);
        if (it != edgeMap.end()) {
            edge->twin = it->second;
        }
    }
}

VertexPtr DCEL::getVertex(const PointPtr& point) const {
    auto it = vertices.find(point);
    return (it != vertices.end()) ? it->second : nullptr;
}

VertexPtr DCEL::getOrCreateVertex(PointPtr point) {
    auto it = vertices.find(point);
    if (it != vertices.end()) {
        return it->second;
    }

    auto vertex = std::make_shared<Vertex>(point);
    vertices[point] = vertex;
    return vertex;
}

void DCEL::cleanup() {
    for (auto& [point, vertex] : vertices) {
        vertex->cleanupEdges();
    }
}

// =============================================================================
// Topology Operations
// =============================================================================

std::vector<HalfEdgePtr> DCEL::circumference(const HalfEdgePtr& startEdge,
                                              const std::vector<FacePtr>& faceList) {
    if (faceList.empty()) return {};

    std::set<Face*> faceSet;
    for (const auto& f : faceList) {
        if (f) faceSet.insert(f.get());
    }

    std::vector<HalfEdgePtr> boundaryEdges;

    // Find all edges on the boundary
    for (const auto& face : faceList) {
        if (!face || !face->halfEdge) continue;

        auto edge = face->halfEdge;
        do {
            // Edge is on boundary if twin is null or twin's face is not in set
            auto twinEdge = edge->getTwin();
            if (!twinEdge) {
                boundaryEdges.push_back(edge);
            } else {
                auto twinFace = twinEdge->getFace();
                if (!twinFace || faceSet.find(twinFace.get()) == faceSet.end()) {
                    boundaryEdges.push_back(edge);
                }
            }
            edge = edge->next;
        } while (edge && edge != face->halfEdge);
    }

    if (boundaryEdges.empty()) return {};

    // Sort edges into a cycle by following connectivity
    std::vector<HalfEdgePtr> result;

    // Find starting edge
    HalfEdgePtr current;
    if (startEdge) {
        auto it = std::find(boundaryEdges.begin(), boundaryEdges.end(), startEdge);
        if (it != boundaryEdges.end()) {
            current = startEdge;
        }
    }
    if (!current) {
        current = boundaryEdges[0];
    }

    std::set<HalfEdge*> boundarySet;
    for (const auto& e : boundaryEdges) {
        boundarySet.insert(e.get());
    }

    std::set<HalfEdge*> visited;

    while (current && visited.find(current.get()) == visited.end()) {
        visited.insert(current.get());
        result.push_back(current);

        // Find next boundary edge
        // Walk around the destination vertex to find the next boundary edge
        auto next = current->next;
        while (next && boundarySet.find(next.get()) == boundarySet.end()) {
            auto nextTwin = next->getTwin();
            if (nextTwin) {
                next = nextTwin->next;
            } else {
                break;
            }
        }

        if (next && boundarySet.find(next.get()) != boundarySet.end()) {
            current = next;
        } else {
            current = nullptr;
        }
    }

    return result;
}

std::vector<std::vector<FacePtr>> DCEL::split(const std::vector<FacePtr>& faceList) {
    if (faceList.empty()) return {};

    std::set<Face*> faceSet;
    for (const auto& f : faceList) {
        if (f) faceSet.insert(f.get());
    }

    std::set<Face*> visited;
    std::vector<std::vector<FacePtr>> components;

    for (const auto& startFace : faceList) {
        if (!startFace || visited.find(startFace.get()) != visited.end()) continue;

        // BFS to find connected component
        std::vector<FacePtr> component;
        std::queue<FacePtr> queue;
        queue.push(startFace);

        while (!queue.empty()) {
            auto current = queue.front();
            queue.pop();

            if (!current || visited.find(current.get()) != visited.end()) continue;
            visited.insert(current.get());
            component.push_back(current);

            // Add adjacent faces that are in the face set
            if (current->halfEdge) {
                auto edge = current->halfEdge;
                do {
                    auto twinEdge = edge->getTwin();
                    if (twinEdge) {
                        auto neighborFace = twinEdge->getFace();
                        if (neighborFace &&
                            faceSet.find(neighborFace.get()) != faceSet.end() &&
                            visited.find(neighborFace.get()) == visited.end()) {
                            queue.push(neighborFace);
                        }
                    }
                    edge = edge->next;
                } while (edge && edge != current->halfEdge);
            }
        }

        if (!component.empty()) {
            components.push_back(std::move(component));
        }
    }

    return components;
}

// =============================================================================
// Edge Operations
// =============================================================================

CollapseResult DCEL::collapseEdge(const HalfEdgePtr& edge) {
    CollapseResult result;
    if (!edge || !edge->origin || !edge->destination()) {
        return result;
    }

    auto v1 = edge->origin;
    auto v2 = edge->destination();

    // Compute midpoint and update v1's position (shared PointPtr mutation!)
    Point midpoint = Point::midpoint(*v1->point, *v2->point);
    v1->point->x = midpoint.x;
    v1->point->y = midpoint.y;

    result.vertex = v1;

    // Move all v2's edges to v1
    for (auto& weakEdge : v2->edges) {
        if (auto e = weakEdge.lock()) {
            e->origin = v1;
            v1->addEdge(e);
        }
    }
    v2->edges.clear();

    // Remove the collapsed edge from face cycles
    auto edgePrev = edge->getPrev();
    if (edgePrev) edgePrev->next = edge->next;
    if (edge->next) edge->next->prev = edgePrev;

    // Update face.halfEdge if it pointed to the collapsed edge
    auto edgeFace = edge->getFace();
    if (edgeFace && edgeFace->halfEdge == edge) {
        edgeFace->halfEdge = (edge->next != edge) ? edge->next : nullptr;
    }

    // Collect affected edges for face shape updates
    if (edgeFace && edge->next) {
        result.affectedEdges.push_back(edge->next);
    }

    // Handle twin edge if it exists
    auto twinEdge = edge->getTwin();
    if (twinEdge) {
        auto twinPrev = twinEdge->getPrev();
        if (twinPrev) twinPrev->next = twinEdge->next;
        if (twinEdge->next) twinEdge->next->prev = twinPrev;

        auto twinFace = twinEdge->getFace();
        if (twinFace && twinFace->halfEdge == twinEdge) {
            twinFace->halfEdge = (twinEdge->next != twinEdge) ? twinEdge->next : nullptr;
        }

        if (twinFace && twinEdge->next) {
            result.affectedEdges.push_back(twinEdge->next);
        }
    }

    // Clean up v1's edge list (remove expired references)
    v1->cleanupEdges();

    // Remove v2 from vertices map
    for (auto it = vertices.begin(); it != vertices.end(); ++it) {
        if (it->second == v2) {
            vertices.erase(it);
            break;
        }
    }

    // Remove collapsed edges from the edges vector
    edges.erase(
        std::remove_if(edges.begin(), edges.end(),
            [&edge, &twinEdge](const HalfEdgePtr& e) {
                return e == edge || e == twinEdge;
            }),
        edges.end()
    );

    return result;
}

VertexPtr DCEL::splitEdge(const HalfEdgePtr& edge) {
    if (!edge || !edge->origin || !edge->destination()) {
        return nullptr;
    }

    // Compute midpoint
    Point midpoint = Point::midpoint(*edge->origin->point, *edge->destination()->point);
    PointPtr midpointPtr = makePoint(midpoint);

    // Create new vertex
    auto newVertex = std::make_shared<Vertex>(midpointPtr);
    vertices[midpointPtr] = newVertex;

    // Create new edge from midpoint to destination
    auto newEdge = std::make_shared<HalfEdge>();

    newEdge->origin = newVertex;
    newEdge->face = edge->face;
    newEdge->next = edge->next;
    newEdge->prev = edge;

    if (edge->next) edge->next->prev = newEdge;
    edge->next = newEdge;

    newVertex->addEdge(newEdge);
    edges.push_back(newEdge);

    // Handle twin edge if it exists
    auto twinEdge = edge->getTwin();
    if (twinEdge) {
        auto newTwin = std::make_shared<HalfEdge>();

        newTwin->origin = newVertex;
        newTwin->face = twinEdge->face;
        newTwin->twin = edge;
        twinEdge->twin = newEdge;
        newEdge->twin = twinEdge;
        edge->twin = newTwin;

        newTwin->next = twinEdge->next;
        newTwin->prev = twinEdge;
        if (twinEdge->next) twinEdge->next->prev = newTwin;
        twinEdge->next = newTwin;

        newVertex->addEdge(newTwin);
        edges.push_back(newTwin);
    }

    return newVertex;
}

std::vector<HalfEdgePtr> DCEL::vertices2chain(const std::vector<VertexPtr>& verts) {
    std::vector<HalfEdgePtr> chain;
    if (verts.size() < 2) return chain;

    for (size_t i = 0; i < verts.size() - 1; ++i) {
        auto edge = findEdge(verts[i], verts[i + 1]);
        if (edge) {
            chain.push_back(edge);
        }
    }

    return chain;
}

HalfEdgePtr DCEL::findEdge(const VertexPtr& from, const VertexPtr& to) {
    if (!from || !to) return nullptr;

    for (const auto& weakEdge : from->edges) {
        if (auto edge = weakEdge.lock()) {
            if (edge->destination() == to) {
                return edge;
            }
        }
    }
    return nullptr;
}

// =============================================================================
// EdgeChain Implementation
// =============================================================================

std::vector<Point> EdgeChain::toPoly(const std::vector<HalfEdgePtr>& chain) {
    std::vector<Point> poly;
    poly.reserve(chain.size());

    for (const auto& edge : chain) {
        if (edge && edge->origin) {
            poly.push_back(*edge->origin->point);
        }
    }

    return poly;
}

std::vector<Point> EdgeChain::toPolyline(const std::vector<HalfEdgePtr>& chain) {
    std::vector<Point> poly;
    if (chain.empty()) return poly;

    poly.reserve(chain.size() + 1);

    for (const auto& edge : chain) {
        if (edge && edge->origin) {
            poly.push_back(*edge->origin->point);
        }
    }

    // Add last point (destination of final edge)
    if (!chain.empty() && chain.back()) {
        auto dest = chain.back()->destination();
        if (dest) {
            poly.push_back(*dest->point);
        }
    }

    return poly;
}

std::vector<PointPtr> EdgeChain::toPolyPtrs(const std::vector<HalfEdgePtr>& chain) {
    std::vector<PointPtr> poly;
    poly.reserve(chain.size());

    for (const auto& edge : chain) {
        if (edge && edge->origin) {
            poly.push_back(edge->origin->point);
        }
    }

    return poly;
}

std::vector<VertexPtr> EdgeChain::vertices(const std::vector<HalfEdgePtr>& chain) {
    std::vector<VertexPtr> verts;
    verts.reserve(chain.size());

    for (const auto& edge : chain) {
        if (edge) {
            verts.push_back(edge->origin);
        }
    }

    return verts;
}

HalfEdgePtr EdgeChain::edgeByOrigin(const std::vector<HalfEdgePtr>& chain, const VertexPtr& vertex) {
    for (const auto& edge : chain) {
        if (edge && edge->origin == vertex) {
            return edge;
        }
    }
    return nullptr;
}

HalfEdgePtr EdgeChain::edgeByOriginPoint(const std::vector<HalfEdgePtr>& chain, const PointPtr& point) {
    for (const auto& edge : chain) {
        if (edge && edge->origin && edge->origin->point == point) {
            return edge;
        }
    }
    return nullptr;
}

double EdgeChain::length(const std::vector<HalfEdgePtr>& chain) {
    double total = 0.0;
    for (const auto& edge : chain) {
        if (edge) {
            total += edge->length();
        }
    }
    return total;
}

bool EdgeChain::isClosed(const std::vector<HalfEdgePtr>& chain) {
    if (chain.size() < 2) return false;

    const auto& first = chain.front();
    const auto& last = chain.back();

    if (!first || !last) return false;

    return last->destination() == first->origin;
}

std::vector<HalfEdgePtr> EdgeChain::reverse(const std::vector<HalfEdgePtr>& chain) {
    std::vector<HalfEdgePtr> reversed;
    reversed.reserve(chain.size());

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto& edge = *it;
        if (!edge) return {};

        auto twin = edge->getTwin();
        if (!twin) {
            // Can't reverse without twins
            return {};
        }
        reversed.push_back(twin);
    }

    return reversed;
}

} // namespace geom
} // namespace town_generator
