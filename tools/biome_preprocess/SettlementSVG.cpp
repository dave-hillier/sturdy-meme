#include "SettlementSVG.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <iomanip>
#include <cmath>

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
static float getSettlementMarkerRadius(SettlementType type, float scale) {
    switch (type) {
        case SettlementType::Town:           return 8.0f * scale;
        case SettlementType::Village:        return 5.0f * scale;
        case SettlementType::FishingVillage: return 5.0f * scale;
        case SettlementType::Hamlet:         return 3.0f * scale;
        default:                             return 4.0f * scale;
    }
}

// Get settlement type name for labels
static const char* getSettlementTypeName(SettlementType type) {
    switch (type) {
        case SettlementType::Town:           return "Town";
        case SettlementType::Village:        return "Village";
        case SettlementType::FishingVillage: return "Fishing Village";
        case SettlementType::Hamlet:         return "Hamlet";
        default:                             return "Unknown";
    }
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

    // Draw settlement areas (faded circles showing radius)
    file << "  <g id=\"settlement-areas\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float areaRadius = settlement.radius * scale;
        const char* color = getSettlementColor(settlement.type);

        file << "    <circle cx=\"" << cx << "\" cy=\"" << cy
             << "\" r=\"" << areaRadius << "\" fill=\"" << color
             << "\" fill-opacity=\"0.15\" stroke=\"" << color
             << "\" stroke-width=\"1\" stroke-opacity=\"0.4\" stroke-dasharray=\"4,2\"/>\n";
    }
    file << "  </g>\n\n";

    // Draw settlement center markers
    file << "  <g id=\"settlement-markers\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float r = getSettlementMarkerRadius(settlement.type, 1.0f);
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
        float labelOffset = getSettlementMarkerRadius(settlement.type, 1.0f) + 4.0f;

        // Label with settlement ID and type
        file << "    <text x=\"" << (cx + labelOffset) << "\" y=\"" << (cy + 3)
             << "\" fill=\"#333333\">#" << settlement.id << "</text>\n";
    }
    file << "  </g>\n\n";

    // Draw feature indicators (small icons for special features)
    file << "  <g id=\"settlement-features\" font-size=\"8\">\n";
    for (const auto& settlement : settlements) {
        float cx = settlement.position.x * scale;
        float cy = settlement.position.y * scale;
        float markerRadius = getSettlementMarkerRadius(settlement.type, 1.0f);

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
