#include "RoadSVG.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace RoadGen {

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

// Generate SVG path string from points using Catmull-Rom splines
static std::string generateSVGPath(const std::vector<glm::vec2>& points, float tension = 0.5f) {
    if (points.size() < 2) return "";

    std::ostringstream path;
    path << std::fixed << std::setprecision(2);

    path << "M " << points[0].x << " " << points[0].y;

    if (points.size() == 2) {
        path << " L " << points[1].x << " " << points[1].y;
        return path.str();
    }

    // Extend by duplicating endpoints for Catmull-Rom
    std::vector<glm::vec2> extended;
    extended.push_back(points[0]);
    for (const auto& p : points) {
        extended.push_back(p);
    }
    extended.push_back(points.back());

    // Generate cubic Bezier curves
    for (size_t i = 0; i < points.size() - 1; ++i) {
        glm::vec2 cp1, cp2;
        catmullRomToBezier(
            extended[i], extended[i + 1], extended[i + 2], extended[i + 3],
            tension, cp1, cp2
        );

        path << " C " << cp1.x << " " << cp1.y
             << " " << cp2.x << " " << cp2.y
             << " " << points[i + 1].x << " " << points[i + 1].y;
    }

    return path.str();
}

// Get color for road type
static const char* getRoadColor(RoadType type) {
    switch (type) {
        case RoadType::MainRoad:  return "#d4a574";  // Tan/brown
        case RoadType::Road:      return "#b8956e";  // Lighter brown
        case RoadType::Lane:      return "#8b7355";  // Medium brown
        case RoadType::Bridleway: return "#6b5344";  // Dark brown
        case RoadType::Footpath:  return "#4a3728";  // Very dark brown
        default:                  return "#888888";
    }
}

// Get stroke width for road type (base width, will be scaled)
static float getRoadStrokeWidth(RoadType type) {
    switch (type) {
        case RoadType::MainRoad:  return 4.0f;
        case RoadType::Road:      return 3.0f;
        case RoadType::Lane:      return 2.0f;
        case RoadType::Bridleway: return 1.5f;
        case RoadType::Footpath:  return 1.0f;
        default:                  return 2.0f;
    }
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

// Get radius for settlement marker
static float getSettlementRadius(SettlementType type, float scale) {
    switch (type) {
        case SettlementType::Town:           return 8.0f * scale;
        case SettlementType::Village:        return 5.0f * scale;
        case SettlementType::FishingVillage: return 5.0f * scale;
        case SettlementType::Hamlet:         return 3.0f * scale;
        default:                             return 4.0f * scale;
    }
}

void writeNetworkSVG(
    const std::string& filename,
    const ColonizationResult& network,
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
    file << "  <!-- Road network topology from space colonization -->\n";
    file << "  <!-- Nodes: " << network.nodes.size() << " -->\n";
    file << "  <!-- Edges: " << network.edges.size() << " -->\n";

    // Draw edges (roads)
    file << "  <g id=\"edges\" stroke=\"#8b4513\" stroke-linecap=\"round\">\n";

    for (const auto& edge : network.edges) {
        const NetworkNode& from = network.nodes[edge.fromNode];
        const NetworkNode& to = network.nodes[edge.toNode];

        float x1 = from.position.x * scale;
        float y1 = from.position.y * scale;
        float x2 = to.position.x * scale;
        float y2 = to.position.y * scale;

        // Stroke width based on depth (thicker for main routes)
        float strokeWidth = std::max(1.0f, 4.0f - edge.depth * 0.5f);

        file << "    <line x1=\"" << x1 << "\" y1=\"" << y1
             << "\" x2=\"" << x2 << "\" y2=\"" << y2
             << "\" stroke-width=\"" << strokeWidth << "\"/>\n";
    }

    file << "  </g>\n";

    // Draw junction nodes
    file << "  <g id=\"junctions\" fill=\"#4a3728\">\n";

    for (const auto& node : network.nodes) {
        if (node.isSettlement) continue;

        float cx = node.position.x * scale;
        float cy = node.position.y * scale;

        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"2\"/>\n";
    }

    file << "  </g>\n";

    // Draw settlements
    file << "  <g id=\"settlements\">\n";

    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float r = getSettlementRadius(settlement.type, 1.0f);
        const char* color = getSettlementColor(settlement.type);

        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"" << r << "\" fill=\"" << color
             << "\" stroke=\"#ffffff\" stroke-width=\"1\"/>\n";
    }

    file << "  </g>\n";

    // Legend
    file << "  <g id=\"legend\" transform=\"translate(10, " << (outputHeight - 100) << ")\">\n";
    file << "    <rect x=\"0\" y=\"0\" width=\"120\" height=\"90\" fill=\"white\" fill-opacity=\"0.8\" rx=\"5\"/>\n";
    file << "    <text x=\"10\" y=\"15\" font-size=\"10\" font-weight=\"bold\">Settlement Types</text>\n";

    float ly = 30;
    file << "    <circle cx=\"15\" cy=\"" << ly << "\" r=\"6\" fill=\"#cc3333\"/>\n";
    file << "    <text x=\"30\" y=\"" << (ly + 4) << "\" font-size=\"9\">Town</text>\n";
    ly += 15;
    file << "    <circle cx=\"15\" cy=\"" << ly << "\" r=\"4\" fill=\"#cc6633\"/>\n";
    file << "    <text x=\"30\" y=\"" << (ly + 4) << "\" font-size=\"9\">Village</text>\n";
    ly += 15;
    file << "    <circle cx=\"15\" cy=\"" << ly << "\" r=\"4\" fill=\"#3366cc\"/>\n";
    file << "    <text x=\"30\" y=\"" << (ly + 4) << "\" font-size=\"9\">Fishing Village</text>\n";
    ly += 15;
    file << "    <circle cx=\"15\" cy=\"" << ly << "\" r=\"3\" fill=\"#669933\"/>\n";
    file << "    <text x=\"30\" y=\"" << (ly + 4) << "\" font-size=\"9\">Hamlet</text>\n";

    file << "  </g>\n";

    file << "</svg>\n";

    SDL_Log("Wrote network topology SVG: %s", filename.c_str());
}

void writeRoadsSVG(
    const std::string& filename,
    const RoadNetwork& roads,
    const std::vector<Settlement>& settlements,
    int outputWidth,
    int outputHeight
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s", filename.c_str());
        return;
    }

    float scale = static_cast<float>(outputWidth) / roads.terrainSize;

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << outputWidth << "\" height=\"" << outputHeight << "\" "
         << "viewBox=\"0 0 " << outputWidth << " " << outputHeight << "\">\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"#f5f5dc\"/>\n";

    // Metadata
    file << "  <!-- Road network with A* paths -->\n";
    file << "  <!-- Roads: " << roads.roads.size() << " -->\n";
    file << "  <!-- Total length: " << (roads.getTotalLength() / 1000.0f) << " km -->\n";

    // Sort roads by type (draw main roads last so they're on top)
    std::vector<size_t> roadOrder;
    roadOrder.reserve(roads.roads.size());
    for (size_t i = 0; i < roads.roads.size(); i++) {
        roadOrder.push_back(i);
    }
    std::sort(roadOrder.begin(), roadOrder.end(), [&](size_t a, size_t b) {
        return static_cast<int>(roads.roads[a].type) < static_cast<int>(roads.roads[b].type);
    });

    // Draw roads
    file << "  <g id=\"roads\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";

    for (size_t idx : roadOrder) {
        const auto& road = roads.roads[idx];
        if (road.controlPoints.size() < 2) continue;

        // Convert control points to scaled coordinates
        std::vector<glm::vec2> scaledPoints;
        for (const auto& cp : road.controlPoints) {
            scaledPoints.push_back(cp.position * scale);
        }

        std::string pathD = generateSVGPath(scaledPoints, 0.5f);
        const char* color = getRoadColor(road.type);
        float strokeWidth = getRoadStrokeWidth(road.type);

        file << "    <path d=\"" << pathD << "\" "
             << "stroke=\"" << color << "\" "
             << "stroke-width=\"" << strokeWidth << "\"/>\n";
    }

    file << "  </g>\n";

    // Draw settlements
    file << "  <g id=\"settlements\">\n";

    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float r = getSettlementRadius(settlement.type, 1.0f);
        const char* color = getSettlementColor(settlement.type);

        // Draw settlement area circle (faded)
        float areaRadius = settlement.radius * scale;
        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"" << areaRadius << "\" fill=\"" << color
             << "\" fill-opacity=\"0.2\" stroke=\"" << color
             << "\" stroke-width=\"1\" stroke-opacity=\"0.5\"/>\n";

        // Draw settlement center marker
        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"" << r << "\" fill=\"" << color
             << "\" stroke=\"#ffffff\" stroke-width=\"1\"/>\n";
    }

    file << "  </g>\n";

    // Legend
    file << "  <g id=\"legend\" transform=\"translate(10, " << (outputHeight - 130) << ")\">\n";
    file << "    <rect x=\"0\" y=\"0\" width=\"100\" height=\"120\" fill=\"white\" fill-opacity=\"0.8\" rx=\"5\"/>\n";
    file << "    <text x=\"10\" y=\"15\" font-size=\"10\" font-weight=\"bold\">Road Types</text>\n";

    float ly = 28;
    file << "    <line x1=\"10\" y1=\"" << ly << "\" x2=\"30\" y2=\"" << ly
         << "\" stroke=\"#d4a574\" stroke-width=\"4\"/>\n";
    file << "    <text x=\"35\" y=\"" << (ly + 4) << "\" font-size=\"9\">Main Road</text>\n";
    ly += 14;
    file << "    <line x1=\"10\" y1=\"" << ly << "\" x2=\"30\" y2=\"" << ly
         << "\" stroke=\"#b8956e\" stroke-width=\"3\"/>\n";
    file << "    <text x=\"35\" y=\"" << (ly + 4) << "\" font-size=\"9\">Road</text>\n";
    ly += 14;
    file << "    <line x1=\"10\" y1=\"" << ly << "\" x2=\"30\" y2=\"" << ly
         << "\" stroke=\"#8b7355\" stroke-width=\"2\"/>\n";
    file << "    <text x=\"35\" y=\"" << (ly + 4) << "\" font-size=\"9\">Lane</text>\n";
    ly += 14;
    file << "    <line x1=\"10\" y1=\"" << ly << "\" x2=\"30\" y2=\"" << ly
         << "\" stroke=\"#6b5344\" stroke-width=\"1.5\"/>\n";
    file << "    <text x=\"35\" y=\"" << (ly + 4) << "\" font-size=\"9\">Bridleway</text>\n";
    ly += 14;
    file << "    <line x1=\"10\" y1=\"" << ly << "\" x2=\"30\" y2=\"" << ly
         << "\" stroke=\"#4a3728\" stroke-width=\"1\"/>\n";
    file << "    <text x=\"35\" y=\"" << (ly + 4) << "\" font-size=\"9\">Footpath</text>\n";

    file << "  </g>\n";

    file << "</svg>\n";

    SDL_Log("Wrote roads SVG: %s", filename.c_str());
}

} // namespace RoadGen
