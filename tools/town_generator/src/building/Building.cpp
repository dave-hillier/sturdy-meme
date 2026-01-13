#include "town_generator/building/Building.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace town_generator {
namespace building {

std::vector<bool> Building::getPlan(int width, int height, double stopProb) {
    // Faithful to mfcg.js Jd.getPlan (lines 123-141)
    // Grows a connected region from a random start cell until it reaches
    // all 4 edges of the grid, then stops with probability based on stopProb.
    // Creates L/T/U-shaped building footprints.
    int total = width * height;
    std::vector<bool> plan(total, false);

    // Start with random cell
    int startX = static_cast<int>(utils::Random::floatVal() * width);
    int startY = static_cast<int>(utils::Random::floatVal() * height);
    plan[startX + startY * width] = true;
    int remaining = total - 1;

    // Track filled region bounds
    int minX = startX, maxX = startX;
    int minY = startY, maxY = startY;

    // MFCG uses infinite loop - keep going until reaching all edges
    // Safety limit: if we've tried 10000 times, give up
    constexpr int MAX_SAFETY = 10000;
    int iterations = 0;

    while (iterations < MAX_SAFETY) {
        ++iterations;

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
                --remaining;
            }
        }

        // MFCG stopping condition: only stop after region reaches all 4 edges
        // canGrow = (minX > 0 || maxX < width-1 || minY > 0 || maxY < height-1)
        bool canGrow = (minX > 0 || maxX < width - 1 || minY > 0 || maxY < height - 1);

        bool shouldContinue;
        if (canGrow) {
            // Haven't reached all edges yet - keep going
            shouldContinue = true;
        } else {
            // At all edges
            if (remaining > 0) {
                // Still have cells - use stopProb (continue if Random < stopProb)
                shouldContinue = utils::Random::floatVal() < stopProb;
            } else {
                // No cells remaining
                shouldContinue = false;
            }
        }

        if (!shouldContinue) break;
    }

    return plan;
}

std::vector<bool> Building::getPlanFront(int width, int height) {
    // Faithful to mfcg.js Jd.getPlanFront (lines 142-158)
    // Like getPlan but starts with the entire front row filled.
    // Creates buildings that have frontage on one side.
    int total = width * height;
    std::vector<bool> plan(total, false);

    // Fill front row (y=0)
    for (int x = 0; x < width; ++x) {
        plan[x] = true;
    }
    int remaining = total - width;  // Cells remaining after front row
    int maxY = 0;
    int filledCount = width;

    // MFCG uses infinite loop - keep going until reaching back edge
    // Safety limit: if we've tried 10000 times, give up (should never hit this)
    constexpr int MAX_SAFETY = 10000;
    int iterations = 0;

    while (iterations < MAX_SAFETY) {
        ++iterations;

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
                --remaining;
                ++filledCount;
            }
        }

        // MFCG stopping logic: only check after reaching back edge
        bool shouldContinue;
        if (maxY >= height - 1) {
            // Reached the back edge
            if (remaining > 0) {
                // 50% chance to stop (continue if Random < 0.5)
                shouldContinue = utils::Random::floatVal() < 0.5;
            } else {
                // No cells remaining
                shouldContinue = false;
            }
        } else {
            // Haven't reached back edge yet - keep going
            shouldContinue = true;
        }

        if (!shouldContinue) break;
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

    // Follow the chain with safety limit
    constexpr size_t MAX_ITERATIONS = 1000;
    size_t iterations = 0;
    while (result.size() < starts.size() + 1 && iterations < MAX_ITERATIONS) {
        ++iterations;

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

    // Limit grid size to prevent excessive computation
    // mfcg.js typically produces small grids (2-5 cells per side)
    constexpr int MAX_GRID_SIZE = 8;
    if (cols > MAX_GRID_SIZE) cols = MAX_GRID_SIZE;
    if (rows > MAX_GRID_SIZE) rows = MAX_GRID_SIZE;

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

    // Generate grid cells - faithful to Cutter.grid (mfcg.js lines 6127-6167)
    // The gap parameter adds randomness to grid line positions
    std::vector<geom::Polygon> gridCells;
    {
        geom::Point p0 = quad[0], p1 = quad[1], p2 = quad[2], p3 = quad[3];

        // Create parameter arrays for columns and rows
        std::vector<double> colParams(cols + 1);
        std::vector<double> rowParams(rows + 1);

        for (int c = 0; c <= cols; ++c) {
            colParams[c] = static_cast<double>(c) / cols;
        }
        for (int r = 0; r <= rows; ++r) {
            rowParams[r] = static_cast<double>(r) / rows;
        }

        // Apply chaos/randomness to interior grid lines (faithful to mfcg.js lines 6140-6145)
        if (gap > 0) {
            for (int c = 1; c < cols; ++c) {
                double noise = (utils::Random::floatVal() + utils::Random::floatVal() +
                               utils::Random::floatVal()) / 3.0 - 0.5;
                colParams[c] += noise / (cols - 1) * gap;
            }
            for (int r = 1; r < rows; ++r) {
                double noise = (utils::Random::floatVal() + utils::Random::floatVal() +
                               utils::Random::floatVal()) / 3.0 - 0.5;
                rowParams[r] += noise / (rows - 1) * gap;
            }
        }

        // Generate vertex grid
        std::vector<std::vector<geom::Point>> vertices(rows + 1);
        for (int r = 0; r <= rows; ++r) {
            geom::Point left = geom::GeomUtils::lerp(p0, p3, rowParams[r]);
            geom::Point right = geom::GeomUtils::lerp(p1, p2, rowParams[r]);
            vertices[r].resize(cols + 1);
            for (int c = 0; c <= cols; ++c) {
                vertices[r][c] = geom::GeomUtils::lerp(left, right, colParams[c]);
            }
        }

        // Create cells from vertex grid
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                gridCells.push_back(geom::Polygon({
                    vertices[r][c],
                    vertices[r][c + 1],
                    vertices[r + 1][c + 1],
                    vertices[r + 1][c]
                }));
            }
        }
    }

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
