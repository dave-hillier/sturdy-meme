#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace town_generator {
namespace building {

CurtainWall::CurtainWall(
    bool real,
    City* model,
    const std::vector<Cell*>& cells,
    const std::vector<geom::PointPtr>& reserved
) : real_(real), patches_(cells) {

    if (cells.size() == 1) {
        shape = cells[0]->shape.copy();  // Shares vertices with patch
    } else {
        shape = City::findCircumference(cells);  // Already shares vertices

        if (real) {
            double smoothFactor = std::min(1.0, 40.0 / cells.size());

            std::vector<geom::Point> smoothed;
            for (const auto& vPtr : shape) {
                // Check if reserved by pointer identity
                bool isReserved = std::find(reserved.begin(), reserved.end(), vPtr) != reserved.end();
                if (isReserved) {
                    smoothed.push_back(*vPtr);
                } else {
                    smoothed.push_back(shape.smoothVertex(*vPtr, smoothFactor));
                }
            }

            // Mutate shared vertices in place
            for (size_t i = 0; i < shape.length(); ++i) {
                shape[i].set(smoothed[i]);
            }
        }
    }

    segments.resize(shape.length(), true);

    buildGates(real, model, reserved);
}

void CurtainWall::buildGates(bool real, City* model, const std::vector<geom::PointPtr>& reserved) {
    gates.clear();

    // Find potential entrance points - store as PointPtr to preserve identity
    std::vector<geom::PointPtr> entrances;

    if (patches_.size() > 1) {
        for (const auto& vPtr : shape) {
            // Check if reserved by pointer identity
            bool isReserved = std::find(reserved.begin(), reserved.end(), vPtr) != reserved.end();
            if (isReserved) continue;

            // Count adjacent inner wards (by pointer identity)
            int adjacentCount = 0;
            for (auto* p : patches_) {
                if (p->shape.containsPtr(vPtr)) {
                    adjacentCount++;
                }
            }

            if (adjacentCount > 1) {
                entrances.push_back(vPtr);
            }
        }
    } else {
        for (const auto& vPtr : shape) {
            bool isReserved = std::find(reserved.begin(), reserved.end(), vPtr) != reserved.end();
            if (!isReserved) {
                entrances.push_back(vPtr);
            }
        }
    }

    // If no entrances found with strict criteria, use all non-reserved vertices
    if (entrances.empty()) {
        for (const auto& vPtr : shape) {
            bool isReserved = std::find(reserved.begin(), reserved.end(), vPtr) != reserved.end();
            if (!isReserved) {
                entrances.push_back(vPtr);
            }
        }
    }

    if (entrances.empty()) {
        // Last resort: just use the first vertex
        if (shape.length() > 0) {
            entrances.push_back(shape.ptr(0));
        } else {
            return;  // Can't create gates on empty shape
        }
    }

    // Select gates
    while (!entrances.empty()) {
        int index = utils::Random::intVal(0, static_cast<int>(entrances.size()));
        geom::PointPtr gatePtr = entrances[index];
        gates.push_back(gatePtr);  // Store the shared pointer

        if (real) {
            // Find outer wards for potential road creation
            std::vector<Cell*> outerWards;
            for (auto* p : model->cells) {
                if (p->shape.containsPtr(gatePtr)) {
                    bool isInner = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
                    if (!isInner) {
                        outerWards.push_back(p);
                    }
                }
            }

            if (outerWards.size() == 1) {
                auto* outer = outerWards[0];
                if (outer->shape.length() > 3) {
                    geom::Point prevGate = shape.prev(*gatePtr);
                    geom::Point nextGate = shape.next(*gatePtr);
                    geom::Point wall = nextGate.subtract(prevGate);
                    geom::Point out(wall.y, -wall.x);

                    // Find farthest point in the direction perpendicular to wall
                    double maxDot = -std::numeric_limits<double>::infinity();
                    geom::PointPtr farthestPtr = outer->shape.ptr(0);

                    for (const auto& vPtr : outer->shape) {
                        bool onShape = shape.containsPtr(vPtr);
                        bool isRes = std::find(reserved.begin(), reserved.end(), vPtr) != reserved.end();

                        if (!onShape && !isRes) {
                            geom::Point dir = vPtr->subtract(*gatePtr);
                            double len = dir.length();
                            if (len > 0.001) {
                                double dot = dir.dot(out) / len;
                                if (dot > maxDot) {
                                    maxDot = dot;
                                    farthestPtr = vPtr;
                                }
                            }
                        }
                    }

                    // Split the outer ward (using splitShared to preserve PointPtrs)
                    auto halves = outer->shape.splitShared(*gatePtr, *farthestPtr);
                    std::vector<Cell*> newPatches;
                    for (const auto& half : halves) {
                        auto* newPatch = new Cell(half);
                        newPatches.push_back(newPatch);
                    }

                    // Replace in model
                    auto it = std::find(model->cells.begin(), model->cells.end(), outer);
                    if (it != model->cells.end()) {
                        *it = newPatches[0];
                        for (size_t i = 1; i < newPatches.size(); ++i) {
                            model->cells.insert(it + i, newPatches[i]);
                        }
                    }
                }
            }
        }

        // Remove neighboring entrances to avoid close gates
        if (index == 0) {
            if (entrances.size() > 2) {
                entrances.erase(entrances.begin(), entrances.begin() + 2);
            } else {
                entrances.clear();
            }
            if (!entrances.empty()) {
                entrances.pop_back();
            }
        } else if (index == static_cast<int>(entrances.size()) - 1) {
            if (index > 0) {
                entrances.erase(entrances.begin() + index - 1, entrances.end());
            }
            if (!entrances.empty()) {
                entrances.erase(entrances.begin());
            }
        } else {
            int start = std::max(0, index - 1);
            int end = std::min(static_cast<int>(entrances.size()), index + 2);
            entrances.erase(entrances.begin() + start, entrances.begin() + end);
        }

        if (entrances.size() < 3) break;
    }

    if (gates.empty()) {
        throw std::runtime_error("Bad walled area shape!");
    }

    // Smooth sections of wall with gates - mutate the shared point in place
    if (real) {
        for (auto& gatePtr : gates) {
            // Mutate the shared point directly
            gatePtr->set(shape.smoothVertex(*gatePtr));
        }
    }
}

void CurtainWall::buildTowers() {
    towers.clear();

    if (real_) {
        size_t len = shape.length();
        for (size_t i = 0; i < len; ++i) {
            geom::PointPtr tPtr = shape.ptr(i);

            // Check if this is a gate (by pointer identity)
            bool isGate = std::find(gates.begin(), gates.end(), tPtr) != gates.end();
            if (isGate) continue;

            // Check if adjacent segments are real wall
            bool prevSegment = segments[(i + len - 1) % len];
            bool currSegment = segments[i];

            if (prevSegment || currSegment) {
                towers.push_back(*tPtr);
            }
        }
    }
}

double CurtainWall::getRadius() const {
    double radius = 0.0;
    for (const auto& vPtr : shape) {
        radius = std::max(radius, vPtr->length());
    }
    return radius;
}

bool CurtainWall::bordersBy(Cell* p, const geom::Point& v0, const geom::Point& v1) const {
    bool withinWalls = std::find(patches_.begin(), patches_.end(), p) != patches_.end();

    int index;
    if (withinWalls) {
        index = shape.findEdge(v0, v1);
    } else {
        index = shape.findEdge(v1, v0);
    }

    if (index != -1 && static_cast<size_t>(index) < segments.size() && segments[index]) {
        return true;
    }

    return false;
}

bool CurtainWall::borders(Cell* p) const {
    bool withinWalls = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
    size_t length = shape.length();

    for (size_t i = 0; i < length; ++i) {
        if (!segments[i]) continue;

        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % length];

        int index;
        if (withinWalls) {
            index = p->shape.findEdge(v0, v1);
        } else {
            index = p->shape.findEdge(v1, v0);
        }

        if (index != -1) {
            return true;
        }
    }

    return false;
}

double CurtainWall::getTowerRadius(const geom::Point& vertex) const {
    // Faithful to mfcg.js getTowerRadius (line 11192-11193)
    // Returns: LTOWER_RADIUS if vertex is a tower, 1 + 2*TOWER_RADIUS if gate, 0 otherwise
    if (!real_) return 0.0;

    // Check if vertex is a tower
    for (const auto& tower : towers) {
        if (geom::Point::distance(tower, vertex) < 0.5) {
            return LTOWER_RADIUS;
        }
    }

    // Check if vertex is a gate
    for (const auto& gatePtr : gates) {
        if (geom::Point::distance(*gatePtr, vertex) < 0.5) {
            return 1.0 + 2.0 * TOWER_RADIUS;
        }
    }

    return 0.0;
}

} // namespace building
} // namespace town_generator
