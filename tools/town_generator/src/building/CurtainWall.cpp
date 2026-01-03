#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace town_generator {
namespace building {

CurtainWall::CurtainWall(
    bool real,
    Model* model,
    const std::vector<Patch*>& patches,
    const std::vector<geom::Point>& reserved
) : real_(real), patches_(patches) {

    if (patches.size() == 1) {
        shape = patches[0]->shape;
    } else {
        shape = Model::findCircumference(patches);

        if (real) {
            double smoothFactor = std::min(1.0, 40.0 / patches.size());

            std::vector<geom::Point> smoothed;
            for (const auto& vPtr : shape) {
                const geom::Point& v = *vPtr;
                bool isReserved = std::find(reserved.begin(), reserved.end(), v) != reserved.end();
                if (isReserved) {
                    smoothed.push_back(v);
                } else {
                    smoothed.push_back(shape.smoothVertex(v, smoothFactor));
                }
            }

            for (size_t i = 0; i < shape.length(); ++i) {
                shape[i].set(smoothed[i]);
            }
        }
    }

    segments.resize(shape.length(), true);

    buildGates(real, model, reserved);
}

void CurtainWall::buildGates(bool real, Model* model, const std::vector<geom::Point>& reserved) {
    gates.clear();

    // Find potential entrance points
    std::vector<geom::Point> entrances;

    if (patches_.size() > 1) {
        for (const auto& vPtr : shape) {
            const geom::Point& v = *vPtr;
            bool isReserved = std::find(reserved.begin(), reserved.end(), v) != reserved.end();
            if (isReserved) continue;

            // Count adjacent inner wards
            int adjacentCount = 0;
            for (auto* p : patches_) {
                if (p->shape.contains(v)) {
                    adjacentCount++;
                }
            }

            if (adjacentCount > 1) {
                entrances.push_back(v);
            }
        }
    } else {
        for (const auto& vPtr : shape) {
            const geom::Point& v = *vPtr;
            bool isReserved = std::find(reserved.begin(), reserved.end(), v) != reserved.end();
            if (!isReserved) {
                entrances.push_back(v);
            }
        }
    }

    // If no entrances found with strict criteria, use all non-reserved vertices
    if (entrances.empty()) {
        for (const auto& vPtr : shape) {
            const geom::Point& v = *vPtr;
            bool isReserved = std::find(reserved.begin(), reserved.end(), v) != reserved.end();
            if (!isReserved) {
                entrances.push_back(v);
            }
        }
    }

    if (entrances.empty()) {
        // Last resort: just use the first vertex
        if (shape.length() > 0) {
            entrances.push_back(shape[0]);
        } else {
            return;  // Can't create gates on empty shape
        }
    }

    // Select gates
    while (!entrances.empty()) {
        int index = utils::Random::intVal(0, static_cast<int>(entrances.size()));
        geom::Point gate = entrances[index];
        gates.push_back(gate);

        if (real) {
            // Find outer wards for potential road creation
            std::vector<Patch*> outerWards;
            for (auto* p : model->patches) {
                if (p->shape.contains(gate)) {
                    bool isInner = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
                    if (!isInner) {
                        outerWards.push_back(p);
                    }
                }
            }

            if (outerWards.size() == 1) {
                auto* outer = outerWards[0];
                if (outer->shape.length() > 3) {
                    geom::Point prevGate = shape.prev(gate);
                    geom::Point nextGate = shape.next(gate);
                    geom::Point wall = nextGate.subtract(prevGate);
                    geom::Point out(wall.y, -wall.x);

                    // Find farthest point in the direction perpendicular to wall
                    double maxDot = -std::numeric_limits<double>::infinity();
                    geom::Point farthest = outer->shape[0];

                    for (const auto& vPtr : outer->shape) {
                        const geom::Point& v = *vPtr;
                        bool onShape = shape.contains(v);
                        bool isRes = std::find(reserved.begin(), reserved.end(), v) != reserved.end();

                        if (!onShape && !isRes) {
                            geom::Point dir = v.subtract(gate);
                            double dot = dir.dot(out) / dir.length();
                            if (dot > maxDot) {
                                maxDot = dot;
                                farthest = v;
                            }
                        }
                    }

                    // Split the outer ward
                    auto halves = outer->shape.split(gate, farthest);
                    std::vector<Patch*> newPatches;
                    for (const auto& half : halves) {
                        auto* newPatch = new Patch(half);
                        newPatches.push_back(newPatch);
                    }

                    // Replace in model
                    auto it = std::find(model->patches.begin(), model->patches.end(), outer);
                    if (it != model->patches.end()) {
                        *it = newPatches[0];
                        for (size_t i = 1; i < newPatches.size(); ++i) {
                            model->patches.insert(it + i, newPatches[i]);
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

    // Smooth sections of wall with gates
    if (real) {
        for (auto& gate : gates) {
            gate.set(shape.smoothVertex(gate));
        }
    }
}

void CurtainWall::buildTowers() {
    towers.clear();

    if (real_) {
        size_t len = shape.length();
        for (size_t i = 0; i < len; ++i) {
            const geom::Point& t = shape[i];

            // Check if this is a gate
            bool isGate = std::find(gates.begin(), gates.end(), t) != gates.end();
            if (isGate) continue;

            // Check if adjacent segments are real wall
            bool prevSegment = segments[(i + len - 1) % len];
            bool currSegment = segments[i];

            if (prevSegment || currSegment) {
                towers.push_back(t);
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

bool CurtainWall::bordersBy(Patch* p, const geom::Point& v0, const geom::Point& v1) const {
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

bool CurtainWall::borders(Patch* p) const {
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

} // namespace building
} // namespace town_generator
