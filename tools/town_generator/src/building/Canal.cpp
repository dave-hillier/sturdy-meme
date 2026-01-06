#include "town_generator/building/Canal.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Patch.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

// ============================================================================
// CanalTopology implementation (faithful to mfcg.js gh class)
// ============================================================================

void CanalTopology::build(Model* model) {
    // Build graph from non-water patches (faithful to mfcg.js buildTopology line 10378-10381)
    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;

        // Process each edge of the patch
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr v0 = patch->shape.ptr(i);
            geom::PointPtr v1 = patch->shape.ptr((i + 1) % patch->shape.length());

            geom::Node* n0 = getOrCreateNode(v0);
            geom::Node* n1 = getOrCreateNode(v1);

            if (n0 && n1) {
                double dist = geom::Point::distance(*v0, *v1);
                n0->link(n1, dist);
            }
        }
    }
}

geom::Node* CanalTopology::getOrCreateNode(const geom::PointPtr& pt) {
    auto it = pt2node.find(pt);
    if (it != pt2node.end()) {
        return it->second;
    }

    geom::Node* node = graph.add();
    pt2node[pt] = node;
    node2pt[node] = pt;
    return node;
}

void CanalTopology::excludePolygon(const std::vector<geom::PointPtr>& polygon) {
    // Exclude all points on the polygon (like mfcg.js excludePolygon)
    for (const auto& pt : polygon) {
        excludedPoints_.insert(pt);

        // Unlink the node from the graph
        auto it = pt2node.find(pt);
        if (it != pt2node.end() && it->second) {
            it->second->unlinkAll();
        }
    }
}

void CanalTopology::excludePoints(const std::vector<geom::PointPtr>& points) {
    // Exclude specific points (like mfcg.js excludePoints)
    for (const auto& pt : points) {
        excludedPoints_.insert(pt);

        auto it = pt2node.find(pt);
        if (it != pt2node.end() && it->second) {
            it->second->unlinkAll();
        }
    }
}

std::vector<geom::PointPtr> CanalTopology::buildPath(const geom::PointPtr& from, const geom::PointPtr& to) {
    auto fromIt = pt2node.find(from);
    auto toIt = pt2node.find(to);

    if (fromIt == pt2node.end() || toIt == pt2node.end()) {
        return {};
    }

    // Check if either endpoint is excluded
    if (excludedPoints_.count(from) || excludedPoints_.count(to)) {
        return {};
    }

    auto path = graph.aStar(fromIt->second, toIt->second, nullptr);
    if (path.empty()) {
        return {};
    }

    std::vector<geom::PointPtr> result;
    for (auto* node : path) {
        auto it = node2pt.find(node);
        if (it != node2pt.end()) {
            result.push_back(it->second);
        }
    }

    return result;
}

// ============================================================================
// Canal implementation (faithful to mfcg.js yb class)
// ============================================================================

std::unique_ptr<Canal> Canal::createRiver(Model* model) {
    if (!model) return nullptr;

    // Build topology for river pathfinding (faithful to mfcg.js yb.buildTopology)
    CanalTopology topology;
    topology.build(model);

    // Exclude wall vertices (except gates) - mfcg.js line 10384
    if (model->wall) {
        std::vector<geom::PointPtr> wallPts;
        for (const auto& pt : model->wall->shape) {
            // Don't exclude gates
            bool isGate = false;
            for (const auto& gate : model->gates) {
                if (gate == pt) {
                    isGate = true;
                    break;
                }
            }
            if (!isGate) {
                wallPts.push_back(pt);
            }
        }
        topology.excludePolygon(wallPts);
    }

    // Exclude citadel wall vertices - mfcg.js line 10385
    if (model->citadel) {
        std::vector<geom::PointPtr> citadelPts;
        for (const auto& pt : model->citadel->shape) {
            citadelPts.push_back(pt);
        }
        topology.excludePoints(citadelPts);
    }

    // Exclude gates - mfcg.js line 10386
    topology.excludePoints(model->gates);

    // Exclude artery vertices - mfcg.js line 10387-10388
    for (const auto& artery : model->arteries) {
        topology.excludePolygon(artery);
    }

    // Build river course
    std::vector<geom::PointPtr> coursePtrs;

    // Use deltaRiver for coastal cities, regularRiver otherwise
    // (mfcg.js line 10243: 0 < a.shoreE.length ? yb.deltaRiver(a) : yb.regularRiver(a))
    if (!model->shore.empty()) {
        SDL_Log("Canal: Using deltaRiver (coastal city, shore has %zu vertices)", model->shore.length());
        coursePtrs = deltaRiver(model, topology);
    } else {
        SDL_Log("Canal: Using regularRiver (non-coastal city)");
        coursePtrs = regularRiver(model, topology);
    }

    if (coursePtrs.empty()) {
        SDL_Log("Canal: Failed to build river course");
        return nullptr;
    }

    // Validate the course
    if (!validateCourse(model, coursePtrs)) {
        SDL_Log("Canal: Course validation failed");
        return nullptr;
    }

    // Create the canal
    auto canal = std::make_unique<Canal>();
    canal->model = model;
    canal->coursePtr = coursePtrs;

    // Convert to point vector for compatibility
    for (const auto& pt : coursePtrs) {
        canal->course.push_back(*pt);
    }

    // Smooth the shore entry point if coastal (mfcg.js constructor lines 10206-10212)
    if (!model->waterEdge.empty() && !canal->course.empty()) {
        // Lerp first point toward second point
        if (canal->course.size() >= 2) {
            geom::Point& d = canal->course[0];
            geom::Point lerped = geom::GeomUtils::lerp(d, canal->course[1], 0.5);
            d = lerped;
        }
    }

    // Smooth the course (mfcg.js line 10214)
    canal->smoothCourse(1);

    // Update state (calculate width, bridges, gates)
    canal->updateState();

    SDL_Log("Canal: Created river with %zu points, width %.1f",
            canal->course.size(), canal->width);

    return canal;
}

std::vector<geom::PointPtr> Canal::deltaRiver(Model* model, CanalTopology& topology) {
    // Faithful to mfcg.js yb.deltaRiver (lines 10292-10333)

    // Find shore vertices where multiple non-water cells meet
    // These are vertices on land patches that are shared with water patches
    // and also shared between multiple land patches (junction points)
    std::vector<geom::PointPtr> shoreVertices;

    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;  // Only look at land patches

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr v = patch->shape.ptr(i);

            // Check if already found
            bool alreadyFound = false;
            for (const auto& sv : shoreVertices) {
                if (sv == v) {
                    alreadyFound = true;
                    break;
                }
            }
            if (alreadyFound) continue;

            // Count land and water patches sharing this vertex
            int landCount = 0;
            bool bordersWater = false;
            for (auto* other : model->patches) {
                if (other->shape.containsPtr(v)) {
                    if (other->waterbody) {
                        bordersWater = true;
                    } else {
                        landCount++;
                    }
                }
            }

            // Need to border water AND be shared by multiple land patches
            // (like mfcg.js: 1 < Z.count(cellsByVertex, !a.waterbody))
            if (bordersWater && landCount > 1) {
                shoreVertices.push_back(v);
            }
        }
    }

    if (shoreVertices.empty()) {
        SDL_Log("Canal: No valid shore vertices found");
        return {};
    }

    SDL_Log("Canal: Found %zu shore junction vertices", shoreVertices.size());

    // Sort by distance from origin (mfcg.js line 10300-10302)
    std::sort(shoreVertices.begin(), shoreVertices.end(), [](const geom::PointPtr& a, const geom::PointPtr& b) {
        return a->length() < b->length();
    });

    // Find endpoint vertices on earthEdge that are NOT on shore
    // These are vertices shared by land patches but not bordering water
    std::vector<geom::PointPtr> earthVertices;
    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr v = patch->shape.ptr(i);

            // Skip if on shore (already found)
            bool isOnShore = false;
            for (const auto& sv : shoreVertices) {
                if (sv == v) {
                    isOnShore = true;
                    break;
                }
            }
            if (isOnShore) continue;

            // Skip if already found
            bool alreadyFound = false;
            for (const auto& ev : earthVertices) {
                if (ev == v) {
                    alreadyFound = true;
                    break;
                }
            }
            if (alreadyFound) continue;

            // Must be shared by multiple land patches
            int landCount = 0;
            for (auto* other : model->patches) {
                if (!other->waterbody && other->shape.containsPtr(v)) {
                    landCount++;
                }
            }

            if (landCount > 1) {
                earthVertices.push_back(v);
            }
        }
    }

    if (earthVertices.empty()) {
        SDL_Log("Canal: No valid earth vertices found");
        return {};
    }

    SDL_Log("Canal: Found %zu earth junction vertices", earthVertices.size());

    // Try multiple shore vertices and find the longest valid path
    std::vector<geom::PointPtr> bestPath;
    double bestPathLength = 0;

    // Try each shore vertex (sorted by distance from center, try closest first)
    int attempts = 0;
    const int maxAttempts = 20;  // Don't try all candidates if there are many

    for (const auto& shoreV : shoreVertices) {
        if (attempts++ >= maxAttempts) break;

        // Try multiple earth vertices for each shore vertex
        // Sort by distance from shore to find furthest endpoints
        std::vector<std::pair<double, geom::PointPtr>> earthWithDist;
        for (const auto& earthV : earthVertices) {
            double dist = geom::Point::distance(*earthV, *shoreV);
            earthWithDist.push_back({dist, earthV});
        }
        std::sort(earthWithDist.begin(), earthWithDist.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });  // Furthest first

        // Try up to 5 earth vertices per shore vertex
        int earthAttempts = 0;
        for (const auto& [dist, earthV] : earthWithDist) {
            if (earthAttempts++ >= 5) break;

            // Build path from earth vertex to shore
            auto path = topology.buildPath(earthV, shoreV);

            if (!path.empty()) {
                // Calculate path length
                double pathLength = 0;
                for (size_t i = 1; i < path.size(); ++i) {
                    pathLength += geom::Point::distance(*path[i-1], *path[i]);
                }

                if (pathLength > bestPathLength) {
                    bestPath = path;
                    bestPathLength = pathLength;
                    SDL_Log("Canal: deltaRiver found path with %zu vertices, length %.1f",
                            path.size(), pathLength);
                }
            }
        }
    }

    if (!bestPath.empty()) {
        SDL_Log("Canal: deltaRiver returning best path with %zu vertices, length %.1f",
                bestPath.size(), bestPathLength);
    }

    return bestPath;
}

std::vector<geom::PointPtr> Canal::regularRiver(Model* model, CanalTopology& topology) {
    // Faithful to mfcg.js yb.regularRiver (lines 10248-10290)
    // For non-coastal cities, find path from horizon through center to horizon

    // Get horizon vertices that are shared by multiple cells
    // mfcg.js lines 10249-10253
    std::vector<geom::PointPtr> horizonVertices;
    for (size_t i = 0; i < model->earthEdge.length(); ++i) {
        geom::PointPtr v = model->earthEdge.ptr(i);

        int cellCount = 0;
        for (auto* patch : model->patches) {
            if (patch->shape.containsPtr(v)) {
                cellCount++;
            }
        }

        if (cellCount > 1) {
            horizonVertices.push_back(v);
        }
    }

    if (horizonVertices.size() < 2) {
        SDL_Log("Canal: regularRiver needs at least 2 horizon vertices");
        return {};
    }

    // Try pairs of horizon vertices
    std::vector<geom::PointPtr> remaining = horizonVertices;

    while (remaining.size() > 1) {
        // Pick a random vertex
        int idx = utils::Random::intVal(0, static_cast<int>(remaining.size()));
        geom::PointPtr k = remaining[idx];

        // Find the most "opposite" vertex (smallest dot product)
        geom::PointPtr n = nullptr;
        double minDot = std::numeric_limits<double>::infinity();

        for (const auto& h : remaining) {
            if (h == k) continue;

            geom::Point hNorm = h->norm(1);
            double dot = k->x * hNorm.x + k->y * hNorm.y;

            if (dot < minDot) {
                minDot = dot;
                n = h;
            }
        }

        if (!n) break;

        // Find center vertex (closest to origin among inner patches)
        geom::PointPtr centerV = nullptr;
        double minCenterDist = std::numeric_limits<double>::infinity();

        for (auto* patch : model->inner) {
            for (size_t i = 0; i < patch->shape.length(); ++i) {
                geom::PointPtr v = patch->shape.ptr(i);
                double dist = v->length();
                if (dist < minCenterDist) {
                    minCenterDist = dist;
                    centerV = v;
                }
            }
        }

        if (!centerV) break;

        // Build path: n -> center -> k
        auto path1 = topology.buildPath(n, centerV);
        if (path1.empty()) {
            remaining.erase(std::remove(remaining.begin(), remaining.end(), k), remaining.end());
            remaining.erase(std::remove(remaining.begin(), remaining.end(), n), remaining.end());
            continue;
        }

        auto path2 = topology.buildPath(centerV, k);
        if (path2.empty()) {
            remaining.erase(std::remove(remaining.begin(), remaining.end(), k), remaining.end());
            remaining.erase(std::remove(remaining.begin(), remaining.end(), n), remaining.end());
            continue;
        }

        // Combine paths at intersection point (mfcg.js lines 10271-10278)
        for (size_t i = 0; i < path2.size(); ++i) {
            auto it = std::find(path1.begin(), path1.end(), path2[i]);
            if (it != path1.end()) {
                // Found intersection - combine paths
                std::vector<geom::PointPtr> combined;

                // path2[0..i] + path1[intersection..]
                for (size_t j = 0; j < i; ++j) {
                    combined.push_back(path2[j]);
                }
                for (auto jt = it; jt != path1.end(); ++jt) {
                    combined.push_back(*jt);
                }

                if (!combined.empty()) {
                    SDL_Log("Canal: regularRiver found path with %zu vertices", combined.size());
                    return combined;
                }
                break;
            }
        }

        // Discard and try again
        remaining.erase(std::remove(remaining.begin(), remaining.end(), k), remaining.end());
        remaining.erase(std::remove(remaining.begin(), remaining.end(), n), remaining.end());
    }

    return {};
}

bool Canal::validateCourse(Model* model, const std::vector<geom::PointPtr>& coursePtrs) {
    // Faithful to mfcg.js yb.validateCourse (lines 10335-10364)

    if (coursePtrs.empty()) return false;

    // Need at least 3 points for a valid path
    if (coursePtrs.size() < 3) {
        SDL_Log("Canal: Course too short (%zu vertices)", coursePtrs.size());
        return false;
    }

    // Calculate total path length
    double pathLength = 0;
    for (size_t i = 1; i < coursePtrs.size(); ++i) {
        pathLength += geom::Point::distance(*coursePtrs[i-1], *coursePtrs[i]);
    }

    // Path should span a reasonable distance
    // (more lenient than MFCG's vertex count check, which assumes DCEL granularity)
    // Use 5% of earth perimeter as minimum
    double minPathLength = model->earthEdge.perimeter() / 20.0;
    if (pathLength < minPathLength) {
        SDL_Log("Canal: Course too short (length %.1f < %.1f)", pathLength, minPathLength);
        return false;
    }

    // Middle vertices should not be on shore
    // (Using coordinate proximity check since shore is smoothed)
    for (size_t i = 1; i + 1 < coursePtrs.size(); ++i) {
        for (size_t j = 0; j < model->shore.length(); ++j) {
            if (geom::Point::distance(*coursePtrs[i], model->shore[j]) < 1.0) {
                SDL_Log("Canal: Course vertex %zu is too close to shore", i);
                return false;
            }
        }
    }

    SDL_Log("Canal: Validated course with %zu vertices, length %.1f", coursePtrs.size(), pathLength);
    return true;
}

void Canal::updateState() {
    // Faithful to mfcg.js yb.updateState (lines 10391-10448)

    if (!model || course.empty()) return;

    // Calculate width based on inner city size (mfcg.js line 10425)
    // width = (3 + inner.length / 5) * (0.8 + random * 0.4) * (rural ? 1.5 : 1)
    double baseWidth = 3.0 + model->inner.size() / 5.0;
    double variation = 0.8 + utils::Random::floatVal() * 0.4;
    width = baseWidth * variation * (rural ? 1.5 : 1.0);

    // Find bridges at street crossings
    findBridges();

    // Check if canal is rural (doesn't pass through inner city)
    // mfcg.js lines 10416-10424
    rural = true;
    for (size_t i = 2; i + 1 < coursePtr.size(); ++i) {
        // Check if this vertex is in inner city
        for (auto* patch : model->inner) {
            if (patch->shape.containsPtr(coursePtr[i])) {
                rural = false;
                break;
            }
        }
        if (!rural) break;
    }
}

void Canal::findBridges() {
    if (!model || course.size() < 2) return;

    bridges.clear();

    // Find street crossings (faithful to mfcg.js updateState bridge logic)
    for (const auto& artery : model->arteries) {
        if (artery.size() < 2) continue;

        for (size_t i = 0; i + 1 < artery.size(); ++i) {
            geom::Point streetP1 = *artery[i];
            geom::Point streetP2 = *artery[i + 1];

            // Check if this street segment crosses any canal segment
            for (size_t j = 0; j + 1 < course.size(); ++j) {
                const geom::Point& canalP1 = course[j];
                const geom::Point& canalP2 = course[j + 1];

                // Check for intersection
                auto intersection = geom::GeomUtils::intersectLines(
                    canalP1.x, canalP1.y,
                    canalP2.x - canalP1.x, canalP2.y - canalP1.y,
                    streetP1.x, streetP1.y,
                    streetP2.x - streetP1.x, streetP2.y - streetP1.y
                );

                if (intersection) {
                    double t1 = intersection->x;
                    double t2 = intersection->y;

                    // Check if intersection is within both segments
                    if (t1 >= 0 && t1 <= 1 && t2 >= 0 && t2 <= 1) {
                        geom::Point bridgePoint(
                            canalP1.x + t1 * (canalP2.x - canalP1.x),
                            canalP1.y + t1 * (canalP2.y - canalP1.y)
                        );
                        geom::Point streetDir = streetP2.subtract(streetP1).norm(1.0);
                        bridges[bridgePoint] = streetDir;
                    }
                }
            }
        }
    }

    SDL_Log("Canal: Found %zu bridges", bridges.size());
}

void Canal::smoothCourse(int iterations) {
    if (course.size() < 3) return;

    // Use Polygon::smoothOpen to smooth the course path
    course = geom::Polygon::smoothOpen(course, nullptr, iterations);

    // Also update coursePtr if we have it
    // Note: This breaks the reference semantics but is necessary for smoothing
    coursePtr.clear();
    for (const auto& pt : course) {
        coursePtr.push_back(geom::makePoint(pt));
    }
}

geom::Polygon Canal::getWaterPolygon() const {
    if (course.size() < 2) {
        return geom::Polygon();
    }

    // Create a polygon by offsetting the course on both sides
    std::vector<geom::Point> leftSide;
    std::vector<geom::Point> rightSide;

    double halfWidth = width / 2.0;

    for (size_t i = 0; i < course.size(); ++i) {
        geom::Point dir;
        if (i == 0) {
            dir = course[1].subtract(course[0]);
        } else if (i == course.size() - 1) {
            dir = course[i].subtract(course[i - 1]);
        } else {
            dir = course[i + 1].subtract(course[i - 1]);
        }

        double len = dir.length();
        if (len < 0.001) continue;

        geom::Point perpDir(-dir.y / len, dir.x / len);

        leftSide.push_back(course[i].add(perpDir.scale(halfWidth)));
        rightSide.push_back(course[i].add(perpDir.scale(-halfWidth)));
    }

    // Combine into polygon (left side forward, right side backward)
    std::vector<geom::Point> polyPoints;
    for (const auto& p : leftSide) {
        polyPoints.push_back(p);
    }
    for (auto it = rightSide.rbegin(); it != rightSide.rend(); ++it) {
        polyPoints.push_back(*it);
    }

    return geom::Polygon(polyPoints);
}

bool Canal::containsVertex(const geom::Point& v, double tolerance) const {
    for (const auto& cp : course) {
        if (geom::Point::distance(cp, v) < tolerance) {
            return true;
        }
    }
    return false;
}

bool Canal::containsEdge(const geom::Point& v0, const geom::Point& v1, double tolerance) const {
    // Check if both vertices are on the canal course, and they are adjacent
    int idx0 = -1, idx1 = -1;

    for (size_t i = 0; i < course.size(); ++i) {
        if (geom::Point::distance(course[i], v0) < tolerance) {
            idx0 = static_cast<int>(i);
        }
        if (geom::Point::distance(course[i], v1) < tolerance) {
            idx1 = static_cast<int>(i);
        }
    }

    if (idx0 == -1 || idx1 == -1) return false;

    // Check if they are adjacent (in either direction)
    return std::abs(idx0 - idx1) == 1;
}

double Canal::getWidthAtVertex(const geom::Point& v, double tolerance) const {
    if (containsVertex(v, tolerance)) {
        return width;
    }
    return 0.0;
}

} // namespace building
} // namespace town_generator
