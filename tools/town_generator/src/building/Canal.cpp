#include "town_generator/building/Canal.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Patch.h"
#include "town_generator/building/Topology.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

std::unique_ptr<Canal> Canal::createRiver(Model* model) {
    if (!model) return nullptr;

    auto canal = std::make_unique<Canal>();
    canal->model = model;

    // River width based on city size
    canal->width = 3.0 + utils::Random::floatVal() * 3.0;

    // For delta river (coastal cities), find shore vertices and build path to opposite edge
    // Shore vertices are on the boundary between water and land patches

    // Find vertices on the shore (shared between water and land patches)
    std::vector<geom::PointPtr> shoreVertices;
    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;

        // Check each vertex of land patch
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr vPtr = patch->shape.ptr(i);

            // Check if this vertex is shared with a water patch
            bool bordersWater = false;
            int landNeighborCount = 0;
            for (auto* neighbor : patch->neighbors) {
                if (neighbor->shape.containsPtr(vPtr)) {
                    if (neighbor->waterbody) {
                        bordersWater = true;
                    } else {
                        landNeighborCount++;
                    }
                }
            }

            // A good shore vertex borders water AND is shared by multiple land patches
            // (this ensures it's a junction point on the Voronoi graph)
            if (bordersWater && landNeighborCount >= 1) {
                // Avoid duplicates
                bool found = false;
                for (const auto& sv : shoreVertices) {
                    if (sv == vPtr) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    shoreVertices.push_back(vPtr);
                }
            }
        }
    }

    if (shoreVertices.empty()) {
        SDL_Log("Canal: No shore vertices found");
        return nullptr;
    }

    // Filter to vertices that are actually on the shore circumference
    // This ensures the river starts from the actual coastline, not one patch inland
    std::vector<geom::PointPtr> validShoreVertices;
    for (const auto& sv : shoreVertices) {
        if (model->shore.containsPtr(sv)) {
            validShoreVertices.push_back(sv);
        }
    }

    // Fall back to all shore vertices if filtering removed everything
    if (validShoreVertices.empty()) {
        SDL_Log("Canal: No vertices on shore circumference, using all %zu shore vertices", shoreVertices.size());
        validShoreVertices = shoreVertices;
    } else {
        SDL_Log("Canal: Filtered to %zu vertices on shore circumference (from %zu)",
                validShoreVertices.size(), shoreVertices.size());
        shoreVertices = validShoreVertices;
    }

    // Sort shore vertices by distance from center (prefer vertices near city center for river entry)
    std::sort(shoreVertices.begin(), shoreVertices.end(),
        [](const geom::PointPtr& a, const geom::PointPtr& b) {
            return a->length() < b->length();
        });

    // Find vertices on the opposite edge (horizon) - vertices that are far from water
    // and on the outer boundary of the city
    std::vector<geom::PointPtr> horizonVertices;
    for (auto* patch : model->patches) {
        if (patch->waterbody || patch->withinCity) continue;  // Only outer land patches

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr vPtr = patch->shape.ptr(i);

            // Check if this is an outer boundary vertex (few neighbors)
            int neighborCount = 0;
            bool isShore = false;
            for (auto* neighbor : patch->neighbors) {
                if (neighbor->shape.containsPtr(vPtr)) {
                    neighborCount++;
                    if (neighbor->waterbody) isShore = true;
                }
            }

            // Skip if it's a shore vertex
            if (isShore) continue;

            // Vertices with few neighbors might be on outer edge
            if (neighborCount <= 2) {
                bool found = false;
                for (const auto& hv : horizonVertices) {
                    if (hv == vPtr) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    horizonVertices.push_back(vPtr);
                }
            }
        }
    }

    // If no horizon vertices found, try finding outer vertices from all land patches
    if (horizonVertices.empty()) {
        for (auto* patch : model->patches) {
            if (patch->waterbody) continue;

            for (size_t i = 0; i < patch->shape.length(); ++i) {
                geom::PointPtr vPtr = patch->shape.ptr(i);

                // Check distance from origin - far vertices are likely on horizon
                double dist = vPtr->length();
                if (dist > 400) {  // Far from center
                    bool isShore = false;
                    for (const auto& sv : shoreVertices) {
                        if (sv == vPtr) {
                            isShore = true;
                            break;
                        }
                    }
                    if (!isShore) {
                        bool found = false;
                        for (const auto& hv : horizonVertices) {
                            if (hv == vPtr) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            horizonVertices.push_back(vPtr);
                        }
                    }
                }
            }
        }
    }

    SDL_Log("Canal: Found %zu shore vertices, %zu horizon vertices",
            shoreVertices.size(), horizonVertices.size());

    if (horizonVertices.empty()) {
        SDL_Log("Canal: No horizon vertices found, using simple path");
        // Fallback to simple path from shore toward opposite direction
        if (shoreVertices.empty()) return nullptr;

        geom::Point start = *shoreVertices[0];
        geom::Point center(0, 0);
        geom::Point dir = center.subtract(start).norm(300);
        geom::Point end = start.add(dir);

        canal->buildCourse(start, end);
        if (!canal->course.empty()) {
            canal->smoothCourse(2);
            canal->findBridges();
            return canal;
        }
        return nullptr;
    }

    // Pick a shore vertex near the city center
    geom::PointPtr startVertex = shoreVertices[0];

    // Find the best horizon vertex - perpendicular to the shore direction
    // Compute shore direction (tangent) at the start vertex
    geom::Point shoreDir(0, 1);  // Default
    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            if (patch->shape.ptr(i) == startVertex) {
                size_t prev = (i == 0) ? patch->shape.length() - 1 : i - 1;
                size_t next = (i + 1) % patch->shape.length();
                shoreDir = patch->shape[next].subtract(patch->shape[prev]).norm(1);
                break;
            }
        }
    }

    // Perpendicular to shore (inland direction)
    geom::Point inlandDir(-shoreDir.y, shoreDir.x);

    // Find horizon vertex that is:
    // 1. Somewhat aligned with inland direction (dot product > 0)
    // 2. Far enough from start vertex (to make a proper river)
    geom::PointPtr endVertex = nullptr;
    double bestScore = -1000;
    for (const auto& hv : horizonVertices) {
        double dist = geom::Point::distance(*hv, *startVertex);
        if (dist < 200) continue;  // Must be at least 200 units away

        geom::Point toHorizon = hv->subtract(*startVertex).norm(1);
        double dotProduct = toHorizon.x * inlandDir.x + toHorizon.y * inlandDir.y;

        // Score combines alignment with distance
        // Prefer far points that are roughly in the inland direction
        double score = dotProduct * 0.5 + dist * 0.01;
        if (score > bestScore) {
            bestScore = score;
            endVertex = hv;
        }
    }

    if (!endVertex) {
        SDL_Log("Canal: No suitable end vertex found");
        return nullptr;
    }

    double dist = geom::Point::distance(*startVertex, *endVertex);
    SDL_Log("Canal: River from (%.0f, %.0f) to (%.0f, %.0f), distance %.0f",
            startVertex->x, startVertex->y, endVertex->x, endVertex->y, dist);

    // Build course by following patch edges
    canal->buildCourseAlongEdges(startVertex, endVertex);

    if (canal->course.empty()) {
        SDL_Log("Canal: Failed to build course");
        return nullptr;
    }

    // Smooth the canal course (faithful to mfcg.js - rivers are smoothed)
    canal->smoothCourse(2);

    SDL_Log("Canal: Course has %zu points (smoothed)", canal->course.size());
    canal->findBridges();

    return canal;
}

void Canal::buildCourse(const geom::Point& start, const geom::Point& end) {
    course.clear();

    // Simple straight canal with some waviness
    course.push_back(start);

    geom::Point dir = end.subtract(start);
    double dist = dir.length();
    if (dist < 1) {
        course.push_back(end);
        return;
    }

    // Add intermediate points with some waviness
    int numPoints = static_cast<int>(dist / 20.0);
    if (numPoints < 1) numPoints = 1;
    if (numPoints > 10) numPoints = 10;

    geom::Point perpDir(-dir.y / dist, dir.x / dist);

    for (int i = 1; i < numPoints; ++i) {
        double t = static_cast<double>(i) / numPoints;
        geom::Point basePoint = start.add(dir.scale(t));

        // Add some waviness
        double waveAmplitude = width * 2.0 * (utils::Random::floatVal() - 0.5);
        geom::Point waveOffset = perpDir.scale(waveAmplitude);

        course.push_back(basePoint.add(waveOffset));
    }

    course.push_back(end);
}

void Canal::buildCourseAlongEdges(const geom::PointPtr& start, const geom::PointPtr& end) {
    course.clear();

    // Walk from start toward end, following patch vertices
    course.push_back(*start);

    geom::Point current = *start;
    geom::Point target = *end;

    // Use vector to track visited points (check by coordinate proximity)
    auto isVisited = [](const std::vector<geom::Point>& visited, const geom::Point& p) {
        for (const auto& v : visited) {
            if (geom::Point::distance(v, p) < 0.5) return true;
        }
        return false;
    };
    std::vector<geom::Point> visited;
    visited.push_back(current);

    int maxSteps = 200;

    for (int step = 0; step < maxSteps; ++step) {
        // Find patches containing the current point
        std::vector<Patch*> containingPatches;
        for (auto* patch : model->patches) {
            if (patch->waterbody) continue;
            for (size_t i = 0; i < patch->shape.length(); ++i) {
                if (geom::Point::distance(patch->shape[i], current) < 0.5) {
                    containingPatches.push_back(patch);
                    break;
                }
            }
        }

        if (containingPatches.empty()) break;

        // Find the best next vertex (makes most progress toward target)
        geom::Point bestNext = current;
        double bestProgress = -1000;

        for (auto* patch : containingPatches) {
            for (size_t i = 0; i < patch->shape.length(); ++i) {
                const geom::Point& v = patch->shape[i];
                if (isVisited(visited, v)) continue;

                // Progress = how much closer to target
                double progressToTarget = geom::Point::distance(current, target) -
                                          geom::Point::distance(v, target);

                // Add small random factor to prevent getting stuck
                progressToTarget += utils::Random::floatVal() * 5.0;

                if (progressToTarget > bestProgress) {
                    bestProgress = progressToTarget;
                    bestNext = v;
                }
            }
        }

        if (bestProgress <= -10) break;  // Stuck

        course.push_back(bestNext);
        visited.push_back(bestNext);
        current = bestNext;

        if (geom::Point::distance(current, target) < 5.0) {
            break;  // Close enough
        }
    }

    // Add the final target point
    course.push_back(target);
}

void Canal::findBridges() {
    if (!model || course.size() < 2) return;

    bridges.clear();

    // Find street crossings
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
                        geom::Point streetDir = streetP2.subtract(streetP1);
                        bridges[bridgePoint] = streetDir.norm(1.0);
                    }
                }
            }
        }
    }
}

void Canal::smoothCourse(int iterations) {
    if (course.size() < 3) return;

    // Use Polygon::smoothOpen to smooth the course path
    course = geom::Polygon::smoothOpen(course, nullptr, iterations);
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
    // Check if both vertices are on the canal course, and they are adjacent in the course
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
