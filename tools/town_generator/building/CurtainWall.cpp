/**
 * Implementation of CurtainWall class.
 */

#include "CurtainWall.hpp"
#include "Model.hpp"
#include "../utils/Random.hpp"
#include <SDL3/SDL.h>
#include <limits>

namespace town {

CurtainWall::CurtainWall(bool real, std::shared_ptr<Model> model,
                         const std::vector<std::shared_ptr<Patch>>& patches,
                         const Polygon& reserved)
    : real_(real), patches_(patches) {

    if (patches.size() == 1) {
        shape = patches[0]->shape;
    } else {
        shape = Model::findCircumference(patches);

        if (real) {
            float smoothFactor = std::min(1.0f, 40.0f / static_cast<float>(patches.size()));
            std::vector<Point> smoothed;
            for (size_t i = 0; i < shape.size(); ++i) {
                const Point& v = shape[i];
                if (reserved.contains(v)) {
                    smoothed.push_back(v);
                } else {
                    smoothed.push_back(shape.smoothVertex(v, smoothFactor));
                }
            }
            shape = Polygon(smoothed);
        }
    }

    segments.clear();
    for (size_t i = 0; i < shape.size(); ++i) {
        segments.push_back(true);
    }

    // Build gates
    gates.clear();

    // Entrances are vertices of the walls with more than 1 adjacent inner ward
    std::vector<Point> entrances;
    if (patches.size() > 1) {
        for (size_t i = 0; i < shape.size(); ++i) {
            const Point& v = shape[i];
            if (!reserved.contains(v)) {
                int count = 0;
                for (const auto& p : patches) {
                    if (p->shape.contains(v)) {
                        count++;
                    }
                }
                if (count > 1) {
                    entrances.push_back(v);
                }
            }
        }
    } else {
        for (size_t i = 0; i < shape.size(); ++i) {
            const Point& v = shape[i];
            if (!reserved.contains(v)) {
                entrances.push_back(v);
            }
        }
    }

    if (entrances.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Bad walled area shape!");
        return;
    }

    do {
        int index = static_cast<int>(Random::getFloat() * static_cast<float>(entrances.size()));
        Point gate = entrances[index];
        gates.push_back(gate);

        if (real) {
            // Find outer wards
            auto allVertPatches = model->patchByVertex(gate);
            std::vector<std::shared_ptr<Patch>> outerWards;
            for (auto& w : allVertPatches) {
                bool inPatches = false;
                for (const auto& p : patches) {
                    if (p == w) {
                        inPatches = true;
                        break;
                    }
                }
                if (!inPatches) {
                    outerWards.push_back(w);
                }
            }

            if (outerWards.size() == 1) {
                // Split the outer ward to make room for a road
                auto outer = outerWards[0];
                if (outer->shape.size() > 3) {
                    Point wall = shape.next(gate).subtract(shape.prev(gate));
                    Point out(wall.y, -wall.x);

                    // Find farthest point
                    Point farthest;
                    float maxScore = -std::numeric_limits<float>::infinity();
                    for (size_t j = 0; j < outer->shape.size(); ++j) {
                        const Point& v = outer->shape[j];
                        if (shape.contains(v) || reserved.contains(v)) {
                            continue;
                        }
                        Point dir = v.subtract(gate);
                        float score = dir.dot(out) / dir.length();
                        if (score > maxScore) {
                            maxScore = score;
                            farthest = v;
                        }
                    }

                    // Split the patch
                    auto halves = outer->shape.split(gate, farthest);
                    std::vector<std::shared_ptr<Patch>> newPatches;
                    for (auto& half : halves) {
                        newPatches.push_back(std::make_shared<Patch>(half.data()));
                    }

                    // Replace in model->patches
                    auto it = std::find(model->patches.begin(), model->patches.end(), outer);
                    if (it != model->patches.end()) {
                        it = model->patches.erase(it);
                        model->patches.insert(it, newPatches.begin(), newPatches.end());
                    }
                }
            }
        }

        // Remove neighbouring entrances to ensure gates aren't too close
        if (index == 0) {
            if (entrances.size() > 2) {
                entrances.erase(entrances.begin(), entrances.begin() + 2);
            } else {
                entrances.erase(entrances.begin());
            }
            if (!entrances.empty()) {
                entrances.pop_back();
            }
        } else if (index == static_cast<int>(entrances.size()) - 1) {
            size_t start = index > 0 ? index - 1 : 0;
            size_t count = std::min(size_t(2), entrances.size() - start);
            entrances.erase(entrances.begin() + start, entrances.begin() + start + count);
            if (!entrances.empty()) {
                entrances.erase(entrances.begin());
            }
        } else {
            size_t start = index > 0 ? index - 1 : 0;
            size_t count = std::min(size_t(3), entrances.size() - start);
            entrances.erase(entrances.begin() + start, entrances.begin() + start + count);
        }

    } while (entrances.size() >= 3);

    if (gates.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Bad walled area shape!");
        return;
    }

    // Smooth sections of wall near gates
    if (real) {
        for (auto& gate : gates) {
            int idx = shape.indexOf(gate);
            if (idx != -1) {
                shape[idx] = shape.smoothVertex(gate);
            }
        }
    }
}

} // namespace town
