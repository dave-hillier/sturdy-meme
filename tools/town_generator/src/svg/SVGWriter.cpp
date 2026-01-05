#include "town_generator/svg/SVGWriter.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/wards/Ward.h"
#include <cmath>

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

std::string SVGWriter::polylineToPath(const building::Model::Street& street) {
    if (street.empty()) return "";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "M " << street[0]->x << " " << street[0]->y;
    for (size_t i = 1; i < street.size(); ++i) {
        ss << " L " << street[i]->x << " " << street[i]->y;
    }

    return ss.str();
}

std::string SVGWriter::generate(const building::Model& model, const Style& style) {
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

    // Patch outlines (optional, for debugging)
    svg << "  <g id=\"patches\" opacity=\"0.5\">\n";
    for (const auto* patch : model.patches) {
        svg << "    <path d=\"" << polygonToPath(patch->shape) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.patchStroke << "\" ";
        svg << "stroke-width=\"" << style.patchStrokeWidth << "\"/>\n";
    }
    svg << "  </g>\n";

    // Roads (outside walls)
    svg << "  <g id=\"roads\">\n";
    for (const auto& road : model.roads) {
        svg << "    <path d=\"" << polylineToPath(road) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.roadStroke << "\" ";
        svg << "stroke-width=\"" << style.roadStrokeWidth << "\" ";
        svg << "stroke-linecap=\"round\" stroke-linejoin=\"round\" ";
        svg << "stroke-dasharray=\"2,2\"/>\n";
    }
    svg << "  </g>\n";

    // Arteries (main streets)
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

    // Ward type colors for debugging
    auto getWardColor = [](const std::string& name) -> std::string {
        if (name == "Craftsmen") return "#d4a574";      // tan/brown
        if (name == "Merchant") return "#c9a227";       // gold
        if (name == "Patriciate") return "#8b4513";     // saddle brown
        if (name == "Slum") return "#a0522d";           // sienna
        if (name == "Common") return "#deb887";         // burlywood
        if (name == "Gate") return "#cd853f";           // peru
        if (name == "Market") return "#f5deb3";         // wheat
        if (name == "Cathedral") return "#bc8f8f";      // rosy brown
        if (name == "Castle") return "#696969";         // dim gray
        if (name == "Administration") return "#b8860b"; // dark goldenrod
        if (name == "Military") return "#808080";       // gray
        if (name == "Farm") return "#9acd32";           // yellow green
        if (name == "Park") return "#90ee90";           // light green
        return "#d2b48c";                               // default tan
    };

    // Buildings (colored by ward type)
    svg << "  <g id=\"buildings\">\n";
    for (const auto& ward : model.wards_) {
        std::string wardColor = getWardColor(ward->getName());
        for (const auto& building : ward->geometry) {
            svg << "    <path d=\"" << polygonToPath(building) << "\" ";
            svg << "fill=\"" << wardColor << "\" ";
            svg << "stroke=\"" << style.buildingStroke << "\" ";
            svg << "stroke-width=\"" << style.buildingStrokeWidth << "\"/>\n";
        }
    }
    svg << "  </g>\n";

    // Walls
    if (model.wall) {
        svg << "  <g id=\"walls\">\n";
        svg << "    <path d=\"" << polygonToPath(model.wall->shape) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.wallStroke << "\" ";
        svg << "stroke-width=\"" << style.wallStrokeWidth << "\"/>\n";

        // Towers
        for (const auto& tower : model.wall->towers) {
            svg << "    <circle cx=\"" << tower.x << "\" cy=\"" << tower.y << "\" ";
            svg << "r=\"" << style.towerRadius << "\" ";
            svg << "fill=\"" << style.towerFill << "\" stroke=\"" << style.towerStroke << "\" ";
            svg << "stroke-width=\"1\"/>\n";
        }

        // Gates
        for (const auto& gatePtr : model.wall->gates) {
            svg << "    <circle cx=\"" << gatePtr->x << "\" cy=\"" << gatePtr->y << "\" ";
            svg << "r=\"" << style.towerRadius * 1.2 << "\" ";
            svg << "fill=\"" << style.gateFill << "\" stroke=\"" << style.wallStroke << "\" ";
            svg << "stroke-width=\"1\"/>\n";
        }

        svg << "  </g>\n";
    }

    // Citadel
    if (model.citadel) {
        svg << "  <g id=\"citadel\">\n";
        svg << "    <path d=\"" << polygonToPath(model.citadel->shape) << "\" ";
        svg << "fill=\"none\" stroke=\"" << style.wallStroke << "\" ";
        svg << "stroke-width=\"" << style.wallStrokeWidth * 1.2 << "\"/>\n";

        for (const auto& tower : model.citadel->towers) {
            svg << "    <circle cx=\"" << tower.x << "\" cy=\"" << tower.y << "\" ";
            svg << "r=\"" << style.towerRadius * 1.3 << "\" ";
            svg << "fill=\"" << style.towerFill << "\" stroke=\"" << style.towerStroke << "\" ";
            svg << "stroke-width=\"1.5\"/>\n";
        }

        svg << "  </g>\n";
    }

    svg << "</svg>\n";

    return svg.str();
}

bool SVGWriter::write(const building::Model& model, const std::string& filename, const Style& style) {
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
