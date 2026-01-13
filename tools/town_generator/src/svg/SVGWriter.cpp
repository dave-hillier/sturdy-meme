#include "town_generator/svg/SVGWriter.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Canal.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/wards/Harbour.h"
#include <cmath>
#include <map>

namespace town_generator {
namespace svg {

std::string SVGWriter::polygonToPath(const geom::Polygon& poly) {
    if (poly.length() == 0) return "";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "M " << poly[0].x << " " << poly[0].y;
    for (size_t i = 1; i < poly.length(); ++i) {
        ss << " L " << poly[i].x << " " << poly[i].y;
    }
    ss << " Z";

    return ss.str();
}

std::string SVGWriter::polylineToPath(const std::vector<geom::Point>& points) {
    if (points.empty()) return "";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "M " << points[0].x << " " << points[0].y;
    for (size_t i = 1; i < points.size(); ++i) {
        ss << " L " << points[i].x << " " << points[i].y;
    }

    return ss.str();
}

std::string SVGWriter::polylineToPath(const building::City::Street& street) {
    if (street.empty()) return "";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "M " << street[0]->x << " " << street[0]->y;
    for (size_t i = 1; i < street.size(); ++i) {
        ss << " L " << street[i]->x << " " << street[i]->y;
    }

    return ss.str();
}

// Helper to check if a point is a gate
static bool isGate(const geom::Point& pt, const std::vector<geom::PointPtr>& gates) {
    for (const auto& gate : gates) {
        if (std::abs(gate->x - pt.x) < 0.01 && std::abs(gate->y - pt.y) < 0.01) {
            return true;
        }
    }
    return false;
}

std::string SVGWriter::generate(const building::City& model, const Style& style) {
    std::ostringstream svg;
    svg << std::fixed << std::setprecision(2);

    // Calculate bounds
    auto bounds = model.borderPatch.shape.getBounds();
    double margin = 5.0;
    double width = bounds.width() + margin * 2;
    double height = bounds.height() + margin * 2;
    double minX = bounds.left - margin;
    double minY = bounds.top - margin;

    // SVG header
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    svg << "width=\"" << width * 4 << "\" height=\"" << height * 4 << "\" ";
    svg << "viewBox=\"" << minX << " " << minY << " " << width << " " << height << "\">\n";

    // CSS styles for common attributes - reduces file size significantly
    svg << "  <style>\n";
    svg << "    .building { stroke: " << style.buildingStroke << "; stroke-width: " << style.buildingStrokeWidth << "; }\n";
    svg << "    .special { fill: " << style.wallStroke << "; stroke: " << style.buildingStroke << "; stroke-width: " << style.buildingStrokeWidth << "; }\n";
    svg << "    .road { fill: none; stroke-linecap: round; stroke-linejoin: round; }\n";
    svg << "    .tower { fill: " << style.towerFill << "; }\n";
    svg << "  </style>\n";

    // Background
    svg << "  <rect x=\"" << minX << "\" y=\"" << minY << "\" ";
    svg << "width=\"" << width << "\" height=\"" << height << "\" ";
    svg << "fill=\"" << style.backgroundColor << "\"/>\n";

    // Farm fields (green background for Farm wards) - rendered first, below water
    svg << "  <g id=\"farms\">\n";
    for (const auto& ward : model.wards_) {
        if (ward->getName() == "Farm" && ward->patch) {
            svg << "    <path d=\"" << polygonToPath(ward->patch->shape) << "\" ";
            svg << "fill=\"" << style.greenFill << "\" stroke=\"none\"/>\n";
        }
    }
    svg << "  </g>\n";

    // Water bodies (rendered on top of farms)
    // Use getOcean() for smart smoothing that preserves landing area alignment
    svg << "  <g id=\"water\">\n";
    geom::Polygon ocean = model.getOcean();
    if (ocean.length() > 0) {
        svg << "    <path d=\"" << polygonToPath(ocean) << "\" ";
        svg << "fill=\"" << style.waterFill << "\" stroke=\"none\"/>\n";
    }
    svg << "  </g>\n";

    // Canals/rivers
    if (!model.canals.empty()) {
        svg << "  <g id=\"canals\">\n";
        for (const auto& canal : model.canals) {
            geom::Polygon waterPoly = canal->getWaterPolygon();
            if (waterPoly.length() > 0) {
                svg << "    <path d=\"" << polygonToPath(waterPoly) << "\" ";
                svg << "fill=\"" << style.waterFill << "\" stroke=\"" << style.waterStroke << "\" ";
                svg << "stroke-width=\"" << style.waterStrokeWidth << "\"/>\n";
            }
            // Draw bridges
            for (const auto& [pos, dir] : canal->bridges) {
                double bridgeLen = canal->width * 1.5;
                geom::Point offset = dir.scale(bridgeLen / 2);
                svg << "    <line x1=\"" << (pos.x - offset.x) << "\" y1=\"" << (pos.y - offset.y) << "\" ";
                svg << "x2=\"" << (pos.x + offset.x) << "\" y2=\"" << (pos.y + offset.y) << "\" ";
                svg << "stroke=\"" << style.wallStroke << "\" stroke-width=\"2.0\"/>\n";
            }
        }
        svg << "  </g>\n";
    }

    // Harbour piers - rendered as filled polygons on water
    // Reference: mfcg.js drawPier renders piers with stroke width 1.2
    svg << "  <g id=\"piers\" fill=\"" << style.backgroundColor << "\" stroke=\"" << style.buildingStroke << "\" stroke-width=\"0.3\">\n";
    for (const auto& ward : model.wards_) {
        if (ward->getName() == "Harbour") {
            auto* harbour = dynamic_cast<wards::Harbour*>(ward.get());
            if (harbour) {
                for (const auto& pier : harbour->getPiers()) {
                    svg << "    <path d=\"" << polygonToPath(pier) << "\"/>\n";
                }
            }
        }
    }
    svg << "  </g>\n";

    // Roads (outside walls) - use CSS class for common attributes
    svg << "  <g id=\"roads\" class=\"road\" stroke=\"" << style.roadStroke << "\" stroke-width=\"" << style.roadStrokeWidth << "\">\n";
    for (const auto& road : model.roads) {
        svg << "    <path d=\"" << polylineToPath(road) << "\"/>\n";
    }
    svg << "  </g>\n";

    // Arteries (main streets)
    svg << "  <g id=\"arteries\" class=\"road\" stroke=\"" << style.arteryStroke << "\" stroke-width=\"" << style.arteryStrokeWidth << "\">\n";
    for (const auto& artery : model.arteries) {
        svg << "    <path d=\"" << polylineToPath(artery) << "\"/>\n";
    }
    svg << "  </g>\n";

    // Streets (secondary)
    svg << "  <g id=\"streets\" class=\"road\" stroke=\"" << style.streetStroke << "\" stroke-width=\"" << style.streetStrokeWidth << "\">\n";
    for (const auto& street : model.streets) {
        svg << "    <path d=\"" << polylineToPath(street) << "\"/>\n";
    }
    svg << "  </g>\n";

    // Alleys (from individual wards)
    svg << "  <g id=\"alleys\" class=\"road\" stroke=\"" << style.alleyStroke << "\" stroke-width=\"" << style.alleyStrokeWidth << "\">\n";
    for (const auto& ward : model.wards_) {
        std::string wardName = ward->getName();
        if (wardName == "Alleys") continue;  // Already rendered from WardGroups
        for (const auto& alley : ward->alleys) {
            svg << "    <path d=\"" << polylineToPath(alley) << "\"/>\n";
        }
    }
    svg << "  </g>\n";

    // Helper to get a tinted roof color based on ward type (MFCG ward types only)
    // Uses slight hue/brightness shifts like mfcg.js tinting
    auto getWardTint = [&](const std::string& name) -> std::string {
        // Base color is #A5A095 (RGB: 165, 160, 149)
        // Apply slight tints to differentiate ward types
        if (name == "Alleys") return "#A5A095";       // base tan
        if (name == "Market") return "#B0A898";       // warmer
        if (name == "Cathedral") return "#A5A0A0";    // cooler, slight purple
        if (name == "Castle") return "#909090";       // gray stone
        if (name == "Farm") return "#A5A095";         // base (buildings on green)
        if (name == "Park") return "#A5A095";         // base
        if (name == "Harbour") return "#A5A095";      // base
        if (name == "Wilderness") return "#A5A095";   // base
        return style.buildingFill;                    // default
    };

    // Buildings (with ward-type tinting like mfcg.js)
    // Skip special buildings here - they're rendered separately below
    svg << "  <g id=\"buildings\">\n";

    // Helper to generate distinct colors for ward groups
    // Uses HSL with varying hue to create visually distinct colors
    auto getGroupColor = [](size_t groupIndex) -> std::string {
        // Base saturation and lightness for building colors
        double s = 0.25;  // Low saturation for muted colors
        double l = 0.65;  // Medium-light for visibility

        // Spread hues across the spectrum, using golden ratio for good distribution
        double hue = std::fmod(groupIndex * 137.508, 360.0);  // Golden angle in degrees

        // Convert HSL to RGB
        double c = (1.0 - std::abs(2.0 * l - 1.0)) * s;
        double x = c * (1.0 - std::abs(std::fmod(hue / 60.0, 2.0) - 1.0));
        double m = l - c / 2.0;

        double r, g, b;
        if (hue < 60) { r = c; g = x; b = 0; }
        else if (hue < 120) { r = x; g = c; b = 0; }
        else if (hue < 180) { r = 0; g = c; b = x; }
        else if (hue < 240) { r = 0; g = x; b = c; }
        else if (hue < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }

        int ri = static_cast<int>((r + m) * 255);
        int gi = static_cast<int>((g + m) * 255);
        int bi = static_cast<int>((b + m) * 255);

        std::ostringstream hex;
        hex << "#" << std::hex << std::setfill('0')
            << std::setw(2) << ri
            << std::setw(2) << gi
            << std::setw(2) << bi;
        return hex.str();
    };

    // Render buildings from WardGroups (Alleys wards)
    // Each ward group gets a distinct color to help visualize the subdivision
    size_t groupIdx = 0;
    for (const auto& group : model.wardGroups_) {
        std::string groupColor = getGroupColor(groupIdx);
        // Use group element with fill attribute, CSS class for stroke
        svg << "    <g class=\"building\" fill=\"" << groupColor << "\">\n";
        for (const auto& block : group->blocks) {
            for (const auto& building : block->buildings) {
                svg << "      <path d=\"" << polygonToPath(building) << "\"/>\n";
            }
        }
        svg << "    </g>\n";
        ++groupIdx;
    }

    // Render buildings from non-grouped wards (Farm, Harbour, Market, Park, etc.)
    for (const auto& ward : model.wards_) {
        // Skip special wards (Cathedral, Castle) - rendered as solid dark
        if (ward->isSpecialWard()) continue;

        // Skip Alleys wards - already rendered from WardGroups above
        std::string wardName = ward->getName();
        if (wardName == "Alleys") continue;

        if (ward->geometry.empty()) continue;

        std::string wardColor = getWardTint(wardName);
        svg << "    <g class=\"building\" fill=\"" << wardColor << "\">\n";
        for (const auto& building : ward->geometry) {
            // Skip the church building - rendered separately as special
            if (ward->hasChurch() && &building == &ward->getChurch()) continue;
            svg << "      <path d=\"" << polygonToPath(building) << "\"/>\n";
        }
        svg << "    </g>\n";
    }
    svg << "  </g>\n";

    // Special buildings (churches, cathedrals, castles) - solid dark like mfcg.js
    // Reference: mfcg.js hd.drawSolids() fills with K.colorWall
    svg << "  <g id=\"special-buildings\" class=\"special\">\n";
    for (const auto& ward : model.wards_) {
        // Churches from Alleys wards
        if (ward->hasChurch()) {
            svg << "    <path d=\"" << polygonToPath(ward->getChurch()) << "\"/>\n";
        }
        // Cathedral and Castle wards - all geometry is special
        if (ward->isSpecialWard()) {
            for (const auto& building : ward->geometry) {
                svg << "    <path d=\"" << polygonToPath(building) << "\"/>\n";
            }
        }
    }
    svg << "  </g>\n";

    // Helper lambda to render walls with gates as small gaps
    auto renderWall = [&](const building::CurtainWall* wall, bool isCitadel) {
        if (!wall) return;

        double towerR = isCitadel ? style.citadelTowerRadius : style.towerRadius;
        double wallWidth = isCitadel ? style.wallStrokeWidth * 1.2 : style.wallStrokeWidth;
        double halfGap = style.gateWidth / 2;

        // Build map of gate indices to gate info
        std::map<size_t, geom::PointPtr> gateAtIndex;
        size_t n = wall->shape.length();
        for (const auto& gatePtr : wall->gates) {
            for (size_t i = 0; i < n; ++i) {
                if (std::abs(wall->shape[i].x - gatePtr->x) < 0.01 &&
                    std::abs(wall->shape[i].y - gatePtr->y) < 0.01) {
                    gateAtIndex[i] = gatePtr;
                    break;
                }
            }
        }

        // Render wall segments, creating small gaps at gate positions
        for (size_t i = 0; i < n; ++i) {
            // Skip disabled segments (e.g., segments bordering water or citadel)
            if (i < wall->segments.size() && !wall->segments[i]) {
                continue;
            }

            size_t next = (i + 1) % n;
            const auto& p0 = wall->shape[i];
            const auto& p1 = wall->shape[next];

            // Calculate segment direction
            double dx = p1.x - p0.x;
            double dy = p1.y - p0.y;
            double segLen = std::sqrt(dx * dx + dy * dy);
            if (segLen < 0.01) continue;
            dx /= segLen;
            dy /= segLen;

            // Start and end points for this segment
            double startX = p0.x, startY = p0.y;
            double endX = p1.x, endY = p1.y;

            // If segment starts at a gate, offset the start point to create gap
            if (gateAtIndex.count(i)) {
                startX = p0.x + dx * halfGap;
                startY = p0.y + dy * halfGap;
            }

            // If segment ends at a gate, offset the end point to create gap
            if (gateAtIndex.count(next)) {
                endX = p1.x - dx * halfGap;
                endY = p1.y - dy * halfGap;
            }

            // Only draw if there's still a segment to draw
            double remainingLen = std::sqrt((endX - startX) * (endX - startX) +
                                            (endY - startY) * (endY - startY));
            if (remainingLen > 0.1) {
                svg << "    <line x1=\"" << startX << "\" y1=\"" << startY << "\" ";
                svg << "x2=\"" << endX << "\" y2=\"" << endY << "\" ";
                svg << "stroke=\"" << style.wallStroke << "\" ";
                svg << "stroke-width=\"" << wallWidth << "\" ";
                svg << "stroke-linecap=\"round\"/>\n";
            }
        }

        // Towers (filled circles)
        for (const auto& tower : wall->towers) {
            svg << "    <circle class=\"tower\" cx=\"" << tower.x << "\" cy=\"" << tower.y << "\" ";
            svg << "r=\"" << towerR << "\"/>\n";
        }

        // Gates: two flanking square towers at the ends of the gap (along the wall)
        for (const auto& gatePtr : wall->gates) {
            // Find the gate's index in the wall shape
            size_t gateIdx = 0;
            for (size_t i = 0; i < n; ++i) {
                if (std::abs(wall->shape[i].x - gatePtr->x) < 0.01 &&
                    std::abs(wall->shape[i].y - gatePtr->y) < 0.01) {
                    gateIdx = i;
                    break;
                }
            }

            // Get adjacent vertices to calculate wall directions
            size_t prev = (gateIdx + n - 1) % n;
            size_t nextIdx = (gateIdx + 1) % n;
            const auto& pPrev = wall->shape[prev];
            const auto& pNext = wall->shape[nextIdx];
            const auto& pGate = *gatePtr;

            double towerSize = towerR * 0.8;

            // Direction from previous vertex to gate (incoming wall segment)
            double dx1 = pGate.x - pPrev.x;
            double dy1 = pGate.y - pPrev.y;
            double len1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
            if (len1 > 0) { dx1 /= len1; dy1 /= len1; }

            // Tower at the end of the incoming wall segment (offset back from gate)
            double t1x = pGate.x - dx1 * halfGap;
            double t1y = pGate.y - dy1 * halfGap;
            svg << "    <rect class=\"tower\" x=\"" << (t1x - towerSize) << "\" y=\"" << (t1y - towerSize) << "\" ";
            svg << "width=\"" << (towerSize * 2) << "\" height=\"" << (towerSize * 2) << "\"/>\n";

            // Direction from gate to next vertex (outgoing wall segment)
            double dx2 = pNext.x - pGate.x;
            double dy2 = pNext.y - pGate.y;
            double len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
            if (len2 > 0) { dx2 /= len2; dy2 /= len2; }

            // Tower at the start of the outgoing wall segment (offset forward from gate)
            double t2x = pGate.x + dx2 * halfGap;
            double t2y = pGate.y + dy2 * halfGap;
            svg << "    <rect class=\"tower\" x=\"" << (t2x - towerSize) << "\" y=\"" << (t2y - towerSize) << "\" ";
            svg << "width=\"" << (towerSize * 2) << "\" height=\"" << (towerSize * 2) << "\"/>\n";
        }
    };

    // Walls
    if (model.wall) {
        svg << "  <g id=\"walls\">\n";
        renderWall(model.wall, false);
        svg << "  </g>\n";
    }

    // Citadel
    if (model.citadel) {
        svg << "  <g id=\"citadel\">\n";
        renderWall(model.citadel, true);
        svg << "  </g>\n";
    }

    svg << "</svg>\n";

    return svg.str();
}

bool SVGWriter::write(const building::City& model, const std::string& filename, const Style& style) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << generate(model, style);
    file.close();

    return true;
}

} // namespace svg
} // namespace town_generator
