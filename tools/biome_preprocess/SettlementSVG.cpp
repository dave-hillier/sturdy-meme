#include "SettlementSVG.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <vector>

// Simple hash-based noise for deterministic variation
static float hashNoise(uint32_t seed, int index) {
    uint32_t h = seed + static_cast<uint32_t>(index) * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Get color for settlement type
static const char* getSettlementColor(SettlementType type) {
    switch (type) {
        case SettlementType::Town:           return "#cc3333";  // Red
        case SettlementType::Village:        return "#cc6633";  // Orange
        case SettlementType::FishingVillage: return "#3366cc";  // Blue
        case SettlementType::Hamlet:         return "#669933";  // Green
        default:                             return "#666666";
    }
}

// Get marker radius for settlement type (center marker)
static float getSettlementMarkerRadius(SettlementType type) {
    switch (type) {
        case SettlementType::Town:           return 8.0f;
        case SettlementType::Village:        return 5.0f;
        case SettlementType::FishingVillage: return 5.0f;
        case SettlementType::Hamlet:         return 3.0f;
        default:                             return 4.0f;
    }
}

// Get number of perimeter points based on settlement type
static int getPerimeterPointCount(SettlementType type) {
    switch (type) {
        case SettlementType::Town:           return 16;
        case SettlementType::Village:        return 12;
        case SettlementType::FishingVillage: return 10;
        case SettlementType::Hamlet:         return 8;
        default:                             return 10;
    }
}

// Get radius variation factor (how irregular the perimeter is)
static float getRadiusVariation(SettlementType type) {
    switch (type) {
        case SettlementType::Town:           return 0.25f;  // More regular
        case SettlementType::Village:        return 0.30f;
        case SettlementType::FishingVillage: return 0.35f;
        case SettlementType::Hamlet:         return 0.40f;  // More irregular
        default:                             return 0.30f;
    }
}

// Generate perimeter points for a settlement with organic variation
static std::vector<glm::vec2> generatePerimeterPoints(
    const Settlement& settlement,
    float scale
) {
    int numPoints = getPerimeterPointCount(settlement.type);
    float variation = getRadiusVariation(settlement.type);
    float baseRadius = settlement.radius * scale;

    std::vector<glm::vec2> points;
    points.reserve(numPoints);

    // Use settlement ID as seed for deterministic noise
    uint32_t seed = settlement.id * 31337u;

    for (int i = 0; i < numPoints; ++i) {
        float angle = static_cast<float>(i) / numPoints * 2.0f * 3.14159265f;

        // Generate noisy radius
        float noise1 = hashNoise(seed, i * 2) - 0.5f;
        float noise2 = hashNoise(seed, i * 2 + 1) - 0.5f;
        float radiusNoise = 1.0f + (noise1 + noise2 * 0.5f) * variation;
        float r = baseRadius * radiusNoise;

        float x = settlement.position.x * scale + std::cos(angle) * r;
        float y = settlement.position.y * scale + std::sin(angle) * r;

        points.push_back(glm::vec2(x, y));
    }

    return points;
}

// Convert Catmull-Rom spline segment to Bezier control points
static void catmullRomToBezier(
    const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
    float tension,
    glm::vec2& cp1, glm::vec2& cp2
) {
    float t = (1.0f - tension) / 6.0f;

    cp1.x = p1.x + t * (p2.x - p0.x);
    cp1.y = p1.y + t * (p2.y - p0.y);

    cp2.x = p2.x - t * (p3.x - p1.x);
    cp2.y = p2.y - t * (p3.y - p1.y);
}

// Generate closed SVG path string from points using Catmull-Rom splines
static std::string generateClosedSVGPath(const std::vector<glm::vec2>& points, float tension = 0.5f) {
    if (points.size() < 3) return "";

    std::ostringstream path;
    path << std::fixed << std::setprecision(2);

    // Move to first point
    path << "M " << points[0].x << " " << points[0].y;

    // For closed path, wrap points for Catmull-Rom
    size_t n = points.size();
    std::vector<glm::vec2> extended;
    extended.push_back(points[n - 1]);  // Point before first
    for (const auto& p : points) {
        extended.push_back(p);
    }
    extended.push_back(points[0]);      // Wrap to first
    extended.push_back(points[1]);      // Point after first

    // Generate cubic Bezier curves for each segment
    for (size_t i = 0; i < n; ++i) {
        glm::vec2 cp1, cp2;
        catmullRomToBezier(
            extended[i], extended[i + 1], extended[i + 2], extended[i + 3],
            tension, cp1, cp2
        );

        size_t nextIdx = (i + 1) % n;
        path << " C " << cp1.x << " " << cp1.y
             << " " << cp2.x << " " << cp2.y
             << " " << points[nextIdx].x << " " << points[nextIdx].y;
    }

    path << " Z";  // Close path
    return path.str();
}

void writeSettlementsSVG(
    const std::string& filename,
    const std::vector<Settlement>& settlements,
    float terrainSize,
    int outputWidth,
    int outputHeight
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s", filename.c_str());
        return;
    }

    float scale = static_cast<float>(outputWidth) / terrainSize;

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << outputWidth << "\" height=\"" << outputHeight << "\" "
         << "viewBox=\"0 0 " << outputWidth << " " << outputHeight << "\">\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"#f5f5dc\"/>\n";

    // Metadata
    file << "  <!-- Settlement data visualization -->\n";
    file << "  <!-- Settlements: " << settlements.size() << " -->\n";
    file << "  <!-- Terrain size: " << terrainSize << " m -->\n";

    // Count settlement types for metadata
    int towns = 0, villages = 0, hamlets = 0, fishing = 0;
    for (const auto& s : settlements) {
        switch (s.type) {
            case SettlementType::Town: towns++; break;
            case SettlementType::Village: villages++; break;
            case SettlementType::Hamlet: hamlets++; break;
            case SettlementType::FishingVillage: fishing++; break;
        }
    }
    file << "  <!-- Towns: " << towns << ", Villages: " << villages
         << ", Hamlets: " << hamlets << ", Fishing Villages: " << fishing << " -->\n\n";

    // Draw settlement perimeter shapes
    file << "  <g id=\"settlement-perimeters\" fill-opacity=\"0.25\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
    for (const auto& settlement : settlements) {
        auto perimeterPoints = generatePerimeterPoints(settlement, scale);
        std::string pathD = generateClosedSVGPath(perimeterPoints, 0.5f);
        const char* color = getSettlementColor(settlement.type);

        file << "    <path d=\"" << pathD << "\" "
             << "fill=\"" << color << "\" "
             << "stroke=\"" << color << "\" "
             << "stroke-opacity=\"0.6\"/>\n";
    }
    file << "  </g>\n\n";

    // Draw settlement center markers
    file << "  <g id=\"settlement-markers\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float r = getSettlementMarkerRadius(settlement.type);
        const char* color = getSettlementColor(settlement.type);

        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"" << r << "\" fill=\"" << color
             << "\" stroke=\"#ffffff\" stroke-width=\"1.5\"/>\n";
    }
    file << "  </g>\n\n";

    // Draw settlement labels
    file << "  <g id=\"settlement-labels\" font-family=\"sans-serif\" font-size=\"10\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float labelOffset = getSettlementMarkerRadius(settlement.type) + 4.0f;

        // Label with settlement ID
        file << "    <text x=\"" << (cx + labelOffset) << "\" y=\"" << (cy + 3)
             << "\" fill=\"#333333\">#" << settlement.id << "</text>\n";
    }
    file << "  </g>\n\n";

    // Draw feature indicators (small icons for special features)
    file << "  <g id=\"settlement-features\" font-size=\"8\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float markerRadius = getSettlementMarkerRadius(settlement.type);

        // Draw feature icons below the marker
        float featureY = cy + markerRadius + 12.0f;
        for (size_t i = 0; i < settlement.features.size() && i < 3; ++i) {
            const std::string& feature = settlement.features[i];
            const char* icon = "?";
            const char* iconColor = "#666666";

            if (feature == "market") {
                icon = "M";
                iconColor = "#8b4513";
            } else if (feature == "harbour") {
                icon = "H";
                iconColor = "#1e90ff";
            } else if (feature == "river_access") {
                icon = "R";
                iconColor = "#4a90c0";
            } else if (feature == "coastal") {
                icon = "C";
                iconColor = "#20b2aa";
            } else if (feature == "agricultural") {
                icon = "A";
                iconColor = "#8b7355";
            } else if (feature == "downland") {
                icon = "D";
                iconColor = "#90b060";
            }

            float iconX = cx - 8.0f + i * 10.0f;
            file << "    <text x=\"" << iconX << "\" y=\"" << featureY
                 << "\" fill=\"" << iconColor << "\" font-weight=\"bold\">" << icon << "</text>\n";
        }
    }
    file << "  </g>\n\n";

    // Legend
    file << "  <g id=\"legend\" transform=\"translate(10, " << (outputHeight - 160) << ")\">\n";
    file << "    <rect x=\"0\" y=\"0\" width=\"130\" height=\"150\" fill=\"white\" fill-opacity=\"0.9\" rx=\"5\" stroke=\"#cccccc\"/>\n";
    file << "    <text x=\"10\" y=\"18\" font-size=\"12\" font-weight=\"bold\" font-family=\"sans-serif\">Settlement Types</text>\n";

    float ly = 35;
    file << "    <circle cx=\"18\" cy=\"" << ly << "\" r=\"6\" fill=\"#cc3333\" stroke=\"white\" stroke-width=\"1\"/>\n";
    file << "    <text x=\"32\" y=\"" << (ly + 4) << "\" font-size=\"10\" font-family=\"sans-serif\">Town (" << towns << ")</text>\n";
    ly += 18;
    file << "    <circle cx=\"18\" cy=\"" << ly << "\" r=\"4\" fill=\"#cc6633\" stroke=\"white\" stroke-width=\"1\"/>\n";
    file << "    <text x=\"32\" y=\"" << (ly + 4) << "\" font-size=\"10\" font-family=\"sans-serif\">Village (" << villages << ")</text>\n";
    ly += 18;
    file << "    <circle cx=\"18\" cy=\"" << ly << "\" r=\"4\" fill=\"#3366cc\" stroke=\"white\" stroke-width=\"1\"/>\n";
    file << "    <text x=\"32\" y=\"" << (ly + 4) << "\" font-size=\"10\" font-family=\"sans-serif\">Fishing Village (" << fishing << ")</text>\n";
    ly += 18;
    file << "    <circle cx=\"18\" cy=\"" << ly << "\" r=\"3\" fill=\"#669933\" stroke=\"white\" stroke-width=\"1\"/>\n";
    file << "    <text x=\"32\" y=\"" << (ly + 4) << "\" font-size=\"10\" font-family=\"sans-serif\">Hamlet (" << hamlets << ")</text>\n";

    // Feature legend
    ly += 22;
    file << "    <text x=\"10\" y=\"" << ly << "\" font-size=\"10\" font-weight=\"bold\" font-family=\"sans-serif\">Features</text>\n";
    ly += 14;
    file << "    <text x=\"12\" y=\"" << ly << "\" font-size=\"8\" font-family=\"sans-serif\">"
         << "<tspan font-weight=\"bold\" fill=\"#8b4513\">M</tspan>=Market "
         << "<tspan font-weight=\"bold\" fill=\"#1e90ff\">H</tspan>=Harbour "
         << "<tspan font-weight=\"bold\" fill=\"#4a90c0\">R</tspan>=River</text>\n";
    ly += 12;
    file << "    <text x=\"12\" y=\"" << ly << "\" font-size=\"8\" font-family=\"sans-serif\">"
         << "<tspan font-weight=\"bold\" fill=\"#20b2aa\">C</tspan>=Coastal "
         << "<tspan font-weight=\"bold\" fill=\"#8b7355\">A</tspan>=Agri "
         << "<tspan font-weight=\"bold\" fill=\"#90b060\">D</tspan>=Downland</text>\n";

    file << "  </g>\n";

    file << "</svg>\n";

    SDL_Log("Wrote settlements SVG: %s (%zu settlements)", filename.c_str(), settlements.size());
}
