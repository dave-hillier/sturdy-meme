#pragma once

#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/wards/AllWards.hpp"
#include "town_generator2/mapping/Palette.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace town_generator2 {
namespace mapping {

/**
 * SVGRenderer - Renders a town model to SVG
 */
class SVGRenderer {
public:
    static constexpr double NORMAL_STROKE = 0.3;
    static constexpr double THICK_STROKE = 1.8;
    static constexpr double THIN_STROKE = 0.15;

    Palette palette;

    SVGRenderer(const Palette& p = Palette::DEFAULT()) : palette(p) {}

    std::string render(building::Model& model) {
        // Calculate bounds
        double minX = 1e10, minY = 1e10, maxX = -1e10, maxY = -1e10;
        for (auto* patch : model.patches) {
            for (const auto& v : patch->shape) {
                minX = std::min(minX, v->x);
                minY = std::min(minY, v->y);
                maxX = std::max(maxX, v->x);
                maxY = std::max(maxY, v->y);
            }
        }

        double margin = 20;
        double width = maxX - minX + margin * 2;
        double height = maxY - minY + margin * 2;

        std::ostringstream svg;
        svg << std::fixed << std::setprecision(2);

        // SVG header
        svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
        svg << "viewBox=\"" << (minX - margin) << " " << (minY - margin) << " ";
        svg << width << " " << height << "\" ";
        svg << "width=\"" << width << "\" height=\"" << height << "\">\n";

        // Background
        svg << "<rect x=\"" << (minX - margin) << "\" y=\"" << (minY - margin) << "\" ";
        svg << "width=\"" << width << "\" height=\"" << height << "\" ";
        svg << "fill=\"" << palette.paperHex() << "\"/>\n";

        // Render roads
        for (const auto& road : model.roads) {
            renderRoad(svg, road);
        }

        // Render streets (arteries)
        for (const auto& street : model.arteries) {
            renderStreet(svg, street);
        }

        // Render patch geometry (buildings)
        for (auto* patch : model.patches) {
            if (patch->ward) {
                renderWard(svg, *patch);
            }
        }

        // Render walls
        if (model.wall) {
            renderWall(svg, *model.wall, false);
        }

        if (model.citadel && model.citadel->ward) {
            auto* castle = dynamic_cast<wards::Castle*>(model.citadel->ward);
            if (castle && castle->wall) {
                renderWall(svg, *castle->wall, true);
            }
        }

        svg << "</svg>\n";
        return svg.str();
    }

private:
    void renderPolygon(std::ostringstream& svg, const geom::Polygon& poly,
                       const std::string& fill, const std::string& stroke,
                       double strokeWidth) {
        if (poly.length() < 3) return;

        svg << "<polygon points=\"";
        for (size_t i = 0; i < poly.length(); ++i) {
            if (i > 0) svg << " ";
            svg << poly[i].x << "," << poly[i].y;
        }
        svg << "\" fill=\"" << fill << "\" ";
        svg << "stroke=\"" << stroke << "\" stroke-width=\"" << strokeWidth << "\"/>\n";
    }

    void renderPolyline(std::ostringstream& svg, const geom::Polygon& poly,
                        const std::string& stroke, double strokeWidth,
                        const std::string& linecap = "round") {
        if (poly.length() < 2) return;

        svg << "<polyline points=\"";
        for (size_t i = 0; i < poly.length(); ++i) {
            if (i > 0) svg << " ";
            svg << poly[i].x << "," << poly[i].y;
        }
        svg << "\" fill=\"none\" stroke=\"" << stroke << "\" ";
        svg << "stroke-width=\"" << strokeWidth << "\" ";
        svg << "stroke-linecap=\"" << linecap << "\" stroke-linejoin=\"round\"/>\n";
    }

    void renderCircle(std::ostringstream& svg, const geom::Point& p, double r,
                      const std::string& fill) {
        svg << "<circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"" << r << "\" ";
        svg << "fill=\"" << fill << "\"/>\n";
    }

    void renderRoad(std::ostringstream& svg, const geom::Polygon& road) {
        // Road outline
        renderPolyline(svg, road, palette.mediumHex(),
                      wards::Ward::MAIN_STREET + NORMAL_STROKE, "butt");
        // Road surface
        renderPolyline(svg, road, palette.paperHex(),
                      wards::Ward::MAIN_STREET - NORMAL_STROKE);
    }

    void renderStreet(std::ostringstream& svg, const geom::Polygon& street) {
        // Street outline
        renderPolyline(svg, street, palette.mediumHex(),
                      wards::Ward::REGULAR_STREET + NORMAL_STROKE, "butt");
        // Street surface
        renderPolyline(svg, street, palette.paperHex(),
                      wards::Ward::REGULAR_STREET - NORMAL_STROKE);
    }

    void renderWard(std::ostringstream& svg, building::Patch& patch) {
        wards::Ward* ward = patch.ward;
        if (!ward || ward->geometry.empty()) return;

        // Different rendering based on ward type
        if (dynamic_cast<wards::Castle*>(ward)) {
            renderBuildings(svg, ward->geometry, palette.lightHex(), palette.darkHex(),
                          NORMAL_STROKE * 2);
        }
        else if (dynamic_cast<wards::Cathedral*>(ward)) {
            renderBuildings(svg, ward->geometry, palette.lightHex(), palette.darkHex(),
                          NORMAL_STROKE);
        }
        else if (dynamic_cast<wards::Park*>(ward)) {
            for (const auto& grove : ward->geometry) {
                renderPolygon(svg, grove, palette.mediumHex(), "none", 0);
            }
        }
        else {
            // Standard buildings (Craftsmen, Merchant, Slum, etc.)
            for (const auto& building : ward->geometry) {
                renderPolygon(svg, building, palette.lightHex(), palette.darkHex(),
                            NORMAL_STROKE);
            }
        }
    }

    void renderBuildings(std::ostringstream& svg,
                        const std::vector<geom::Polygon>& blocks,
                        const std::string& fill, const std::string& line,
                        double thickness) {
        // Draw stroke first (larger)
        for (const auto& block : blocks) {
            renderPolygon(svg, block, "none", line, thickness * 2);
        }
        // Then fill
        for (const auto& block : blocks) {
            renderPolygon(svg, block, fill, "none", 0);
        }
    }

    void renderWall(std::ostringstream& svg, building::CurtainWall& wall, bool large) {
        // Wall outline
        renderPolygon(svg, wall.shape, "none", palette.darkHex(), THICK_STROKE);

        // Gates
        for (const auto& gate : wall.gates) {
            renderGate(svg, wall.shape, gate);
        }

        // Towers
        double towerRadius = THICK_STROKE * (large ? 1.5 : 1.0);
        for (const auto& tower : wall.towers) {
            renderCircle(svg, *tower, towerRadius, palette.darkHex());
        }
    }

    void renderGate(std::ostringstream& svg, const geom::Polygon& wall,
                    const geom::PointPtr& gate) {
        int idx = wall.indexOf(gate);
        if (idx == -1) return;

        geom::Point prevP = *wall.previ(idx);
        geom::Point nextP = *wall.nexti(idx);
        geom::Point dir = nextP.subtract(prevP);
        dir.normalize(THICK_STROKE * 1.5);

        geom::Point p1 = gate->subtract(dir);
        geom::Point p2 = gate->add(dir);

        svg << "<line x1=\"" << p1.x << "\" y1=\"" << p1.y << "\" ";
        svg << "x2=\"" << p2.x << "\" y2=\"" << p2.y << "\" ";
        svg << "stroke=\"" << palette.darkHex() << "\" stroke-width=\"" << (THICK_STROKE * 2) << "\" ";
        svg << "stroke-linecap=\"butt\"/>\n";
    }
};

} // namespace mapping
} // namespace town_generator2
