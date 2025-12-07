// River Sources Preprocessing Tool
// Traces rivers upstream from where they meet sea level to find their sources
// Generates visualization showing river paths from mouths to sources

#include <SDL3/SDL_log.h>
#include <stb_image.h>
#include <lodepng.h>
#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

struct RiverSourceConfig {
    std::string heightmapPath;
    std::string outputDir;

    float seaLevel = 0.0f;
    float terrainSize = 16384.0f;
    float minAltitude = 0.0f;
    float maxAltitude = 200.0f;
    uint32_t outputResolution = 2048;

    float riverFlowThreshold = 0.3f;  // Normalized flow threshold for river
    float sourceFlowThreshold = 0.15f; // Lower threshold to trace further upstream
    uint32_t maxTraceLength = 5000;   // Max pixels to trace upstream
};

// A traced river from mouth to source
struct RiverPath {
    std::vector<glm::ivec2> pixels;      // Pixel coordinates along path
    std::vector<float> heights;           // Height at each point
    std::vector<float> flows;             // Flow at each point
    glm::ivec2 mouthPixel;                // Where river meets sea
    glm::ivec2 sourcePixel;               // Highest point (source)
    float mouthHeight;
    float sourceHeight;
    float totalFlow;
};

class RiverSourceGenerator {
public:
    bool generate(const RiverSourceConfig& config);
    bool saveVisualization(const std::string& path) const;
    bool saveRiverPaths(const std::string& path) const;

private:
    bool loadHeightmap(const std::string& path);
    void computeFlowDirectionsAndAccumulation();
    void findRiverMouths();
    void traceUpstream();
    RiverPath traceRiverUpstream(int startX, int startY);

    float getHeight(int x, int y) const;
    float getFlow(int x, int y) const;
    int8_t getFlowDir(int x, int y) const;

    // Build reverse flow lookup - which cells flow INTO each cell
    void buildUpstreamLookup();
    std::vector<glm::ivec2> getUpstreamNeighbors(int x, int y) const;

    RiverSourceConfig config;

    // Heightmap data
    std::vector<float> heightData;
    uint32_t heightmapWidth = 0;
    uint32_t heightmapHeight = 0;

    // Flow data (computed at output resolution)
    std::vector<float> flowAccumulation;
    std::vector<int8_t> flowDirection;
    uint32_t flowWidth = 0;
    uint32_t flowHeight = 0;

    // Reverse lookup: for each cell, which cells flow into it
    std::vector<std::vector<glm::ivec2>> upstreamCells;

    // River mouths (where rivers meet sea)
    std::vector<glm::ivec2> riverMouths;

    // Traced river paths
    std::vector<RiverPath> riverPaths;

    // D8 direction offsets
    static constexpr int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static constexpr float dist[8] = {1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f};
};

constexpr int RiverSourceGenerator::dx[8];
constexpr int RiverSourceGenerator::dy[8];
constexpr float RiverSourceGenerator::dist[8];

float RiverSourceGenerator::getHeight(int x, int y) const {
    // Map from flow coords to heightmap coords
    float hx = static_cast<float>(x) / flowWidth * heightmapWidth;
    float hy = static_cast<float>(y) / flowHeight * heightmapHeight;

    int ix = std::clamp(static_cast<int>(hx), 0, static_cast<int>(heightmapWidth) - 1);
    int iy = std::clamp(static_cast<int>(hy), 0, static_cast<int>(heightmapHeight) - 1);

    return heightData[iy * heightmapWidth + ix];
}

float RiverSourceGenerator::getFlow(int x, int y) const {
    if (x < 0 || x >= static_cast<int>(flowWidth) ||
        y < 0 || y >= static_cast<int>(flowHeight)) return 0.0f;
    return flowAccumulation[y * flowWidth + x];
}

int8_t RiverSourceGenerator::getFlowDir(int x, int y) const {
    if (x < 0 || x >= static_cast<int>(flowWidth) ||
        y < 0 || y >= static_cast<int>(flowHeight)) return -1;
    return flowDirection[y * flowWidth + x];
}

bool RiverSourceGenerator::loadHeightmap(const std::string& path) {
    int width, height, channels;
    uint16_t* data16 = stbi_load_16(path.c_str(), &width, &height, &channels, 1);

    if (!data16) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s", path.c_str());
        return false;
    }

    heightmapWidth = static_cast<uint32_t>(width);
    heightmapHeight = static_cast<uint32_t>(height);
    heightData.resize(heightmapWidth * heightmapHeight);

    float heightRange = config.maxAltitude - config.minAltitude;

    for (uint32_t i = 0; i < heightmapWidth * heightmapHeight; i++) {
        float normalized = static_cast<float>(data16[i]) / 65535.0f;
        heightData[i] = config.minAltitude + normalized * heightRange;
    }

    stbi_image_free(data16);
    SDL_Log("Loaded heightmap: %ux%u", heightmapWidth, heightmapHeight);
    return true;
}

void RiverSourceGenerator::computeFlowDirectionsAndAccumulation() {
    flowWidth = config.outputResolution;
    flowHeight = config.outputResolution;

    flowAccumulation.resize(flowWidth * flowHeight, 0.0f);
    flowDirection.resize(flowWidth * flowHeight, -1);

    SDL_Log("Computing flow directions (%ux%u)...", flowWidth, flowHeight);

    // Compute flow direction for each cell
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            float h = getHeight(x, y);

            // Cells below sea level are outlets
            if (h <= config.seaLevel) {
                flowDirection[y * flowWidth + x] = -1;
                continue;
            }

            // Find steepest downhill neighbor
            float maxSlope = 0.0f;
            int bestDir = -1;
            float lowestH = h;
            int lowestDir = -1;

            for (int d = 0; d < 8; d++) {
                int nx = static_cast<int>(x) + dx[d];
                int ny = static_cast<int>(y) + dy[d];

                if (nx < 0 || nx >= static_cast<int>(flowWidth) ||
                    ny < 0 || ny >= static_cast<int>(flowHeight)) continue;

                float nh = getHeight(nx, ny);

                if (nh < lowestH) {
                    lowestH = nh;
                    lowestDir = d;
                }

                float slope = (h - nh) / dist[d];
                if (slope > maxSlope) {
                    maxSlope = slope;
                    bestDir = d;
                }
            }

            // If flat or pit, flow to lowest neighbor
            if (bestDir < 0 && lowestDir >= 0) {
                bestDir = lowestDir;
            }

            flowDirection[y * flowWidth + x] = static_cast<int8_t>(bestDir);
        }
    }

    SDL_Log("Computing flow accumulation...");

    // Count in-degree for each cell
    std::vector<uint32_t> inDegree(flowWidth * flowHeight, 0);
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            int dir = flowDirection[y * flowWidth + x];
            if (dir >= 0 && dir < 8) {
                int nx = static_cast<int>(x) + dx[dir];
                int ny = static_cast<int>(y) + dy[dir];
                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    inDegree[ny * flowWidth + nx]++;
                }
            }
        }
    }

    // Initialize flow accumulation
    for (auto& f : flowAccumulation) f = 1.0f;

    // Process in topological order
    std::queue<std::pair<uint32_t, uint32_t>> toProcess;
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            if (inDegree[y * flowWidth + x] == 0) {
                toProcess.push({x, y});
            }
        }
    }

    while (!toProcess.empty()) {
        auto [x, y] = toProcess.front();
        toProcess.pop();

        int dir = flowDirection[y * flowWidth + x];
        if (dir >= 0 && dir < 8) {
            int nx = static_cast<int>(x) + dx[dir];
            int ny = static_cast<int>(y) + dy[dir];

            if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                ny >= 0 && ny < static_cast<int>(flowHeight)) {
                flowAccumulation[ny * flowWidth + nx] += flowAccumulation[y * flowWidth + x];
                inDegree[ny * flowWidth + nx]--;

                if (inDegree[ny * flowWidth + nx] == 0) {
                    toProcess.push({static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)});
                }
            }
        }
    }

    // Normalize using log scale
    float maxFlow = *std::max_element(flowAccumulation.begin(), flowAccumulation.end());
    SDL_Log("Max flow accumulation: %.0f cells", maxFlow);

    float logMax = std::log(maxFlow + 1.0f);
    for (auto& f : flowAccumulation) {
        f = std::log(f + 1.0f) / logMax;
    }
}

void RiverSourceGenerator::buildUpstreamLookup() {
    SDL_Log("Building upstream cell lookup...");

    upstreamCells.resize(flowWidth * flowHeight);

    // For each cell, find all cells that flow into it
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            int dir = flowDirection[y * flowWidth + x];
            if (dir >= 0 && dir < 8) {
                int nx = static_cast<int>(x) + dx[dir];
                int ny = static_cast<int>(y) + dy[dir];

                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    upstreamCells[ny * flowWidth + nx].push_back(glm::ivec2(x, y));
                }
            }
        }
    }

    // Sort upstream cells by flow (highest first) for better tracing
    for (auto& cells : upstreamCells) {
        std::sort(cells.begin(), cells.end(), [this](const glm::ivec2& a, const glm::ivec2& b) {
            return getFlow(a.x, a.y) > getFlow(b.x, b.y);
        });
    }
}

std::vector<glm::ivec2> RiverSourceGenerator::getUpstreamNeighbors(int x, int y) const {
    if (x < 0 || x >= static_cast<int>(flowWidth) ||
        y < 0 || y >= static_cast<int>(flowHeight)) {
        return {};
    }
    return upstreamCells[y * flowWidth + x];
}

void RiverSourceGenerator::findRiverMouths() {
    SDL_Log("Finding river mouths...");

    // River mouths are cells that:
    // 1. Are above sea level
    // 2. Have high flow accumulation (are rivers)
    // 3. Flow directly into a cell at or below sea level

    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            float h = getHeight(x, y);
            float flow = getFlow(x, y);
            int dir = getFlowDir(x, y);

            // Must be above sea level with significant flow
            if (h <= config.seaLevel || flow < config.riverFlowThreshold) continue;

            // Check where it flows to
            if (dir >= 0 && dir < 8) {
                int nx = static_cast<int>(x) + dx[dir];
                int ny = static_cast<int>(y) + dy[dir];

                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    float nh = getHeight(nx, ny);

                    // If it flows into sea, this is a river mouth
                    if (nh <= config.seaLevel) {
                        riverMouths.push_back(glm::ivec2(x, y));
                    }
                }
            } else {
                // Outlet cell above sea level = river mouth
                riverMouths.push_back(glm::ivec2(x, y));
            }
        }
    }

    // Sort mouths by flow (highest first)
    std::sort(riverMouths.begin(), riverMouths.end(), [this](const glm::ivec2& a, const glm::ivec2& b) {
        return getFlow(a.x, a.y) > getFlow(b.x, b.y);
    });

    SDL_Log("Found %zu river mouths", riverMouths.size());
}

RiverPath RiverSourceGenerator::traceRiverUpstream(int startX, int startY) {
    RiverPath path;
    path.mouthPixel = glm::ivec2(startX, startY);
    path.mouthHeight = getHeight(startX, startY);
    path.totalFlow = getFlow(startX, startY);

    std::vector<bool> visited(flowWidth * flowHeight, false);

    // Use priority queue to follow main channel (highest flow upstream)
    auto cmp = [this](const glm::ivec2& a, const glm::ivec2& b) {
        return getFlow(a.x, a.y) < getFlow(b.x, b.y);
    };
    std::priority_queue<glm::ivec2, std::vector<glm::ivec2>, decltype(cmp)> pq(cmp);

    pq.push(glm::ivec2(startX, startY));
    visited[startY * flowWidth + startX] = true;

    glm::ivec2 highestPoint = path.mouthPixel;
    float highestHeight = path.mouthHeight;

    while (!pq.empty() && path.pixels.size() < config.maxTraceLength) {
        glm::ivec2 current = pq.top();
        pq.pop();

        float h = getHeight(current.x, current.y);
        float flow = getFlow(current.x, current.y);

        path.pixels.push_back(current);
        path.heights.push_back(h);
        path.flows.push_back(flow);

        if (h > highestHeight) {
            highestHeight = h;
            highestPoint = current;
        }

        // Get all upstream neighbors
        auto upstream = getUpstreamNeighbors(current.x, current.y);

        // Follow the main channel: highest flow upstream cell that meets threshold
        bool foundUpstream = false;
        for (const auto& up : upstream) {
            if (visited[up.y * flowWidth + up.x]) continue;

            float upFlow = getFlow(up.x, up.y);
            if (upFlow >= config.sourceFlowThreshold) {
                visited[up.y * flowWidth + up.x] = true;
                pq.push(up);
                foundUpstream = true;
                break;  // Only follow main channel
            }
        }

        // If no more high-flow upstream, we've reached near the source
        if (!foundUpstream) break;
    }

    path.sourcePixel = highestPoint;
    path.sourceHeight = highestHeight;

    return path;
}

void RiverSourceGenerator::traceUpstream() {
    SDL_Log("Tracing rivers upstream to sources...");

    std::vector<bool> mouthUsed(flowWidth * flowHeight, false);

    for (const auto& mouth : riverMouths) {
        // Skip if this mouth area was already traced
        bool tooClose = false;
        for (int dy = -5; dy <= 5 && !tooClose; dy++) {
            for (int dx = -5; dx <= 5 && !tooClose; dx++) {
                int nx = mouth.x + dx;
                int ny = mouth.y + dy;
                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    if (mouthUsed[ny * flowWidth + nx]) {
                        tooClose = true;
                    }
                }
            }
        }

        if (tooClose) continue;

        // Mark this area as used
        for (int dy = -10; dy <= 10; dy++) {
            for (int dx = -10; dx <= 10; dx++) {
                int nx = mouth.x + dx;
                int ny = mouth.y + dy;
                if (nx >= 0 && nx < static_cast<int>(flowWidth) &&
                    ny >= 0 && ny < static_cast<int>(flowHeight)) {
                    mouthUsed[ny * flowWidth + nx] = true;
                }
            }
        }

        RiverPath path = traceRiverUpstream(mouth.x, mouth.y);

        // Only keep paths with significant length
        if (path.pixels.size() >= 20) {
            riverPaths.push_back(std::move(path));
            SDL_Log("  River %zu: %zu pixels, source height %.1fm at (%d, %d)",
                    riverPaths.size(), riverPaths.back().pixels.size(),
                    riverPaths.back().sourceHeight,
                    riverPaths.back().sourcePixel.x, riverPaths.back().sourcePixel.y);
        }
    }

    SDL_Log("Traced %zu rivers to their sources", riverPaths.size());
}

bool RiverSourceGenerator::generate(const RiverSourceConfig& cfg) {
    config = cfg;

    if (!loadHeightmap(config.heightmapPath)) return false;

    computeFlowDirectionsAndAccumulation();
    buildUpstreamLookup();
    findRiverMouths();
    traceUpstream();

    return true;
}

bool RiverSourceGenerator::saveVisualization(const std::string& path) const {
    SDL_Log("Generating visualization...");

    std::vector<uint8_t> pixels(flowWidth * flowHeight * 4);

    // Base layer: terrain with sea
    for (uint32_t y = 0; y < flowHeight; y++) {
        for (uint32_t x = 0; x < flowWidth; x++) {
            size_t idx = (y * flowWidth + x) * 4;
            float h = getHeight(x, y);

            if (h <= config.seaLevel) {
                // Sea - dark blue
                pixels[idx + 0] = 30;
                pixels[idx + 1] = 80;
                pixels[idx + 2] = 150;
            } else {
                // Land - grayscale based on height
                float normalized = (h - config.seaLevel) / (config.maxAltitude - config.seaLevel);
                normalized = std::clamp(normalized, 0.0f, 1.0f);
                uint8_t gray = static_cast<uint8_t>(60 + normalized * 140);
                pixels[idx + 0] = gray;
                pixels[idx + 1] = gray;
                pixels[idx + 2] = gray;
            }
            pixels[idx + 3] = 255;
        }
    }

    // Draw river paths with gradient from mouth (blue) to source (red/orange)
    for (size_t riverIdx = 0; riverIdx < riverPaths.size(); riverIdx++) {
        const auto& river = riverPaths[riverIdx];
        size_t pathLen = river.pixels.size();

        for (size_t i = 0; i < pathLen; i++) {
            const auto& p = river.pixels[i];

            // Color gradient: blue at mouth -> cyan -> green -> yellow -> orange at source
            float t = static_cast<float>(i) / static_cast<float>(pathLen);

            uint8_t r, g, b;
            if (t < 0.25f) {
                // Blue to cyan
                float lt = t / 0.25f;
                r = 0;
                g = static_cast<uint8_t>(lt * 255);
                b = 255;
            } else if (t < 0.5f) {
                // Cyan to green
                float lt = (t - 0.25f) / 0.25f;
                r = 0;
                g = 255;
                b = static_cast<uint8_t>((1 - lt) * 255);
            } else if (t < 0.75f) {
                // Green to yellow
                float lt = (t - 0.5f) / 0.25f;
                r = static_cast<uint8_t>(lt * 255);
                g = 255;
                b = 0;
            } else {
                // Yellow to orange/red
                float lt = (t - 0.75f) / 0.25f;
                r = 255;
                g = static_cast<uint8_t>((1 - lt * 0.5f) * 255);
                b = 0;
            }

            // Draw a 3x3 pixel for visibility
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int px = p.x + dx;
                    int py = p.y + dy;
                    if (px >= 0 && px < static_cast<int>(flowWidth) &&
                        py >= 0 && py < static_cast<int>(flowHeight)) {
                        size_t idx = (py * flowWidth + px) * 4;
                        pixels[idx + 0] = r;
                        pixels[idx + 1] = g;
                        pixels[idx + 2] = b;
                    }
                }
            }
        }

        // Mark source with white circle
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                if (dx * dx + dy * dy <= 16) {
                    int px = river.sourcePixel.x + dx;
                    int py = river.sourcePixel.y + dy;
                    if (px >= 0 && px < static_cast<int>(flowWidth) &&
                        py >= 0 && py < static_cast<int>(flowHeight)) {
                        size_t idx = (py * flowWidth + px) * 4;
                        pixels[idx + 0] = 255;
                        pixels[idx + 1] = 255;
                        pixels[idx + 2] = 255;
                    }
                }
            }
        }

        // Mark mouth with cyan circle
        for (int dy = -4; dy <= 4; dy++) {
            for (int dx = -4; dx <= 4; dx++) {
                if (dx * dx + dy * dy <= 16) {
                    int px = river.mouthPixel.x + dx;
                    int py = river.mouthPixel.y + dy;
                    if (px >= 0 && px < static_cast<int>(flowWidth) &&
                        py >= 0 && py < static_cast<int>(flowHeight)) {
                        size_t idx = (py * flowWidth + px) * 4;
                        pixels[idx + 0] = 0;
                        pixels[idx + 1] = 255;
                        pixels[idx + 2] = 255;
                    }
                }
            }
        }
    }

    unsigned error = lodepng::encode(path, pixels, flowWidth, flowHeight);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save visualization: %s",
                     lodepng_error_text(error));
        return false;
    }

    SDL_Log("Saved river sources visualization: %s (%ux%u)", path.c_str(), flowWidth, flowHeight);
    return true;
}

bool RiverSourceGenerator::saveRiverPaths(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create river paths file: %s",
                     path.c_str());
        return false;
    }

    float pixelToWorld = config.terrainSize / flowWidth;

    file << "{\n";
    file << "  \"terrain_size\": " << config.terrainSize << ",\n";
    file << "  \"sea_level\": " << config.seaLevel << ",\n";
    file << "  \"num_rivers\": " << riverPaths.size() << ",\n";
    file << "  \"rivers\": [\n";

    for (size_t i = 0; i < riverPaths.size(); i++) {
        const auto& river = riverPaths[i];

        file << "    {\n";
        file << "      \"id\": " << i << ",\n";
        file << "      \"num_points\": " << river.pixels.size() << ",\n";
        file << "      \"mouth\": {\"x\": " << river.mouthPixel.x * pixelToWorld
             << ", \"z\": " << river.mouthPixel.y * pixelToWorld
             << ", \"height\": " << river.mouthHeight << "},\n";
        file << "      \"source\": {\"x\": " << river.sourcePixel.x * pixelToWorld
             << ", \"z\": " << river.sourcePixel.y * pixelToWorld
             << ", \"height\": " << river.sourceHeight << "},\n";
        file << "      \"total_flow\": " << river.totalFlow << ",\n";

        // Output simplified path (every Nth point for smaller file)
        file << "      \"path\": [\n";
        size_t step = std::max(size_t(1), river.pixels.size() / 50);  // ~50 points per river
        for (size_t j = 0; j < river.pixels.size(); j += step) {
            const auto& p = river.pixels[j];
            file << "        {\"x\": " << p.x * pixelToWorld
                 << ", \"z\": " << p.y * pixelToWorld
                 << ", \"h\": " << river.heights[j] << "}";
            if (j + step < river.pixels.size()) file << ",";
            file << "\n";
        }
        file << "      ]\n";
        file << "    }";
        if (i < riverPaths.size() - 1) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    SDL_Log("Saved river paths: %s", path.c_str());
    return true;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <output_dir> [options]\n"
              << "\n"
              << "Traces rivers upstream from where they meet sea level to find sources.\n"
              << "Generates visualization showing river paths from coast to headwaters.\n"
              << "\n"
              << "Arguments:\n"
              << "  heightmap.png    16-bit PNG heightmap file\n"
              << "  output_dir       Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --sea-level <value>         Height below which is sea (default: 0.0)\n"
              << "  --terrain-size <value>      World size in meters (default: 16384.0)\n"
              << "  --min-altitude <value>      Min altitude in heightmap (default: 0.0)\n"
              << "  --max-altitude <value>      Max altitude in heightmap (default: 200.0)\n"
              << "  --output-resolution <value> Analysis resolution (default: 2048)\n"
              << "  --river-threshold <value>   Flow threshold for rivers (default: 0.3)\n"
              << "  --source-threshold <value>  Flow threshold for sources (default: 0.15)\n"
              << "  --help                      Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  river_sources.png    Visualization of rivers from mouth to source\n"
              << "  river_paths.json     River path data in JSON format\n"
              << "\n"
              << "Color gradient in visualization:\n"
              << "  Blue   = River mouth (where it meets sea)\n"
              << "  Cyan   = Lower river course\n"
              << "  Green  = Middle course\n"
              << "  Yellow = Upper course\n"
              << "  Orange = Near source (headwaters)\n"
              << "  White circles = River sources\n"
              << "  Cyan circles  = River mouths\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./river_cache --sea-level 23\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    RiverSourceConfig config;
    config.heightmapPath = argv[1];
    config.outputDir = argv[2];

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--sea-level" && i + 1 < argc) {
            config.seaLevel = std::stof(argv[++i]);
        } else if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else if (arg == "--output-resolution" && i + 1 < argc) {
            config.outputResolution = std::stoul(argv[++i]);
        } else if (arg == "--river-threshold" && i + 1 < argc) {
            config.riverFlowThreshold = std::stof(argv[++i]);
        } else if (arg == "--source-threshold" && i + 1 < argc) {
            config.sourceFlowThreshold = std::stof(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory
    std::filesystem::create_directories(config.outputDir);

    SDL_Log("River Sources Preprocessor");
    SDL_Log("==========================");
    SDL_Log("Heightmap: %s", config.heightmapPath.c_str());
    SDL_Log("Output: %s", config.outputDir.c_str());
    SDL_Log("Sea level: %.1f m", config.seaLevel);
    SDL_Log("Terrain size: %.1f m", config.terrainSize);
    SDL_Log("Altitude range: %.1f to %.1f m", config.minAltitude, config.maxAltitude);
    SDL_Log("Output resolution: %u x %u", config.outputResolution, config.outputResolution);
    SDL_Log("River threshold: %.2f", config.riverFlowThreshold);
    SDL_Log("Source threshold: %.2f", config.sourceFlowThreshold);

    RiverSourceGenerator generator;

    if (!generator.generate(config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "River source generation failed!");
        return 1;
    }

    std::string vizPath = config.outputDir + "/river_sources.png";
    std::string jsonPath = config.outputDir + "/river_paths.json";

    if (!generator.saveVisualization(vizPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save visualization!");
        return 1;
    }

    if (!generator.saveRiverPaths(jsonPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save river paths!");
        return 1;
    }

    SDL_Log("River source generation complete!");
    SDL_Log("Output files:");
    SDL_Log("  %s", vizPath.c_str());
    SDL_Log("  %s", jsonPath.c_str());

    return 0;
}
