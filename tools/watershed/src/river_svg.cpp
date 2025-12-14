#include "river_svg.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
#include <SDL3/SDL_log.h>

// D8 direction offsets (matching d8.cpp)
static const int dx8[8] = { 0,  1, 1, 1, 0, -1, -1, -1};
static const int dy8[8] = {-1, -1, 0, 1, 1,  1,  0, -1};

static uint8_t opposite_direction(uint8_t dir) {
    return (dir + 4) % 8;
}

// Find all headwater cells (river cells with no upstream river cells)
static std::vector<std::pair<int, int>> find_headwaters(
    const std::vector<uint32_t>& river_map,
    const D8Result& d8,
    int width,
    int height
) {
    std::vector<std::pair<int, int>> headwaters;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (river_map[y * width + x] == 0) continue;  // Not a river cell

            // Check if any river neighbor flows into this cell
            bool has_upstream_river = false;
            for (int dir = 0; dir < 8; ++dir) {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                if (river_map[ny * width + nx] == 0) continue;  // Not a river

                // Check if this neighbor flows into us
                uint8_t neighbor_dir = d8.direction_at(nx, ny);
                if (neighbor_dir == opposite_direction(dir)) {
                    has_upstream_river = true;
                    break;
                }
            }

            if (!has_upstream_river) {
                headwaters.push_back({x, y});
            }
        }
    }

    return headwaters;
}

// Trace a single river from headwater downstream until it ends or joins another river
static River trace_river_downstream(
    int start_x, int start_y,
    const std::vector<uint32_t>& river_map,
    const D8Result& d8,
    std::set<int>& visited,
    int width,
    int height
) {
    River river;
    river.max_accumulation = 0;

    int x = start_x;
    int y = start_y;

    while (true) {
        int idx = y * width + x;

        // Add point to river (use center of cell)
        river.points.push_back({x + 0.5, y + 0.5});
        river.max_accumulation = std::max(river.max_accumulation, river_map[idx]);
        visited.insert(idx);

        // Follow flow direction downstream
        uint8_t dir = d8.direction_at(x, y);
        if (dir >= 8) break;  // No flow direction (pit)

        int nx = x + dx8[dir];
        int ny = y + dy8[dir];

        // Off edge?
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) break;

        // No longer a river?
        if (river_map[ny * width + nx] == 0) break;

        // Already visited (junction with another traced river)?
        int nidx = ny * width + nx;
        if (visited.count(nidx)) {
            // Add the junction point and stop
            river.points.push_back({nx + 0.5, ny + 0.5});
            break;
        }

        x = nx;
        y = ny;
    }

    return river;
}

std::vector<River> extract_river_paths(
    const std::vector<uint32_t>& river_map,
    const D8Result& d8,
    int width,
    int height
) {
    std::vector<River> rivers;
    std::set<int> visited;

    // Find all headwaters
    auto headwaters = find_headwaters(river_map, d8, width, height);
    SDL_Log("  Found %zu river headwaters", headwaters.size());

    // Sort headwaters by accumulation (trace larger rivers first to handle junctions)
    std::sort(headwaters.begin(), headwaters.end(), [&](const auto& a, const auto& b) {
        return river_map[a.second * width + a.first] > river_map[b.second * width + b.first];
    });

    // Trace each river from headwater to outlet/junction
    for (const auto& [hx, hy] : headwaters) {
        if (visited.count(hy * width + hx)) continue;  // Already part of another river

        River river = trace_river_downstream(hx, hy, river_map, d8, visited, width, height);

        // Only keep rivers with at least 3 points (minimum for a curve)
        if (river.points.size() >= 3) {
            rivers.push_back(std::move(river));
        }
    }

    SDL_Log("  Extracted %zu river paths", rivers.size());
    return rivers;
}

// Compute Catmull-Rom spline control points for SVG cubic Bezier
// Converts a Catmull-Rom segment to Bezier control points
static void catmull_rom_to_bezier(
    const Point& p0, const Point& p1, const Point& p2, const Point& p3,
    double tension,
    Point& cp1, Point& cp2
) {
    double t = (1.0 - tension) / 6.0;

    cp1.x = p1.x + t * (p2.x - p0.x);
    cp1.y = p1.y + t * (p2.y - p0.y);

    cp2.x = p2.x - t * (p3.x - p1.x);
    cp2.y = p2.y - t * (p3.y - p1.y);
}

// Simplify path using Ramer-Douglas-Peucker algorithm
static std::vector<Point> simplify_path(const std::vector<Point>& points, double epsilon) {
    if (points.size() < 3) return points;

    // Find point with maximum distance from line between first and last
    double max_dist = 0;
    size_t max_idx = 0;

    const Point& start = points.front();
    const Point& end = points.back();

    double dx = end.x - start.x;
    double dy = end.y - start.y;
    double line_len = std::sqrt(dx * dx + dy * dy);

    if (line_len > 0) {
        for (size_t i = 1; i < points.size() - 1; ++i) {
            // Perpendicular distance from point to line
            double d = std::abs(dy * points[i].x - dx * points[i].y +
                               end.x * start.y - end.y * start.x) / line_len;
            if (d > max_dist) {
                max_dist = d;
                max_idx = i;
            }
        }
    }

    if (max_dist > epsilon) {
        // Recursively simplify
        std::vector<Point> left(points.begin(), points.begin() + max_idx + 1);
        std::vector<Point> right(points.begin() + max_idx, points.end());

        auto left_simplified = simplify_path(left, epsilon);
        auto right_simplified = simplify_path(right, epsilon);

        // Combine (remove duplicate middle point)
        std::vector<Point> result = left_simplified;
        result.insert(result.end(), right_simplified.begin() + 1, right_simplified.end());
        return result;
    }

    return {points.front(), points.back()};
}

// Generate SVG path string using Catmull-Rom splines converted to Bezier curves
static std::string generate_svg_path(const std::vector<Point>& points, double tension = 0.5) {
    if (points.size() < 2) return "";

    std::ostringstream path;
    path << std::fixed << std::setprecision(2);

    // Move to first point
    path << "M " << points[0].x << " " << points[0].y;

    if (points.size() == 2) {
        // Just a line
        path << " L " << points[1].x << " " << points[1].y;
        return path.str();
    }

    // For Catmull-Rom, we need points before and after each segment
    // Extend by duplicating endpoints
    std::vector<Point> extended;
    extended.push_back(points[0]);  // Duplicate first point
    for (const auto& p : points) {
        extended.push_back(p);
    }
    extended.push_back(points.back());  // Duplicate last point

    // Generate cubic Bezier curves for each segment
    for (size_t i = 0; i < points.size() - 1; ++i) {
        Point cp1, cp2;
        catmull_rom_to_bezier(
            extended[i], extended[i + 1], extended[i + 2], extended[i + 3],
            tension, cp1, cp2
        );

        path << " C " << cp1.x << " " << cp1.y
             << " " << cp2.x << " " << cp2.y
             << " " << points[i + 1].x << " " << points[i + 1].y;
    }

    return path.str();
}

void write_rivers_svg(
    const std::string& filename,
    const std::vector<River>& rivers,
    int width,
    int height,
    int output_width,
    int output_height
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s", filename.c_str());
        return;
    }

    // Use output dimensions if provided, otherwise use processing dimensions
    int svg_width = (output_width > 0) ? output_width : width;
    int svg_height = (output_height > 0) ? output_height : height;

    // Calculate scale factors for coordinate transformation
    double scale_x = static_cast<double>(svg_width) / width;
    double scale_y = static_cast<double>(svg_height) / height;

    // Find max accumulation for stroke width scaling
    uint32_t global_max_acc = 0;
    for (const auto& river : rivers) {
        global_max_acc = std::max(global_max_acc, river.max_accumulation);
    }

    double log_max = std::log(static_cast<double>(global_max_acc) + 1.0);

    // Write SVG header
    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << svg_width << "\" height=\"" << svg_height << "\" "
         << "viewBox=\"0 0 " << svg_width << " " << svg_height << "\">\n";

    // Add metadata
    file << "  <!-- River network: " << rivers.size() << " rivers -->\n";
    file << "  <!-- Max flow accumulation: " << global_max_acc << " -->\n";
    file << "  <!-- Processing resolution: " << width << "x" << height << " -->\n";
    file << "  <!-- Output resolution: " << svg_width << "x" << svg_height << " -->\n";

    // Write each river as a path
    int rivers_written = 0;
    for (size_t i = 0; i < rivers.size(); ++i) {
        const River& river = rivers[i];

        // Scale points to output dimensions
        std::vector<Point> scaled_points;
        scaled_points.reserve(river.points.size());
        for (const auto& p : river.points) {
            scaled_points.push_back({p.x * scale_x, p.y * scale_y});
        }

        // Simplify path to reduce complexity (epsilon scaled to output resolution)
        auto simplified = simplify_path(scaled_points, scale_x);
        if (simplified.size() < 2) continue;

        // Calculate stroke width based on max accumulation (scaled for output)
        double log_acc = std::log(static_cast<double>(river.max_accumulation) + 1.0);
        double stroke_width = (0.5 + 4.5 * (log_acc / log_max)) * scale_x;

        // Generate path with Catmull-Rom spline
        std::string path_d = generate_svg_path(simplified, 0.5);
        file << "  <path d=\"" << path_d << "\" "
             << "fill=\"none\" "
             << "stroke=\"#1e90ff\" "
             << "stroke-width=\"" << stroke_width << "\" "
             << "stroke-linecap=\"round\" "
             << "stroke-linejoin=\"round\"/>\n";

        rivers_written++;
    }

    file << "</svg>\n";
    SDL_Log("  Wrote %d rivers to SVG", rivers_written);
}
