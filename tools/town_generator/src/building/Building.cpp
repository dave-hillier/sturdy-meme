#include "town_generator/building/Building.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace town_generator {
namespace building {

std::vector<bool> Building::getPlan(int width, int height, double stopProb) {
    // Faithful to mfcg.js Jd.getPlan (lines 9809-9828)
    int total = width * height;
    std::vector<bool> plan(total, false);

    // Start with random cell
    int startX = static_cast<int>(utils::Random::floatVal() * width);
    int startY = static_cast<int>(utils::Random::floatVal() * height);
    plan[startX + startY * width] = true;
    int filled = total - 1;

    // Track filled region bounds
    int minX = startX, maxX = startX;
    int minY = startY, maxY = startY;

    // Grow the filled region
    while (true) {
        int x = static_cast<int>(utils::Random::floatVal() * width);
        int y = static_cast<int>(utils::Random::floatVal() * height);
        int idx = x + y * width;

        // Check if cell is empty and adjacent to filled cell
        if (!plan[idx]) {
            bool adjacent = (x > 0 && plan[idx - 1]) ||
                           (y > 0 && plan[idx - width]) ||
                           (x < width - 1 && plan[idx + 1]) ||
                           (y < height - 1 && plan[idx + width]);

            if (adjacent) {
                // Update bounds
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;

                plan[idx] = true;
                --filled;
            }
        }

        // Check stopping condition
        bool canGrow = (minX > 0 || maxX < width - 1 || minY > 0 || maxY < height - 1);
        if (!canGrow) {
            if (filled > 0) {
                if (utils::Random::floatVal() >= stopProb) break;
            } else {
                break;
            }
        }
    }

    return plan;
}

std::vector<bool> Building::getPlanFront(int width, int height) {
    // Faithful to mfcg.js Jd.getPlanFront (lines 9829-9848)
    int total = width * height;
    std::vector<bool> plan(total, false);

    // Fill front row
    for (int x = 0; x < width; ++x) {
        plan[x] = true;
    }
    int filled = total - width;

    int maxY = 0;

    // Grow from front
    while (true) {
        int x = static_cast<int>(utils::Random::floatVal() * width);
        int y = 1 + static_cast<int>(utils::Random::floatVal() * (height - 1));
        int idx = x + y * width;

        if (!plan[idx]) {
            bool adjacent = (x > 0 && plan[idx - 1]) ||
                           (y > 0 && plan[idx - width]) ||
                           (x < width - 1 && plan[idx + 1]) ||
                           (y < height - 1 && plan[idx + width]);

            if (adjacent) {
                if (y > maxY) maxY = y;
                plan[idx] = true;
                --filled;
            }
        }

        // Check stopping
        bool canGrow = maxY < height - 1;
        if (!canGrow) {
            if (filled > 0 && utils::Random::floatVal() >= 0.5) break;
            if (filled <= 0) break;
        }
    }

    return plan;
}

std::vector<bool> Building::getPlanSym(int width, int height) {
    // Faithful to mfcg.js Jd.getPlanSym (lines 9849-9860)
    std::vector<bool> plan = getPlan(width, height, 0.0);

    // Mirror horizontally
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx1 = y * width + x;
            int idx2 = (y + 1) * width - 1 - x;
            plan[idx1] = plan[idx2] = (plan[idx1] || plan[idx2]);
        }
    }

    return plan;
}

std::vector<geom::Point> Building::circumference(const std::vector<geom::Polygon>& cells) {
    // Faithful to mfcg.js Jd.circumference (lines 9861-9906)
    if (cells.empty()) return {};
    if (cells.size() == 1) {
        std::vector<geom::Point> result;
        for (size_t i = 0; i < cells[0].length(); ++i) {
            result.push_back(cells[0][i]);
        }
        return result;
    }

    // Collect all directed edges
    std::vector<geom::Point> starts;
    std::vector<geom::Point> ends;

    for (const auto& cell : cells) {
        size_t len = cell.length();
        for (size_t i = 0; i < len; ++i) {
            geom::Point p = cell[i];
            geom::Point q = cell[(i + 1) % len];

            // Check if reverse edge exists (internal edge)
            bool foundReverse = false;
            for (size_t j = 0; j < starts.size(); ++j) {
                // Check if (q, p) exists
                if (std::abs(starts[j].x - q.x) < 1e-6 &&
                    std::abs(starts[j].y - q.y) < 1e-6 &&
                    std::abs(ends[j].x - p.x) < 1e-6 &&
                    std::abs(ends[j].y - p.y) < 1e-6) {
                    // Remove internal edge
                    starts.erase(starts.begin() + j);
                    ends.erase(ends.begin() + j);
                    foundReverse = true;
                    break;
                }
            }

            if (!foundReverse) {
                starts.push_back(p);
                ends.push_back(q);
            }
        }
    }

    if (starts.empty()) return {};

    // Build polygon from remaining edges
    // Find starting point (any point that appears multiple times as start)
    int startIdx = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        int count = 0;
        for (size_t j = 0; j < starts.size(); ++j) {
            if (std::abs(starts[i].x - starts[j].x) < 1e-6 &&
                std::abs(starts[i].y - starts[j].y) < 1e-6) {
                ++count;
            }
        }
        if (count > 1) {
            startIdx = static_cast<int>(i);
            break;
        }
    }

    std::vector<geom::Point> result;
    geom::Point current = starts[startIdx];
    geom::Point next = ends[startIdx];
    result.push_back(current);

    // Follow the chain
    while (result.size() < starts.size() + 1) {
        // Find point close to result[0] (loop closed)
        if (std::abs(next.x - result[0].x) < 1e-6 &&
            std::abs(next.y - result[0].y) < 1e-6) {
            break;
        }

        result.push_back(next);

        // Find edge starting from next
        bool found = false;
        for (size_t i = 0; i < starts.size(); ++i) {
            if (std::abs(starts[i].x - next.x) < 1e-6 &&
                std::abs(starts[i].y - next.y) < 1e-6) {
                next = ends[i];
                found = true;
                break;
            }
        }

        if (!found) break;
    }

    // Remove collinear points
    std::vector<geom::Point> simplified;
    for (size_t i = 0; i < result.size(); ++i) {
        geom::Point prev = result[(i + result.size() - 1) % result.size()];
        geom::Point curr = result[i];
        geom::Point next = result[(i + 1) % result.size()];

        geom::Point d1 = curr.subtract(prev);
        geom::Point d2 = next.subtract(curr);

        double len1 = d1.length();
        double len2 = d2.length();

        if (len1 < 1e-6 || len2 < 1e-6) continue;

        double dot = (d1.x * d2.x + d1.y * d2.y) / (len1 * len2);

        // Keep non-collinear points
        if (dot < 0.999) {
            simplified.push_back(curr);
        }
    }

    return simplified.empty() ? result : simplified;
}

geom::Polygon Building::create(
    const geom::Polygon& quad,
    double minSq,
    bool hasFront,
    bool symmetric,
    double gap
) {
    // Faithful to mfcg.js Jd.create (lines 9754-9808)
    if (quad.length() != 4) {
        return quad;  // Not a quadrilateral
    }

    double cellSize = std::sqrt(minSq);

    // Calculate edge lengths
    double len01 = geom::Point::distance(quad[0], quad[1]);
    double len12 = geom::Point::distance(quad[1], quad[2]);
    double len23 = geom::Point::distance(quad[2], quad[3]);
    double len30 = geom::Point::distance(quad[3], quad[0]);

    // Grid dimensions based on shorter parallel edges
    int cols = static_cast<int>(std::ceil(std::min(len01, len23) / cellSize));
    int rows = static_cast<int>(std::ceil(std::min(len12, len30) / cellSize));

    // Need at least 2x2 grid for interesting shapes
    if (cols <= 1 || rows <= 1) {
        return geom::Polygon();  // Too small
    }

    // Generate cell plan
    std::vector<bool> plan;
    if (symmetric) {
        plan = getPlanSym(cols, rows);
    } else if (hasFront) {
        plan = getPlanFront(cols, rows);
    } else {
        plan = getPlan(cols, rows);
    }

    // Count filled cells
    int filledCount = 0;
    for (bool b : plan) {
        if (b) ++filledCount;
    }

    // If all cells filled, return original quad
    if (filledCount >= cols * rows) {
        return geom::Polygon();  // No L-shape possible
    }

    // Generate grid cells (using Cutter::grid, faithful to MFCG)
    auto gridCells = Cutter::grid(quad, cols, rows, gap);

    // Collect filled cells
    std::vector<geom::Polygon> filledPolygons;
    for (int i = 0; i < static_cast<int>(plan.size()); ++i) {
        if (plan[i] && i < static_cast<int>(gridCells.size())) {
            filledPolygons.push_back(gridCells[i]);
        }
    }

    // Compute circumference
    auto outline = circumference(filledPolygons);

    if (outline.size() < 3) {
        return geom::Polygon();  // Failed to create outline
    }

    return geom::Polygon(outline);
}

} // namespace building
} // namespace town_generator
