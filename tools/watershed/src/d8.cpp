#include "d8.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <SDL3/SDL_log.h>

#ifdef _OPENMP
#include <omp.h>
#endif

// D8 direction offsets
// Direction: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
static const int dx8[8] = { 0,  1, 1, 1, 0, -1, -1, -1};
static const int dy8[8] = {-1, -1, 0, 1, 1,  1,  0, -1};

// Distance weights for slope calculation (diagonal = sqrt(2))
static const double dist8[8] = {1.0, 1.414, 1.0, 1.414, 1.0, 1.414, 1.0, 1.414};

void get_d8_offset(uint8_t direction, int& dx, int& dy) {
    if (direction < 8) {
        dx = dx8[direction];
        dy = dy8[direction];
    } else {
        dx = 0;
        dy = 0;
    }
}

static uint8_t compute_flow_direction(const ElevationGrid& elevation, int x, int y) {
    uint16_t center = elevation.at(x, y);
    double max_slope = 0.0;
    uint8_t best_dir = 8;  // 8 = no flow (pit)

    for (int dir = 0; dir < 8; ++dir) {
        int nx = x + dx8[dir];
        int ny = y + dy8[dir];

        if (!elevation.in_bounds(nx, ny)) {
            // Flow off edge - treat as steepest possible
            best_dir = dir;
            max_slope = std::numeric_limits<double>::max();
            break;
        }

        uint16_t neighbor = elevation.at(nx, ny);
        if (neighbor < center) {
            double slope = (center - neighbor) / dist8[dir];
            if (slope > max_slope) {
                max_slope = slope;
                best_dir = dir;
            }
        }
    }

    return best_dir;
}

static void compute_flow_accumulation(
    const std::vector<uint8_t>& flow_direction,
    std::vector<uint32_t>& flow_accumulation,
    int width, int height
) {
    // Count incoming flows for each cell (parallel with atomics)
    std::vector<int> in_degree(width * height, 0);

    #pragma omp parallel for schedule(static) collapse(2)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t dir = flow_direction[y * width + x];
            if (dir < 8) {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    #pragma omp atomic
                    in_degree[ny * width + nx]++;
                }
            }
        }
    }

    // Initialize accumulation to 1 (self) - parallel
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < flow_accumulation.size(); ++i) {
        flow_accumulation[i] = 1;
    }

    // Collect cells with no incoming flow (parallel)
    std::vector<std::pair<int, int>> sources;
    #pragma omp parallel
    {
        std::vector<std::pair<int, int>> local_sources;
        #pragma omp for schedule(static) collapse(2) nowait
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (in_degree[y * width + x] == 0) {
                    local_sources.push_back({x, y});
                }
            }
        }
        #pragma omp critical
        sources.insert(sources.end(), local_sources.begin(), local_sources.end());
    }

    // Process cells with topological sort (sequential due to dependencies)
    std::queue<std::pair<int, int>> queue;
    for (auto& p : sources) {
        queue.push(p);
    }

    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop();

        uint8_t dir = flow_direction[y * width + x];
        if (dir < 8) {
            int nx = x + dx8[dir];
            int ny = y + dy8[dir];
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                flow_accumulation[ny * width + nx] += flow_accumulation[y * width + x];
                in_degree[ny * width + nx]--;
                if (in_degree[ny * width + nx] == 0) {
                    queue.push({nx, ny});
                }
            }
        }
    }
}

D8Result compute_d8(const ElevationGrid& elevation) {
    D8Result result;
    result.width = elevation.width;
    result.height = elevation.height;
    result.flow_direction.resize(elevation.width * elevation.height);
    result.flow_accumulation.resize(elevation.width * elevation.height);

    // Compute flow directions (embarrassingly parallel - each pixel is independent)
    #pragma omp parallel for schedule(dynamic, 64) collapse(2)
    for (int y = 0; y < elevation.height; ++y) {
        for (int x = 0; x < elevation.width; ++x) {
            result.flow_direction[y * elevation.width + x] =
                compute_flow_direction(elevation, x, y);
        }
    }

    // Compute flow accumulation (sequential due to dependencies)
    compute_flow_accumulation(
        result.flow_direction,
        result.flow_accumulation,
        result.width,
        result.height
    );

    return result;
}

// Get the opposite direction (for tracing upstream)
static uint8_t opposite_direction(uint8_t dir) {
    return (dir + 4) % 8;
}

// Get direction from (x1,y1) to (x2,y2)
static uint8_t direction_to(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    for (int dir = 0; dir < 8; ++dir) {
        if (dx8[dir] == dx && dy8[dir] == dy) return dir;
    }
    return 8;
}

// Watershed info for merging algorithm
struct WatershedInfo {
    int sink_x, sink_y;
    uint16_t sink_elevation;
    uint64_t elevation_sum;
    uint32_t area;
    bool is_boundary;  // True if drains to edge or sea
};

// Spill point between two watersheds
struct SpillPoint {
    uint32_t ws1, ws2;  // The two watersheds
    int x1, y1;         // Cell in ws1
    int x2, y2;         // Cell in ws2
    uint16_t spill_elevation;

    bool operator>(const SpillPoint& other) const {
        return spill_elevation > other.spill_elevation;
    }
};

// Resolve DAFA using watershed merging algorithm
// Based on: "Watershed Merging Algorithm for Channel Network Identification"
D8Result resolve_dafa_by_merging(const ElevationGrid& elevation, D8Result d8, uint16_t sea_level) {
    int width = d8.width;
    int height = d8.height;

    // Step 1: Label initial watersheds by tracing upstream from each sink
    std::vector<uint32_t> labels(width * height, 0);
    std::unordered_map<uint32_t, WatershedInfo> watersheds;
    uint32_t next_label = 0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (labels[y * width + x] != 0) continue;

            uint8_t dir = d8.flow_direction[y * width + x];
            bool is_sink = false;
            bool is_boundary = false;

            if (dir == 8) {
                is_sink = true;
                // Sea cells are boundary sinks
                if (elevation.at(x, y) <= sea_level) {
                    is_boundary = true;
                }
            } else {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                    is_sink = true;
                    is_boundary = true;
                } else if (elevation.at(nx, ny) <= sea_level && elevation.at(x, y) > sea_level) {
                    // Land cell draining to sea
                    is_sink = true;
                    is_boundary = true;
                }
            }

            if (!is_sink) continue;

            // New watershed - backward march to label all cells
            uint32_t label = ++next_label;
            WatershedInfo info;
            info.sink_x = x;
            info.sink_y = y;
            info.sink_elevation = elevation.at(x, y);
            info.is_boundary = is_boundary;
            info.area = 0;
            info.elevation_sum = 0;

            std::queue<std::pair<int, int>> q;
            q.push({x, y});
            labels[y * width + x] = label;

            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();

                info.area++;
                info.elevation_sum += elevation.at(cx, cy);

                // Find all cells that flow into this cell
                for (int ndir = 0; ndir < 8; ++ndir) {
                    int nx = cx + dx8[ndir];
                    int ny = cy + dy8[ndir];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    if (labels[ny * width + nx] != 0) continue;

                    uint8_t neighbor_dir = d8.flow_direction[ny * width + nx];
                    if (neighbor_dir == opposite_direction(ndir)) {
                        labels[ny * width + nx] = label;
                        q.push({nx, ny});
                    }
                }
            }

            watersheds[label] = info;
        }
    }

    SDL_Log("  Initial watersheds: %zu", watersheds.size());
    int boundary_count = 0;
    for (auto& [l, info] : watersheds) {
        if (info.is_boundary) boundary_count++;
    }
    SDL_Log("  Boundary watersheds: %d", boundary_count);
    SDL_Log("  Interior watersheds: %zu", (watersheds.size() - boundary_count));

    // Step 2: Union-Find structure for merging
    std::vector<uint32_t> parent(next_label + 1);
    for (uint32_t i = 0; i <= next_label; ++i) parent[i] = i;

    std::function<uint32_t(uint32_t)> find = [&](uint32_t x) -> uint32_t {
        if (parent[x] != x) parent[x] = find(parent[x]);
        return parent[x];
    };

    // Step 3: Find all spill points between adjacent watersheds (parallel)
    // Each thread builds a local map, then merge them
    std::map<std::pair<uint32_t, uint32_t>, SpillPoint> best_spills;

    #pragma omp parallel
    {
        std::map<std::pair<uint32_t, uint32_t>, SpillPoint> local_spills;

        #pragma omp for schedule(dynamic, 64) nowait
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint32_t label1 = labels[y * width + x];
                if (label1 == 0) continue;

                uint16_t elev1 = elevation.at(x, y);

                for (int dir = 0; dir < 8; ++dir) {
                    int nx = x + dx8[dir];
                    int ny = y + dy8[dir];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                    uint32_t label2 = labels[ny * width + nx];
                    if (label2 == 0 || label2 == label1) continue;

                    uint16_t elev2 = elevation.at(nx, ny);
                    uint16_t spill_elev = std::max(elev1, elev2);

                    // Canonical key (smaller label first)
                    auto key = std::make_pair(std::min(label1, label2), std::max(label1, label2));

                    auto it = local_spills.find(key);
                    if (it == local_spills.end() || spill_elev < it->second.spill_elevation) {
                        SpillPoint sp;
                        sp.ws1 = label1;
                        sp.ws2 = label2;
                        sp.x1 = x;
                        sp.y1 = y;
                        sp.x2 = nx;
                        sp.y2 = ny;
                        sp.spill_elevation = spill_elev;
                        local_spills[key] = sp;
                    }
                }
            }
        }

        // Merge local maps into global map
        #pragma omp critical
        {
            for (auto& [key, sp] : local_spills) {
                auto it = best_spills.find(key);
                if (it == best_spills.end() || sp.spill_elevation < it->second.spill_elevation) {
                    best_spills[key] = sp;
                }
            }
        }
    }

    // Step 4: Priority queue of spill points, sorted by elevation
    std::priority_queue<SpillPoint, std::vector<SpillPoint>, std::greater<SpillPoint>> pq;
    for (auto& [key, sp] : best_spills) {
        pq.push(sp);
    }

    SDL_Log("  Spill points found: %zu", pq.size());

    // Step 5: Process spill points in order of increasing elevation
    int merges_done = 0;
    while (!pq.empty()) {
        SpillPoint sp = pq.top();
        pq.pop();

        uint32_t root1 = find(sp.ws1);
        uint32_t root2 = find(sp.ws2);

        if (root1 == root2) continue;  // Already merged

        WatershedInfo& ws1 = watersheds[root1];
        WatershedInfo& ws2 = watersheds[root2];

        // If both are boundary, skip (they're both draining to sea/edge)
        if (ws1.is_boundary && ws2.is_boundary) continue;

        // Determine which merges into which
        // Interior flows to boundary; if both interior, smaller flows to larger
        uint32_t from_root, to_root;
        int from_x, from_y, to_x, to_y;

        if (ws2.is_boundary && !ws1.is_boundary) {
            from_root = root1;
            to_root = root2;
            from_x = sp.x1; from_y = sp.y1;
            to_x = sp.x2; to_y = sp.y2;
            if (find(sp.ws1) != root1) {
                // Need to find the correct boundary cells for the merged watershed
                from_x = sp.x1; from_y = sp.y1;
                to_x = sp.x2; to_y = sp.y2;
            }
        } else if (ws1.is_boundary && !ws2.is_boundary) {
            from_root = root2;
            to_root = root1;
            from_x = sp.x2; from_y = sp.y2;
            to_x = sp.x1; to_y = sp.y1;
        } else {
            // Both interior - smaller area flows to larger
            if (ws1.area <= ws2.area) {
                from_root = root1;
                to_root = root2;
                from_x = sp.x1; from_y = sp.y1;
                to_x = sp.x2; to_y = sp.y2;
            } else {
                from_root = root2;
                to_root = root1;
                from_x = sp.x2; from_y = sp.y2;
                to_x = sp.x1; to_y = sp.y1;
            }
        }

        WatershedInfo& from_ws = watersheds[from_root];
        WatershedInfo& to_ws = watersheds[to_root];

        // Trace path from sink of 'from_ws' to the spill boundary cell
        // Use BFS to find shortest path within the watershed
        std::vector<int> prev(width * height, -1);
        std::queue<std::pair<int, int>> bfs;
        bfs.push({from_ws.sink_x, from_ws.sink_y});
        prev[from_ws.sink_y * width + from_ws.sink_x] = from_ws.sink_y * width + from_ws.sink_x;

        while (!bfs.empty()) {
            auto [cx, cy] = bfs.front();
            bfs.pop();

            if (cx == from_x && cy == from_y) break;

            for (int dir = 0; dir < 8; ++dir) {
                int nnx = cx + dx8[dir];
                int nny = cy + dy8[dir];
                if (nnx < 0 || nnx >= width || nny < 0 || nny >= height) continue;
                if (prev[nny * width + nnx] >= 0) continue;
                if (find(labels[nny * width + nnx]) != from_root) continue;

                prev[nny * width + nnx] = cy * width + cx;
                bfs.push({nnx, nny});
            }
        }

        // Reconstruct path and set flow directions along it
        if (prev[from_y * width + from_x] >= 0) {
            int cur = from_y * width + from_x;
            while (prev[cur] != cur) {
                int pcur = prev[cur];
                int cx = cur % width, cy = cur / width;
                int px = pcur % width, py = pcur / width;

                // Set flow from prev to current
                uint8_t dir = direction_to(px, py, cx, cy);
                if (dir < 8) {
                    d8.flow_direction[py * width + px] = dir;
                }
                cur = pcur;
            }

            // Set boundary cell to flow into neighbor watershed
            uint8_t dir_to_neighbor = direction_to(from_x, from_y, to_x, to_y);
            if (dir_to_neighbor < 8) {
                d8.flow_direction[from_y * width + from_x] = dir_to_neighbor;
            }
        }

        // Merge watersheds in Union-Find
        parent[from_root] = to_root;
        to_ws.area += from_ws.area;
        to_ws.elevation_sum += from_ws.elevation_sum;
        // If either was boundary, merged result is boundary
        to_ws.is_boundary = to_ws.is_boundary || from_ws.is_boundary;

        merges_done++;
    }

    SDL_Log("  Merges performed: %d", merges_done);

    // Recompute flow accumulation with updated directions
    compute_flow_accumulation(d8.flow_direction, d8.flow_accumulation, width, height);

    // Count remaining pits
    int remaining_pits = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (d8.flow_direction[y * width + x] == 8 && elevation.at(x, y) > sea_level) {
                remaining_pits++;
            }
        }
    }
    SDL_Log("  Remaining land pits: %d", remaining_pits);

    return d8;
}

std::vector<uint32_t> trace_rivers_from_sea(
    const ElevationGrid& elevation,
    const D8Result& d8,
    uint32_t min_accumulation,
    uint16_t sea_level
) {
    int width = d8.width;
    int height = d8.height;
    std::vector<uint32_t> river_map(width * height, 0);

    // Build reverse flow graph: for each cell, which neighbors flow into it?
    // A neighbor at (nx, ny) flows into (x, y) if neighbor's flow direction points to (x, y)

    // Find sea outlet cells: cells at or below sea level that have upstream land neighbors
    // These are where rivers meet the sea
    std::queue<std::pair<int, int>> outlets;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint16_t elev = elevation.at(x, y);
            if (elev > sea_level) continue;  // Not sea

            // Check if any neighbor flows into this sea cell and has high accumulation
            for (int dir = 0; dir < 8; ++dir) {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];

                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                // Check if neighbor flows into this cell
                uint8_t neighbor_dir = d8.direction_at(nx, ny);
                if (neighbor_dir == opposite_direction(dir)) {
                    // This neighbor flows into our sea cell
                    uint32_t acc = d8.accumulation_at(nx, ny);
                    if (acc >= min_accumulation && elevation.at(nx, ny) > sea_level) {
                        // This is a river outlet - add the land cell to start tracing
                        outlets.push({nx, ny});
                    }
                }
            }
        }
    }

    // Also check edge cells that flow off the map - these are also outlets
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t dir = d8.direction_at(x, y);
            if (dir >= 8) continue;

            int nx = x + dx8[dir];
            int ny = y + dy8[dir];

            // Flows off edge?
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                uint32_t acc = d8.accumulation_at(x, y);
                if (acc >= min_accumulation) {
                    outlets.push({x, y});
                }
            }
        }
    }

    // BFS upstream from outlets, following reverse flow
    while (!outlets.empty()) {
        auto [x, y] = outlets.front();
        outlets.pop();

        int idx = y * width + x;
        if (river_map[idx] > 0) continue;  // Already visited

        uint32_t acc = d8.accumulation_at(x, y);
        river_map[idx] = acc;  // Store accumulation as river "size"

        // Find all neighbors that flow into this cell
        for (int dir = 0; dir < 8; ++dir) {
            int nx = x + dx8[dir];
            int ny = y + dy8[dir];

            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (river_map[ny * width + nx] > 0) continue;  // Already visited

            // Check if neighbor flows into this cell
            uint8_t neighbor_dir = d8.direction_at(nx, ny);
            if (neighbor_dir == opposite_direction(dir)) {
                uint32_t neighbor_acc = d8.accumulation_at(nx, ny);
                if (neighbor_acc >= min_accumulation) {
                    outlets.push({nx, ny});
                }
            }
        }
    }

    return river_map;
}
