/**
 * Terrain Patch Generator
 *
 * Generates terrain-aware Voronoi patches covering the entire map.
 * Patch density is higher near settlements and sparser in wilderness.
 *
 * Inputs:
 * - Heightmap (16-bit PNG)
 * - Rivers (GeoJSON)
 * - Settlements (JSON from settlement_generator)
 *
 * Outputs:
 * - SVG preview with all patches
 * - GeoJSON with patch data for selection by town_generator
 *
 * Natural boundaries considered:
 * - Coastlines (sea level threshold)
 * - Rivers (from GeoJSON)
 * - Terrain slope
 */

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <random>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <memory>

using json = nlohmann::json;

// ============================================================================
// Configuration
// ============================================================================

struct Config {
    std::string heightmapPath;
    std::string riversPath;
    std::string settlementsPath;
    std::string outputSvgPath = "terrain_patches.svg";
    std::string outputJsonPath = "terrain_patches.geojson";

    // Terrain parameters
    float terrainSize = 16384.0f;
    float seaLevel = 0.0f;
    float minAltitude = -15.0f;
    float maxAltitude = 200.0f;

    // Patch generation - density control
    int basePatchCount = 500;           // Base patches for wilderness
    float settlementDensityMultiplier = 8.0f;  // How much denser near settlements
    float settlementInfluenceRadius = 3.0f;    // Multiplier of settlement radius

    int seed = -1;

    // SVG output
    float svgWidth = 2048.0f;
    float svgHeight = 2048.0f;
};

// ============================================================================
// Data Structures
// ============================================================================

struct Settlement {
    uint32_t id;
    std::string type;
    glm::vec2 position;
    float radius;
    float score;
    std::vector<std::string> features;
};

struct RiverSegment {
    std::vector<glm::vec2> points;
    std::vector<float> widths;
    float flow = 0.0f;
};

struct Heightmap {
    std::vector<uint16_t> data;
    uint32_t width = 0;
    uint32_t height = 0;

    float sample(float u, float v) const {
        if (data.empty()) return 0.0f;
        u = glm::clamp(u, 0.0f, 1.0f);
        v = glm::clamp(v, 0.0f, 1.0f);

        float fx = u * (width - 1);
        float fy = v * (height - 1);

        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, static_cast<int>(width - 1));
        int y1 = std::min(y0 + 1, static_cast<int>(height - 1));

        float tx = fx - x0;
        float ty = fy - y0;

        float h00 = data[y0 * width + x0] / 65535.0f;
        float h10 = data[y0 * width + x1] / 65535.0f;
        float h01 = data[y1 * width + x0] / 65535.0f;
        float h11 = data[y1 * width + x1] / 65535.0f;

        float h0 = h00 * (1.0f - tx) + h10 * tx;
        float h1 = h01 * (1.0f - tx) + h11 * tx;

        return h0 * (1.0f - ty) + h1 * ty;
    }

    float sampleWorld(float worldX, float worldZ, float terrainSize) const {
        float u = worldX / terrainSize;
        float v = worldZ / terrainSize;
        return sample(u, v);
    }

    float toWorldHeight(float normalizedHeight, float minAlt, float maxAlt) const {
        return minAlt + normalizedHeight * (maxAlt - minAlt);
    }
};

struct TerrainPatch {
    std::vector<glm::vec2> vertices;
    glm::vec2 center;
    float avgHeight = 0.0f;
    float avgSlope = 0.0f;
    bool isWater = false;
    bool bordersWater = false;
    bool bordersRiver = false;
    int id = 0;
    int nearestSettlementId = -1;
    float distanceToSettlement = std::numeric_limits<float>::max();
};

// ============================================================================
// Data Loading
// ============================================================================

bool loadHeightmap(const std::string& path, Heightmap& hm) {
    std::vector<unsigned char> pngData;
    unsigned w, h;

    unsigned error = lodepng::decode(pngData, w, h, path, LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to load heightmap %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    hm.width = w;
    hm.height = h;
    hm.data.resize(w * h);

    for (uint32_t i = 0; i < w * h; ++i) {
        uint16_t hi = pngData[i * 2];
        uint16_t lo = pngData[i * 2 + 1];
        hm.data[i] = (hi << 8) | lo;
    }

    SDL_Log("Loaded heightmap: %ux%u", w, h);
    return true;
}

std::vector<RiverSegment> loadRivers(const std::string& path) {
    std::vector<RiverSegment> rivers;

    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open rivers file: %s", path.c_str());
        return rivers;
    }

    try {
        json geojson = json::parse(file);

        if (geojson["type"] != "FeatureCollection") {
            return rivers;
        }

        for (const auto& feature : geojson["features"]) {
            if (feature["geometry"]["type"] != "LineString") continue;

            RiverSegment seg;
            const auto& coords = feature["geometry"]["coordinates"];
            for (const auto& coord : coords) {
                float x = coord[0].get<float>();
                float z = coord[1].get<float>();
                seg.points.emplace_back(x, z);
            }

            if (feature.contains("properties")) {
                const auto& props = feature["properties"];
                if (props.contains("widths") && props["widths"].is_array()) {
                    for (const auto& w : props["widths"]) {
                        seg.widths.push_back(w.get<float>());
                    }
                }
                if (props.contains("flow")) {
                    seg.flow = props["flow"].get<float>();
                }
            }

            while (seg.widths.size() < seg.points.size()) {
                seg.widths.push_back(5.0f);
            }

            if (seg.points.size() >= 2) {
                rivers.push_back(std::move(seg));
            }
        }

        SDL_Log("Loaded %zu river segments", rivers.size());

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to parse rivers GeoJSON: %s", e.what());
    }

    return rivers;
}

std::vector<Settlement> loadSettlements(const std::string& path) {
    std::vector<Settlement> settlements;

    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open settlements file: %s", path.c_str());
        return settlements;
    }

    try {
        json j = json::parse(file);

        if (!j.contains("settlements")) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No settlements array in file");
            return settlements;
        }

        for (const auto& sj : j["settlements"]) {
            Settlement s;
            s.id = sj["id"].get<uint32_t>();
            s.type = sj["type"].get<std::string>();
            s.position.x = sj["position"][0].get<float>();
            s.position.y = sj["position"][1].get<float>();
            s.radius = sj["radius"].get<float>();
            s.score = sj["score"].get<float>();

            if (sj.contains("features")) {
                for (const auto& f : sj["features"]) {
                    s.features.push_back(f.get<std::string>());
                }
            }

            settlements.push_back(s);
        }

        SDL_Log("Loaded %zu settlements", settlements.size());

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to parse settlements JSON: %s", e.what());
    }

    return settlements;
}

// ============================================================================
// Terrain Analysis
// ============================================================================

float computeSlope(const Heightmap& hm, float worldX, float worldZ, const Config& cfg) {
    float eps = cfg.terrainSize / hm.width;

    float hC = hm.sampleWorld(worldX, worldZ, cfg.terrainSize);
    float hL = hm.sampleWorld(worldX - eps, worldZ, cfg.terrainSize);
    float hR = hm.sampleWorld(worldX + eps, worldZ, cfg.terrainSize);
    float hU = hm.sampleWorld(worldX, worldZ - eps, cfg.terrainSize);
    float hD = hm.sampleWorld(worldX, worldZ + eps, cfg.terrainSize);

    float heightScale = cfg.maxAltitude - cfg.minAltitude;

    float dhdx = (hR - hL) * heightScale / (2.0f * eps);
    float dhdz = (hD - hU) * heightScale / (2.0f * eps);

    return std::sqrt(dhdx * dhdx + dhdz * dhdz);
}

float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    glm::vec2 ab = b - a;
    glm::vec2 ap = p - a;
    float t = glm::dot(ap, ab) / glm::dot(ab, ab);
    t = glm::clamp(t, 0.0f, 1.0f);
    glm::vec2 closest = a + t * ab;
    return glm::length(p - closest);
}

float distanceToRiver(glm::vec2 p, const std::vector<RiverSegment>& rivers) {
    float minDist = std::numeric_limits<float>::max();
    for (const auto& river : rivers) {
        for (size_t i = 0; i + 1 < river.points.size(); ++i) {
            float d = distanceToSegment(p, river.points[i], river.points[i + 1]);
            minDist = std::min(minDist, d);
        }
    }
    return minDist;
}

// ============================================================================
// Density-Based Seed Generation
// ============================================================================

float computeDensityAtPoint(glm::vec2 pos, const std::vector<Settlement>& settlements,
                            const Config& cfg) {
    // Base density
    float density = 1.0f;

    // Increase density near settlements
    for (const auto& s : settlements) {
        float dist = glm::length(pos - s.position);
        float influenceRadius = s.radius * cfg.settlementInfluenceRadius;

        if (dist < influenceRadius) {
            // Density falls off with distance
            float t = 1.0f - (dist / influenceRadius);
            t = t * t;  // Quadratic falloff

            // Larger settlements have more influence
            float sizeFactor = 1.0f;
            if (s.type == "town") sizeFactor = 2.0f;
            else if (s.type == "village") sizeFactor = 1.5f;
            else if (s.type == "fishing_village") sizeFactor = 1.3f;

            density += t * cfg.settlementDensityMultiplier * sizeFactor;
        }
    }

    return density;
}

struct VoronoiSeed {
    glm::vec2 pos;
    int id;
    int nearestSettlementId = -1;
    float distToSettlement = std::numeric_limits<float>::max();
};

std::vector<VoronoiSeed> generateAdaptiveSeeds(
    const Config& cfg,
    const Heightmap& hm,
    const std::vector<RiverSegment>& rivers,
    const std::vector<Settlement>& settlements) {

    std::vector<VoronoiSeed> seeds;

    std::mt19937 rng(cfg.seed > 0 ? cfg.seed : std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float heightScale = cfg.maxAltitude - cfg.minAltitude;
    float seaNorm = (cfg.seaLevel - cfg.minAltitude) / heightScale;

    // Grid-based sampling with variable density
    int gridRes = static_cast<int>(std::sqrt(cfg.basePatchCount));
    float cellSize = cfg.terrainSize / gridRes;

    int seedId = 0;

    for (int gy = 0; gy < gridRes; ++gy) {
        for (int gx = 0; gx < gridRes; ++gx) {
            float baseX = gx * cellSize;
            float baseZ = gy * cellSize;

            glm::vec2 cellCenter(baseX + cellSize * 0.5f, baseZ + cellSize * 0.5f);

            // Compute local density
            float density = computeDensityAtPoint(cellCenter, settlements, cfg);

            // Number of seeds in this cell based on density
            int seedsInCell = std::max(1, static_cast<int>(density));

            // For high density, subdivide the cell
            float subCellSize = cellSize / std::sqrt(static_cast<float>(seedsInCell));

            for (int si = 0; si < seedsInCell; ++si) {
                // Jittered position within subcell
                float subX = (si % static_cast<int>(std::sqrt(seedsInCell))) * subCellSize;
                float subZ = (si / static_cast<int>(std::sqrt(seedsInCell))) * subCellSize;

                float jitterX = dist(rng) * subCellSize * 0.8f;
                float jitterZ = dist(rng) * subCellSize * 0.8f;

                glm::vec2 pos(baseX + subX + jitterX, baseZ + subZ + jitterZ);

                // Clamp to terrain bounds
                pos.x = glm::clamp(pos.x, 0.0f, cfg.terrainSize - 1.0f);
                pos.y = glm::clamp(pos.y, 0.0f, cfg.terrainSize - 1.0f);

                // Check if on land
                float h = hm.sampleWorld(pos.x, pos.y, cfg.terrainSize);
                if (h <= seaNorm) {
                    continue;  // In water
                }

                // Check distance from rivers - don't place seeds too close
                float riverDist = distanceToRiver(pos, rivers);
                if (riverDist < 10.0f) {
                    continue;
                }

                // Prefer flatter areas (but don't reject steep areas entirely)
                float slope = computeSlope(hm, pos.x, pos.y, cfg);
                if (slope > 0.8f && dist(rng) > 0.2f) {
                    continue;  // Mostly reject very steep areas
                }

                VoronoiSeed seed;
                seed.pos = pos;
                seed.id = seedId++;

                // Find nearest settlement
                for (const auto& s : settlements) {
                    float d = glm::length(pos - s.position);
                    if (d < seed.distToSettlement) {
                        seed.distToSettlement = d;
                        seed.nearestSettlementId = s.id;
                    }
                }

                seeds.push_back(seed);
            }
        }
    }

    SDL_Log("Generated %zu adaptive seeds", seeds.size());
    return seeds;
}

// ============================================================================
// Voronoi Patch Computation
// ============================================================================

std::vector<TerrainPatch> computeVoronoiPatches(
    const std::vector<VoronoiSeed>& seeds,
    const Config& cfg,
    const Heightmap& hm,
    const std::vector<RiverSegment>& rivers) {

    std::vector<TerrainPatch> patches;

    if (seeds.empty()) return patches;

    // Use a grid for efficient Voronoi computation
    int resolution = 512;  // Sample grid
    float cellSize = cfg.terrainSize / resolution;

    float heightScale = cfg.maxAltitude - cfg.minAltitude;
    float seaNorm = (cfg.seaLevel - cfg.minAltitude) / heightScale;

    // Grid of which seed owns each cell
    std::vector<std::vector<int>> ownership(resolution, std::vector<int>(resolution, -1));

    // Assign each grid cell to nearest seed
    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            float worldX = (i + 0.5f) * cellSize;
            float worldZ = (j + 0.5f) * cellSize;
            glm::vec2 p(worldX, worldZ);

            float h = hm.sampleWorld(worldX, worldZ, cfg.terrainSize);
            if (h <= seaNorm) {
                ownership[j][i] = -2;  // Water
                continue;
            }

            float minDist = std::numeric_limits<float>::max();
            int nearest = -1;

            for (const auto& seed : seeds) {
                float d = glm::length(p - seed.pos);
                if (d < minDist) {
                    minDist = d;
                    nearest = seed.id;
                }
            }

            ownership[j][i] = nearest;
        }
    }

    // Extract patch boundaries
    for (const auto& seed : seeds) {
        TerrainPatch patch;
        patch.id = seed.id;
        patch.center = seed.pos;
        patch.nearestSettlementId = seed.nearestSettlementId;
        patch.distanceToSettlement = seed.distToSettlement;

        std::vector<glm::vec2> boundaryPoints;

        for (int j = 0; j < resolution - 1; ++j) {
            for (int i = 0; i < resolution - 1; ++i) {
                int o00 = ownership[j][i];
                int o10 = ownership[j][i + 1];
                int o01 = ownership[j + 1][i];
                int o11 = ownership[j + 1][i + 1];

                bool hasThis = (o00 == seed.id) || (o10 == seed.id) ||
                               (o01 == seed.id) || (o11 == seed.id);
                bool hasOther = (o00 != seed.id && o00 >= -1) ||
                                (o10 != seed.id && o10 >= -1) ||
                                (o01 != seed.id && o01 >= -1) ||
                                (o11 != seed.id && o11 >= -1);

                if (hasThis && hasOther) {
                    float worldX = (i + 0.5f) * cellSize;
                    float worldZ = (j + 0.5f) * cellSize;
                    boundaryPoints.emplace_back(worldX, worldZ);
                }

                bool hasWater = (o00 == -2) || (o10 == -2) || (o01 == -2) || (o11 == -2);
                if (hasThis && hasWater) {
                    patch.bordersWater = true;
                }
            }
        }

        if (boundaryPoints.empty()) continue;

        // Sort by angle from center
        std::sort(boundaryPoints.begin(), boundaryPoints.end(),
            [&seed](const glm::vec2& a, const glm::vec2& b) {
                float angleA = std::atan2(a.y - seed.pos.y, a.x - seed.pos.x);
                float angleB = std::atan2(b.y - seed.pos.y, b.x - seed.pos.x);
                return angleA < angleB;
            });

        patch.vertices = std::move(boundaryPoints);

        // Compute properties
        float h = hm.sampleWorld(seed.pos.x, seed.pos.y, cfg.terrainSize);
        patch.avgHeight = cfg.minAltitude + h * heightScale;
        patch.avgSlope = computeSlope(hm, seed.pos.x, seed.pos.y, cfg);

        float riverDist = distanceToRiver(seed.pos, rivers);
        patch.bordersRiver = (riverDist < 100.0f);

        patches.push_back(std::move(patch));
    }

    SDL_Log("Computed %zu Voronoi patches", patches.size());
    return patches;
}

// ============================================================================
// Output: SVG
// ============================================================================

std::string colorForPatch(const TerrainPatch& patch, const std::vector<Settlement>& settlements) {
    // Near settlement - warmer colors
    if (patch.distanceToSettlement < 500.0f) {
        if (patch.bordersWater) return "#a0c4e8";  // Waterfront
        if (patch.bordersRiver) return "#90d4a8";  // Riverside

        // Color by settlement type
        for (const auto& s : settlements) {
            if (s.id == static_cast<uint32_t>(patch.nearestSettlementId)) {
                if (s.type == "town") return "#e8c078";
                if (s.type == "village") return "#d4b878";
                if (s.type == "fishing_village") return "#a8c8d4";
                return "#c8b890";
            }
        }
    }

    // Wilderness - color by height
    if (patch.bordersWater) return "#8090a0";

    float t = glm::clamp((patch.avgHeight + 15.0f) / 150.0f, 0.0f, 1.0f);
    int r = static_cast<int>(140 - t * 40);
    int g = static_cast<int>(160 + t * 30);
    int b = static_cast<int>(120 - t * 30);

    char buf[16];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return buf;
}

void saveSVG(const std::string& path,
             const Config& cfg,
             const std::vector<TerrainPatch>& patches,
             const std::vector<RiverSegment>& rivers,
             const std::vector<Settlement>& settlements) {

    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create SVG file: %s", path.c_str());
        return;
    }

    auto toSVG = [&](glm::vec2 world) -> glm::vec2 {
        float x = (world.x / cfg.terrainSize) * cfg.svgWidth;
        float y = (world.y / cfg.terrainSize) * cfg.svgHeight;
        return glm::vec2(x, y);
    };

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << cfg.svgWidth << "\" height=\"" << cfg.svgHeight << "\" "
        << "viewBox=\"0 0 " << cfg.svgWidth << " " << cfg.svgHeight << "\">\n";

    out << "  <rect width=\"100%\" height=\"100%\" fill=\"#4080c0\"/>\n";  // Sea color

    out << "  <text x=\"10\" y=\"25\" font-family=\"sans-serif\" font-size=\"14\" "
        << "fill=\"#fff\">Terrain Patches - " << patches.size() << " patches, "
        << settlements.size() << " settlements</text>\n";

    // Patches
    out << "  <g id=\"patches\">\n";
    for (const auto& patch : patches) {
        if (patch.vertices.size() < 3) continue;

        std::string color = colorForPatch(patch, settlements);

        out << "    <path d=\"M";
        for (size_t i = 0; i < patch.vertices.size(); ++i) {
            glm::vec2 p = toSVG(patch.vertices[i]);
            out << (i > 0 ? " L" : "") << p.x << "," << p.y;
        }
        out << " Z\" fill=\"" << color << "\" stroke=\"#303030\" stroke-width=\"0.5\" "
            << "fill-opacity=\"0.85\"/>\n";
    }
    out << "  </g>\n";

    // Rivers
    out << "  <g id=\"rivers\">\n";
    for (const auto& river : rivers) {
        if (river.points.size() < 2) continue;

        out << "    <path d=\"M";
        for (size_t i = 0; i < river.points.size(); ++i) {
            glm::vec2 p = toSVG(river.points[i]);
            out << (i > 0 ? " L" : "") << p.x << "," << p.y;
        }

        float avgWidth = 5.0f;
        if (!river.widths.empty()) {
            avgWidth = 0.0f;
            for (float w : river.widths) avgWidth += w;
            avgWidth /= river.widths.size();
        }
        float strokeWidth = avgWidth / cfg.terrainSize * cfg.svgWidth;
        strokeWidth = glm::clamp(strokeWidth, 1.0f, 10.0f);

        out << "\" fill=\"none\" stroke=\"#2060a0\" stroke-width=\""
            << strokeWidth << "\" stroke-linecap=\"round\"/>\n";
    }
    out << "  </g>\n";

    // Settlements
    out << "  <g id=\"settlements\">\n";
    for (const auto& s : settlements) {
        glm::vec2 p = toSVG(s.position);
        float r = s.radius / cfg.terrainSize * cfg.svgWidth;
        r = glm::clamp(r, 5.0f, 30.0f);

        std::string fillColor = "#c02020";
        if (s.type == "town") fillColor = "#d04040";
        else if (s.type == "village") fillColor = "#d08040";
        else if (s.type == "fishing_village") fillColor = "#4080d0";
        else if (s.type == "hamlet") fillColor = "#a08040";

        out << "    <circle cx=\"" << p.x << "\" cy=\"" << p.y
            << "\" r=\"" << r << "\" fill=\"" << fillColor
            << "\" stroke=\"#000\" stroke-width=\"1\"/>\n";

        out << "    <text x=\"" << p.x << "\" y=\"" << (p.y - r - 3)
            << "\" font-family=\"sans-serif\" font-size=\"10\" "
            << "text-anchor=\"middle\" fill=\"#fff\">" << s.type << " " << s.id << "</text>\n";
    }
    out << "  </g>\n";

    // Legend
    float legendY = cfg.svgHeight - 100;
    out << "  <g id=\"legend\" transform=\"translate(10," << legendY << ")\">\n";
    out << "    <rect x=\"0\" y=\"0\" width=\"200\" height=\"90\" "
        << "fill=\"#000\" fill-opacity=\"0.6\"/>\n";
    out << "    <text x=\"5\" y=\"15\" font-family=\"sans-serif\" font-size=\"11\" "
        << "font-weight=\"bold\" fill=\"#fff\">Legend</text>\n";
    out << "    <rect x=\"5\" y=\"22\" width=\"15\" height=\"10\" fill=\"#e8c078\"/>\n";
    out << "    <text x=\"25\" y=\"31\" font-family=\"sans-serif\" font-size=\"9\" fill=\"#fff\">"
        << "Town area</text>\n";
    out << "    <rect x=\"5\" y=\"36\" width=\"15\" height=\"10\" fill=\"#90d4a8\"/>\n";
    out << "    <text x=\"25\" y=\"45\" font-family=\"sans-serif\" font-size=\"9\" fill=\"#fff\">"
        << "Riverside</text>\n";
    out << "    <rect x=\"5\" y=\"50\" width=\"15\" height=\"10\" fill=\"#a0c4e8\"/>\n";
    out << "    <text x=\"25\" y=\"59\" font-family=\"sans-serif\" font-size=\"9\" fill=\"#fff\">"
        << "Waterfront</text>\n";
    out << "    <rect x=\"5\" y=\"64\" width=\"15\" height=\"10\" fill=\"#8ca078\"/>\n";
    out << "    <text x=\"25\" y=\"73\" font-family=\"sans-serif\" font-size=\"9\" fill=\"#fff\">"
        << "Wilderness</text>\n";
    out << "  </g>\n";

    out << "</svg>\n";
    SDL_Log("Saved SVG: %s", path.c_str());
}

// ============================================================================
// Output: GeoJSON
// ============================================================================

void saveGeoJSON(const std::string& path,
                 const Config& cfg,
                 const std::vector<TerrainPatch>& patches,
                 const std::vector<Settlement>& settlements) {

    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create GeoJSON file: %s", path.c_str());
        return;
    }

    json geojson;
    geojson["type"] = "FeatureCollection";
    geojson["properties"] = {
        {"terrain_size", cfg.terrainSize},
        {"patch_count", patches.size()},
        {"settlement_count", settlements.size()}
    };

    json features = json::array();

    for (const auto& patch : patches) {
        if (patch.vertices.size() < 3) continue;

        json feature;
        feature["type"] = "Feature";
        feature["id"] = patch.id;

        // Polygon geometry (close the ring)
        json coords = json::array();
        json ring = json::array();
        for (const auto& v : patch.vertices) {
            ring.push_back({v.x, v.y});
        }
        ring.push_back({patch.vertices[0].x, patch.vertices[0].y});  // Close ring
        coords.push_back(ring);

        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", coords}
        };

        // Properties
        feature["properties"] = {
            {"center", {patch.center.x, patch.center.y}},
            {"avg_height", patch.avgHeight},
            {"avg_slope", patch.avgSlope},
            {"borders_water", patch.bordersWater},
            {"borders_river", patch.bordersRiver},
            {"nearest_settlement_id", patch.nearestSettlementId},
            {"distance_to_settlement", patch.distanceToSettlement}
        };

        // Add settlement info if close
        if (patch.nearestSettlementId >= 0 && patch.distanceToSettlement < 1000.0f) {
            for (const auto& s : settlements) {
                if (s.id == static_cast<uint32_t>(patch.nearestSettlementId)) {
                    feature["properties"]["settlement_type"] = s.type;
                    break;
                }
            }
        }

        features.push_back(feature);
    }

    geojson["features"] = features;

    out << geojson.dump(2);
    SDL_Log("Saved GeoJSON: %s (%zu patches)", path.c_str(), patches.size());
}

// ============================================================================
// Command Line Parsing
// ============================================================================

void printUsage(const char* prog) {
    SDL_Log("Usage: %s [options]", prog);
    SDL_Log("Options:");
    SDL_Log("  --heightmap <path>         Path to 16-bit PNG heightmap (required)");
    SDL_Log("  --rivers <path>            Path to rivers.geojson");
    SDL_Log("  --settlements <path>       Path to settlements.json");
    SDL_Log("  --output-svg <path>        Output SVG path (default: terrain_patches.svg)");
    SDL_Log("  --output-json <path>       Output GeoJSON path (default: terrain_patches.geojson)");
    SDL_Log("  --base-patches <n>         Base patch count for wilderness (default: 500)");
    SDL_Log("  --density-mult <f>         Settlement density multiplier (default: 8)");
    SDL_Log("  --influence-radius <f>     Settlement influence radius multiplier (default: 3)");
    SDL_Log("  --terrain-size <m>         Terrain size in meters (default: 16384)");
    SDL_Log("  --sea-level <m>            Sea level height (default: 0)");
    SDL_Log("  --min-alt <m>              Minimum altitude (default: -15)");
    SDL_Log("  --max-alt <m>              Maximum altitude (default: 200)");
    SDL_Log("  --seed <n>                 Random seed");
    SDL_Log("  --svg-size <w,h>           SVG dimensions (default: 2048,2048)");
}

bool parseArgs(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--heightmap" && i + 1 < argc) {
            cfg.heightmapPath = argv[++i];
        }
        else if (arg == "--rivers" && i + 1 < argc) {
            cfg.riversPath = argv[++i];
        }
        else if (arg == "--settlements" && i + 1 < argc) {
            cfg.settlementsPath = argv[++i];
        }
        else if (arg == "--output-svg" && i + 1 < argc) {
            cfg.outputSvgPath = argv[++i];
        }
        else if (arg == "--output-json" && i + 1 < argc) {
            cfg.outputJsonPath = argv[++i];
        }
        else if (arg == "--base-patches" && i + 1 < argc) {
            cfg.basePatchCount = std::stoi(argv[++i]);
        }
        else if (arg == "--density-mult" && i + 1 < argc) {
            cfg.settlementDensityMultiplier = std::stof(argv[++i]);
        }
        else if (arg == "--influence-radius" && i + 1 < argc) {
            cfg.settlementInfluenceRadius = std::stof(argv[++i]);
        }
        else if (arg == "--terrain-size" && i + 1 < argc) {
            cfg.terrainSize = std::stof(argv[++i]);
        }
        else if (arg == "--sea-level" && i + 1 < argc) {
            cfg.seaLevel = std::stof(argv[++i]);
        }
        else if (arg == "--min-alt" && i + 1 < argc) {
            cfg.minAltitude = std::stof(argv[++i]);
        }
        else if (arg == "--max-alt" && i + 1 < argc) {
            cfg.maxAltitude = std::stof(argv[++i]);
        }
        else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoi(argv[++i]);
        }
        else if (arg == "--svg-size" && i + 1 < argc) {
            std::string val = argv[++i];
            size_t comma = val.find(',');
            if (comma != std::string::npos) {
                cfg.svgWidth = std::stof(val.substr(0, comma));
                cfg.svgHeight = std::stof(val.substr(comma + 1));
            }
        }
    }

    if (cfg.heightmapPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Heightmap path is required");
        printUsage(argv[0]);
        return false;
    }

    return true;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    Config cfg;

    if (!parseArgs(argc, argv, cfg)) {
        return 1;
    }

    SDL_Log("Terrain Patch Generator (Full Map)");
    SDL_Log("  Heightmap: %s", cfg.heightmapPath.c_str());
    SDL_Log("  Rivers: %s", cfg.riversPath.empty() ? "(none)" : cfg.riversPath.c_str());
    SDL_Log("  Settlements: %s", cfg.settlementsPath.empty() ? "(none)" : cfg.settlementsPath.c_str());
    SDL_Log("  Base patches: %d", cfg.basePatchCount);
    SDL_Log("  Density multiplier: %.1f", cfg.settlementDensityMultiplier);

    // Load data
    Heightmap hm;
    if (!loadHeightmap(cfg.heightmapPath, hm)) {
        return 1;
    }

    std::vector<RiverSegment> rivers;
    if (!cfg.riversPath.empty()) {
        rivers = loadRivers(cfg.riversPath);
    }

    std::vector<Settlement> settlements;
    if (!cfg.settlementsPath.empty()) {
        settlements = loadSettlements(cfg.settlementsPath);
    }

    // Generate adaptive seeds
    std::vector<VoronoiSeed> seeds = generateAdaptiveSeeds(cfg, hm, rivers, settlements);

    // Compute Voronoi patches
    std::vector<TerrainPatch> patches = computeVoronoiPatches(seeds, cfg, hm, rivers);

    // Save outputs
    saveSVG(cfg.outputSvgPath, cfg, patches, rivers, settlements);
    saveGeoJSON(cfg.outputJsonPath, cfg, patches, settlements);

    SDL_Log("Done!");

    return 0;
}
