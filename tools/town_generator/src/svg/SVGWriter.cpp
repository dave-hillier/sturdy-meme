#include "town_generator/svg/SVGWriter.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Canal.h"
#include "town_generator/wards/Ward.h"
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
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    svg << "width=\"" << width * 4 << "\" height=\"" << height * 4 << "\" ";
    svg << "viewBox=\"" << minX << " " << minY << " " << width << " " << height << "\">\n";

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
    svg << "  <g id=\"water\">\n";
    if (model.waterEdge.length() > 0) {
        svg << "    <path d=\"" << polygonToPath(model.waterEdge) << "\" ";
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

    // Roads (outside walls) - solid dark lines
    svg << "  <g id=\"roads\">\n";
    for (const auto& road : model.roads) {
        svg << "    <path d=\"" << polylineToPath(road) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.roadStroke << "\" ";
        svg << "stroke-width=\"" << style.roadStrokeWidth << "\" ";
        svg << "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
    }
    svg << "  </g>\n";

    // Arteries (main streets) - solid dark lines
    svg << "  <g id=\"arteries\">\n";
    for (const auto& artery : model.arteries) {
        svg << "    <path d=\"" << polylineToPath(artery) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.arteryStroke << "\" ";
        svg << "stroke-width=\"" << style.arteryStrokeWidth << "\" ";
        svg << "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
    }
    svg << "  </g>\n";

    // Streets (secondary)
    svg << "  <g id=\"streets\">\n";
    for (const auto& street : model.streets) {
        svg << "    <path d=\"" << polylineToPath(street) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.streetStroke << "\" ";
        svg << "stroke-width=\"" << style.streetStrokeWidth << "\" ";
        svg << "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
    }
    svg << "  </g>\n";

    // Alleys (from ward geometry)
    svg << "  <g id=\"alleys\">\n";
    for (const auto& ward : model.wards_) {
        for (const auto& alley : ward->alleys) {
            svg << "    <path d=\"" << polylineToPath(alley) << "\" ";
            svg << "fill=\"none\" stroke=\"" << style.alleyStroke << "\" ";
            svg << "stroke-width=\"" << style.alleyStrokeWidth << "\" ";
            svg << "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        }
    }
    svg << "  </g>\n";

    // Helper to get a tinted roof color based on ward type
    // Uses slight hue/brightness shifts like mfcg.js tinting
    auto getWardTint = [&](const std::string& name) -> std::string {
        // Base color is #A5A095 (RGB: 165, 160, 149)
        // Apply slight tints to differentiate ward types
        if (name == "Craftsmen") return "#A89A8A";    // warmer/browner
        if (name == "Merchant") return "#B0A890";     // slightly golden
        if (name == "Patriciate") return "#9A9590";   // cooler/grayer
        if (name == "Slum") return "#9E9080";         // darker/muddier
        if (name == "Common") return "#A8A095";       // neutral
        if (name == "Gate") return "#A09590";         // slightly darker
        if (name == "Market") return "#B0A898";       // warmer
        if (name == "Cathedral") return "#A5A0A0";    // cooler, slight purple
        if (name == "Castle") return "#909090";       // gray stone
        if (name == "Administration") return "#A8A090"; // slight gold
        if (name == "Military") return "#959595";     // gray
        if (name == "Farm") return "#A5A095";         // base (buildings on green)
        if (name == "Park") return "#A5A095";         // base
        return style.buildingFill;                    // default
    };

    // Buildings (with ward-type tinting like mfcg.js)
    // Skip special buildings here - they're rendered separately below
    svg << "  <g id=\"buildings\">\n";
    for (const auto& ward : model.wards_) {
        // Skip special wards (Cathedral, Castle) - rendered as solid dark
        if (ward->isSpecialWard()) continue;

        std::string wardColor = getWardTint(ward->getName());
        for (const auto& building : ward->geometry) {
            // Skip the church building - rendered separately as special
            if (ward->hasChurch() && &building == &ward->getChurch()) continue;

            svg << "    <path d=\"" << polygonToPath(building) << "\" ";
            svg << "fill=\"" << wardColor << "\" ";
            svg << "stroke=\"" << style.buildingStroke << "\" ";
            svg << "stroke-width=\"" << style.buildingStrokeWidth << "\"/>\n";
        }
    }
    svg << "  </g>\n";

    // Special buildings (churches, cathedrals, castles) - solid dark like mfcg.js
    // Reference: mfcg.js hd.drawSolids() fills with K.colorWall
    svg << "  <g id=\"special-buildings\">\n";
    for (const auto& ward : model.wards_) {
        // Churches from regular wards (CommonWard, CraftsmenWard, etc.)
        if (ward->hasChurch()) {
            svg << "    <path d=\"" << polygonToPath(ward->getChurch()) << "\" ";
            svg << "fill=\"" << style.wallStroke << "\" ";
            svg << "stroke=\"" << style.buildingStroke << "\" ";
            svg << "stroke-width=\"" << style.buildingStrokeWidth << "\"/>\n";
        }
        // Cathedral and Castle wards - all geometry is special
        if (ward->isSpecialWard()) {
            for (const auto& building : ward->geometry) {
                svg << "    <path d=\"" << polygonToPath(building) << "\" ";
                svg << "fill=\"" << style.wallStroke << "\" ";
                svg << "stroke=\"" << style.buildingStroke << "\" ";
                svg << "stroke-width=\"" << style.buildingStrokeWidth << "\"/>\n";
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
            svg << "    <circle cx=\"" << tower.x << "\" cy=\"" << tower.y << "\" ";
            svg << "r=\"" << towerR << "\" ";
            svg << "fill=\"" << style.towerFill << "\"/>\n";
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
            svg << "    <rect x=\"" << (t1x - towerSize) << "\" y=\"" << (t1y - towerSize) << "\" ";
            svg << "width=\"" << (towerSize * 2) << "\" height=\"" << (towerSize * 2) << "\" ";
            svg << "fill=\"" << style.towerFill << "\"/>\n";

            // Direction from gate to next vertex (outgoing wall segment)
            double dx2 = pNext.x - pGate.x;
            double dy2 = pNext.y - pGate.y;
            double len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
            if (len2 > 0) { dx2 /= len2; dy2 /= len2; }

            // Tower at the start of the outgoing wall segment (offset forward from gate)
            double t2x = pGate.x + dx2 * halfGap;
            double t2y = pGate.y + dy2 * halfGap;
            svg << "    <rect x=\"" << (t2x - towerSize) << "\" y=\"" << (t2y - towerSize) << "\" ";
            svg << "width=\"" << (towerSize * 2) << "\" height=\"" << (towerSize * 2) << "\" ";
            svg << "fill=\"" << style.towerFill << "\"/>\n";
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
