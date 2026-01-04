#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/building/Model.hpp"

namespace town_generator2 {
namespace building {

CurtainWall::CurtainWall(bool real, Model& model, std::vector<Patch*>& patches,
                         const geom::PointList& reserved)
    : real_(real), patches_(patches)
{
    if (patches.size() == 1) {
        shape = patches[0]->shape.copy();
    } else {
        shape = Model::findCircumference(patches);

        if (real) {
            double smoothFactor = std::min(1.0, 40.0 / patches.size());

            // Smooth vertices except reserved ones
            for (size_t i = 0; i < shape.length(); ++i) {
                geom::PointPtr v = shape.ptr(i);
                bool isReserved = false;
                for (const auto& r : reserved) {
                    if (r == v) { isReserved = true; break; }
                }

                if (!isReserved) {
                    geom::Point smoothed = shape.smoothVertex(v, smoothFactor);
                    v->set(smoothed);
                }
            }
        }
    }

    segments.resize(shape.length(), true);
    buildGates(real, model, reserved);
}

void CurtainWall::buildGates(bool real, Model& model, const geom::PointList& reserved) {
    gates.clear();

    // Find entrances - vertices shared by multiple inner wards
    geom::PointList entrances;
    if (patches_.size() > 1) {
        for (const auto& v : shape) {
            // Check if reserved
            bool isReserved = false;
            for (const auto& r : reserved) {
                if (r == v) { isReserved = true; break; }
            }
            if (isReserved) continue;

            // Count patches containing this vertex
            int count = 0;
            for (auto* p : patches_) {
                if (p->shape.contains(v)) count++;
            }
            if (count > 1) {
                entrances.push_back(v);
            }
        }
    } else {
        for (const auto& v : shape) {
            bool isReserved = false;
            for (const auto& r : reserved) {
                if (r == v) { isReserved = true; break; }
            }
            if (!isReserved) {
                entrances.push_back(v);
            }
        }
    }

    if (entrances.empty()) {
        throw std::runtime_error("Bad walled area shape!");
    }

    do {
        int index = utils::Random::getInt(0, entrances.size());
        geom::PointPtr gate = entrances[index];
        gates.push_back(gate);

        if (real) {
            // Find outer wards adjacent to this gate
            auto allPatches = model.patchByVertex(gate);
            std::vector<Patch*> outerWards;
            for (auto* p : allPatches) {
                bool isInner = false;
                for (auto* ip : patches_) {
                    if (p == ip) { isInner = true; break; }
                }
                if (!isInner) {
                    outerWards.push_back(p);
                }
            }

            if (outerWards.size() == 1) {
                // Split outer ward to create road access
                Patch* outer = outerWards[0];
                if (outer->shape.length() > 3) {
                    geom::Point prevP = *shape.prev(gate);
                    geom::Point nextP = *shape.next(gate);
                    geom::Point wall = nextP.subtract(prevP);
                    geom::Point out(-wall.y, wall.x);

                    geom::PointPtr farthest = outer->shape.max([&](const geom::Point& v) {
                        // Skip if on wall or reserved
                        if (shape.containsByValue(v)) return -std::numeric_limits<double>::infinity();
                        for (const auto& r : reserved) {
                            if (*r == v) return -std::numeric_limits<double>::infinity();
                        }

                        geom::Point dir = v.subtract(*gate);
                        return dir.dot(out) / dir.length();
                    });

                    auto halves = outer->shape.split(gate, farthest);
                    if (halves.size() == 2) {
                        // Create new patches from the split
                        for (auto& half : halves) {
                            auto newPatch = std::make_unique<Patch>(half);
                            model.patches.push_back(newPatch.get());
                            model.ownedPatches_.push_back(std::move(newPatch));
                        }
                        // Remove original from patches list
                        model.patches.erase(
                            std::find(model.patches.begin(), model.patches.end(), outer));
                    }
                }
            }
        }

        // Remove neighboring entrances
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

    } while (entrances.size() >= 3);

    if (gates.empty()) {
        throw std::runtime_error("Bad walled area shape!");
    }

    // Smooth further sections of the wall with gates
    if (real) {
        for (const auto& gate : gates) {
            geom::Point smoothed = shape.smoothVertex(gate);
            gate->set(smoothed);
        }
    }
}

void CurtainWall::buildTowers() {
    towers.clear();
    if (real_) {
        size_t len = shape.length();
        for (size_t i = 0; i < len; ++i) {
            geom::PointPtr t = shape.ptr(i);

            // Check if this is a gate
            bool isGate = false;
            for (const auto& g : gates) {
                if (g == t) { isGate = true; break; }
            }

            if (!isGate) {
                // Check if adjacent segments are walls
                bool prevSeg = segments[(i + len - 1) % len];
                bool nextSeg = segments[i];
                if (prevSeg || nextSeg) {
                    towers.push_back(t);
                }
            }
        }
    }
}

} // namespace building
} // namespace town_generator2
