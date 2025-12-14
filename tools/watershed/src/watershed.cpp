#include "watershed.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>

static const int dx8[8] = { 0,  1, 1, 1, 0, -1, -1, -1};
static const int dy8[8] = {-1, -1, 0, 1, 1,  1,  0, -1};

WatershedResult delineate_watersheds(const D8Result& d8) {
    WatershedResult result;
    result.width = d8.width;
    result.height = d8.height;
    result.labels.resize(d8.width * d8.height, 0);
    result.basin_count = 0;

    // Find all outlet cells (pits, flats, or edge-draining)
    std::vector<std::pair<int, int>> outlets;
    for (int y = 0; y < d8.height; ++y) {
        for (int x = 0; x < d8.width; ++x) {
            uint8_t dir = d8.direction_at(x, y);
            if (dir == 8) {
                // Pit or flat - this is an outlet
                outlets.push_back({x, y});
            } else {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];
                if (nx < 0 || nx >= d8.width || ny < 0 || ny >= d8.height) {
                    // Drains off edge - this is an outlet
                    outlets.push_back({x, y});
                }
            }
        }
    }

    // Label each outlet and trace upstream
    uint32_t label = 0;
    for (const auto& [ox, oy] : outlets) {
        if (result.labels[oy * d8.width + ox] != 0) {
            continue;  // Already labeled
        }

        label++;
        std::queue<std::pair<int, int>> queue;
        queue.push({ox, oy});
        result.labels[oy * d8.width + ox] = label;

        while (!queue.empty()) {
            auto [x, y] = queue.front();
            queue.pop();

            // Find all cells that flow into this one
            for (int dir = 0; dir < 8; ++dir) {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];

                if (nx < 0 || nx >= d8.width || ny < 0 || ny >= d8.height) {
                    continue;
                }

                if (result.labels[ny * d8.width + nx] != 0) {
                    continue;  // Already labeled
                }

                // Check if neighbor flows to current cell
                // The opposite direction would be (dir + 4) % 8
                uint8_t neighbor_dir = d8.direction_at(nx, ny);
                if (neighbor_dir == (dir + 4) % 8) {
                    result.labels[ny * d8.width + nx] = label;
                    queue.push({nx, ny});
                }
            }
        }
    }

    result.basin_count = label;
    return result;
}

struct BasinInfo {
    uint32_t area;
    int lowest_x, lowest_y;
    uint16_t lowest_elevation;
    uint32_t spill_to;  // Basin it would spill into
    uint16_t spill_elevation;
};

WatershedResult merge_watersheds(
    const WatershedResult& watersheds,
    const ElevationGrid& elevation,
    const D8Result& /* d8 */,
    uint32_t min_area
) {
    WatershedResult result = watersheds;

    // Calculate basin areas and find lowest points
    std::unordered_map<uint32_t, BasinInfo> basins;

    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            uint32_t label = result.labels[y * result.width + x];
            if (label == 0) continue;

            auto& info = basins[label];
            info.area++;

            uint16_t elev = elevation.at(x, y);
            if (info.area == 1 || elev < info.lowest_elevation) {
                info.lowest_elevation = elev;
                info.lowest_x = x;
                info.lowest_y = y;
            }
        }
    }

    // Find spill points between basins
    for (auto& [label, info] : basins) {
        info.spill_to = 0;
        info.spill_elevation = UINT16_MAX;
    }

    // Check all boundary cells
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            uint32_t label = result.labels[y * result.width + x];
            if (label == 0) continue;

            uint16_t elev = elevation.at(x, y);

            for (int dir = 0; dir < 8; ++dir) {
                int nx = x + dx8[dir];
                int ny = y + dy8[dir];

                if (nx < 0 || nx >= result.width || ny < 0 || ny >= result.height) {
                    continue;
                }

                uint32_t neighbor_label = result.labels[ny * result.width + nx];
                if (neighbor_label != 0 && neighbor_label != label) {
                    // Boundary between basins
                    uint16_t neighbor_elev = elevation.at(nx, ny);
                    uint16_t max_elev = std::max(elev, neighbor_elev);

                    auto& info = basins[label];
                    if (max_elev < info.spill_elevation) {
                        info.spill_elevation = max_elev;
                        info.spill_to = neighbor_label;
                    }
                }
            }
        }
    }

    // Union-Find for merging
    std::unordered_map<uint32_t, uint32_t> parent;
    for (const auto& [label, info] : basins) {
        parent[label] = label;
    }

    std::function<uint32_t(uint32_t)> find = [&](uint32_t x) -> uint32_t {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }
        return parent[x];
    };

    auto unite = [&](uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);
        if (ra != rb) {
            // Merge smaller into larger
            if (basins[ra].area < basins[rb].area) {
                parent[ra] = rb;
                basins[rb].area += basins[ra].area;
            } else {
                parent[rb] = ra;
                basins[ra].area += basins[rb].area;
            }
        }
    };

    // Iteratively merge small basins
    bool changed = true;
    while (changed) {
        changed = false;

        for (const auto& [label, info] : basins) {
            uint32_t root = find(label);
            if (basins[root].area < min_area && info.spill_to != 0) {
                uint32_t neighbor_root = find(info.spill_to);
                if (root != neighbor_root) {
                    unite(root, neighbor_root);
                    changed = true;
                }
            }
        }
    }

    // Relabel with merged basins
    std::unordered_map<uint32_t, uint32_t> new_labels;
    uint32_t next_label = 0;

    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            uint32_t old_label = result.labels[y * result.width + x];
            if (old_label == 0) continue;

            uint32_t root = find(old_label);
            if (new_labels.find(root) == new_labels.end()) {
                new_labels[root] = ++next_label;
            }
            result.labels[y * result.width + x] = new_labels[root];
        }
    }

    result.basin_count = next_label;
    return result;
}
