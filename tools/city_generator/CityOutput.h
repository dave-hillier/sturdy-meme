// City output: GeoJSON and SVG export for visualization
// Includes walls, buildings, wards, roads, gates, towers, and trees
//
// GeoJSON layers:
// - boundary: City border polygon
// - wards: Ward boundary polygons with type properties
// - buildings: Building footprint polygons
// - walls: Wall perimeter and segments
// - towers: Tower point features
// - gates: Gate point features
// - streets: Street/road line features
// - trees: Tree point features (procedural placement)

#pragma once

#include "Model.h"
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <random>

namespace city {

using json = nlohmann::json;

// Ward color palette for visualization
inline std::string getWardColor(WardType type) {
    switch (type) {
        case WardType::Castle:        return "#8B4513";  // SaddleBrown
        case WardType::Cathedral:     return "#FFD700";  // Gold
        case WardType::Market:        return "#FF8C00";  // DarkOrange
        case WardType::Patriciate:    return "#4169E1";  // RoyalBlue
        case WardType::Craftsmen:     return "#CD853F";  // Peru
        case WardType::Merchants:     return "#20B2AA";  // LightSeaGreen
        case WardType::Administration: return "#9370DB"; // MediumPurple
        case WardType::Military:      return "#B22222";  // FireBrick
        case WardType::Slum:          return "#696969";  // DimGray
        case WardType::Farm:          return "#228B22";  // ForestGreen
        case WardType::Park:          return "#32CD32";  // LimeGreen
        case WardType::Gate:          return "#D2691E";  // Chocolate
        case WardType::Common:        return "#A0522D";  // Sienna
        default:                      return "#808080";  // Gray
    }
}

// Generate tree positions using Poisson disk sampling
inline std::vector<Vec2> generateTrees(const Model& model, float density, std::mt19937& rng) {
    std::vector<Vec2> trees;

    // Only place trees in parks and farms, and around the city edge
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (const auto& patch : model.patches) {
        if (!patch->ward) continue;

        float treeDensity = 0.0f;
        if (patch->ward->type == WardType::Park) {
            treeDensity = density * 2.0f;
        } else if (patch->ward->type == WardType::Farm) {
            treeDensity = density * 0.3f;
        } else if (!patch->withinWalls) {
            treeDensity = density * 0.5f;
        } else {
            continue;  // No trees in built-up wards
        }

        // Simple random placement based on patch area
        int numTrees = static_cast<int>(patch->area() * treeDensity * 0.01f);
        AABB bounds = patch->shape.bounds();

        for (int i = 0; i < numTrees; i++) {
            Vec2 candidate{
                bounds.min.x + dist(rng) * bounds.size().x,
                bounds.min.y + dist(rng) * bounds.size().y
            };

            if (patch->shape.contains(candidate)) {
                // Check not inside any building
                bool insideBuilding = false;
                for (const auto& building : patch->ward->geometry) {
                    if (building.contains(candidate)) {
                        insideBuilding = true;
                        break;
                    }
                }

                if (!insideBuilding) {
                    trees.push_back(candidate);
                }
            }
        }
    }

    return trees;
}

// Convert polygon to GeoJSON coordinates
inline json polygonToCoords(const Polygon& poly) {
    json coords = json::array();
    for (const auto& v : poly.vertices) {
        coords.push_back({v.x, v.y});
    }
    // Close the ring
    if (!poly.vertices.empty()) {
        coords.push_back({poly.vertices[0].x, poly.vertices[0].y});
    }
    return json::array({coords});
}

// Convert path to GeoJSON coordinates
inline json pathToCoords(const std::vector<Vec2>& path) {
    json coords = json::array();
    for (const auto& v : path) {
        coords.push_back({v.x, v.y});
    }
    return coords;
}

// Export city to GeoJSON
inline void exportGeoJSON(const Model& model, const std::string& path, float treeDensity = 1.0f) {
    json features = json::array();

    // 1. City boundary
    {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "boundary"},
            {"type", "city_boundary"}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(model.border)}
        };
        features.push_back(feature);
    }

    // 2. Ward boundaries
    for (const auto& patch : model.patches) {
        if (!patch->ward) continue;

        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "wards"},
            {"ward_type", wardTypeName(patch->ward->type)},
            {"label", patch->ward->getLabel()},
            {"color", getWardColor(patch->ward->type)},
            {"within_walls", patch->withinWalls},
            {"within_city", patch->withinCity}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(patch->shape)}
        };
        features.push_back(feature);
    }

    // 3. Buildings
    for (const auto& ward : model.wards) {
        for (const auto& building : ward->geometry) {
            json feature;
            feature["type"] = "Feature";
            feature["properties"] = {
                {"layer", "buildings"},
                {"ward_type", wardTypeName(ward->type)},
                {"color", getWardColor(ward->type)}
            };
            feature["geometry"] = {
                {"type", "Polygon"},
                {"coordinates", polygonToCoords(building)}
            };
            features.push_back(feature);
        }
    }

    // 4. Walls
    if (model.wall) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "walls"},
            {"type", "main_wall"}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(model.wall->shape)}
        };
        features.push_back(feature);
    }

    if (model.citadel) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "walls"},
            {"type", "citadel"}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(model.citadel->shape)}
        };
        features.push_back(feature);
    }

    // 5. Towers
    if (model.wall) {
        for (const auto& tower : model.wall->towers) {
            json feature;
            feature["type"] = "Feature";
            feature["properties"] = {
                {"layer", "towers"},
                {"type", "wall_tower"}
            };
            feature["geometry"] = {
                {"type", "Point"},
                {"coordinates", {tower.x, tower.y}}
            };
            features.push_back(feature);
        }
    }

    // 6. Gates
    for (const auto& gate : model.gates) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "gates"},
            {"type", "city_gate"}
        };
        feature["geometry"] = {
            {"type", "Point"},
            {"coordinates", {gate.x, gate.y}}
        };
        features.push_back(feature);
    }

    // 7. Streets
    for (const auto& street : model.streets) {
        if (street.path.size() < 2) continue;

        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "streets"},
            {"type", street.isMainStreet ? "main_street" : "street"},
            {"width", street.width}
        };
        feature["geometry"] = {
            {"type", "LineString"},
            {"coordinates", pathToCoords(street.path)}
        };
        features.push_back(feature);
    }

    for (const auto& road : model.roads) {
        if (road.path.size() < 2) continue;

        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "streets"},
            {"type", "road"},
            {"width", road.width}
        };
        feature["geometry"] = {
            {"type", "LineString"},
            {"coordinates", pathToCoords(road.path)}
        };
        features.push_back(feature);
    }

    // 8. Plaza
    if (model.plaza) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "plaza"},
            {"type", "central_plaza"}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(*model.plaza)}
        };
        features.push_back(feature);
    }

    // 9. Trees
    std::mt19937 rng(model.params.seed);
    auto trees = generateTrees(model, treeDensity, rng);
    for (const auto& tree : trees) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "trees"},
            {"type", "tree"}
        };
        feature["geometry"] = {
            {"type", "Point"},
            {"coordinates", {tree.x, tree.y}}
        };
        features.push_back(feature);
    }

    // 10. Rivers
    for (const auto& river : model.water.rivers) {
        Polygon shape = river.getMergedShape();
        if (shape.empty()) continue;

        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "water"},
            {"type", river.name == "Coast" ? "coast" : "river"},
            {"name", river.name}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(shape)}
        };
        features.push_back(feature);
    }

    // 11. Ponds
    for (const auto& pond : model.water.ponds) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "water"},
            {"type", pond.isNatural ? "pond" : "fountain"},
            {"name", pond.name}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(pond.shape)}
        };
        features.push_back(feature);
    }

    // 12. Bridges
    for (const auto& bridge : model.water.bridges) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "bridges"},
            {"type", bridge.isArched ? "arched_bridge" : "flat_bridge"},
            {"width", bridge.width}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(bridge.toPolygon())}
        };
        features.push_back(feature);
    }

    // 13. Piers
    for (const auto& pier : model.water.piers) {
        json feature;
        feature["type"] = "Feature";
        feature["properties"] = {
            {"layer", "piers"},
            {"type", "pier"},
            {"width", pier.width}
        };
        feature["geometry"] = {
            {"type", "Polygon"},
            {"coordinates", polygonToCoords(pier.toPolygon())}
        };
        features.push_back(feature);
    }

    // Create GeoJSON FeatureCollection
    json geojson;
    geojson["type"] = "FeatureCollection";
    geojson["properties"] = {
        {"generator", "city_generator"},
        {"seed", model.params.seed},
        {"radius", model.params.radius}
    };
    geojson["features"] = features;

    // Write to file
    std::ofstream file(path);
    file << geojson.dump(2);
}

// Export city to SVG for quick preview
inline void exportSVG(const Model& model, const std::string& path,
                      int width = 1024, int height = 1024, float treeDensity = 1.0f) {
    std::ofstream svg(path);

    // Calculate view bounds
    AABB bounds;
    for (const auto& v : model.border.vertices) {
        bounds.expand(v);
    }
    float margin = bounds.size().x * 0.1f;
    bounds.min -= Vec2{margin, margin};
    bounds.max += Vec2{margin, margin};

    // Transform helper
    auto transform = [&](const Vec2& p) -> std::pair<float, float> {
        float x = (p.x - bounds.min.x) / bounds.size().x * width;
        float y = height - (p.y - bounds.min.y) / bounds.size().y * height;  // Flip Y
        return {x, y};
    };

    // Polygon to SVG path
    auto polyToPath = [&](const Polygon& poly) -> std::string {
        if (poly.empty()) return "";
        std::string d = "M";
        for (size_t i = 0; i < poly.size(); i++) {
            auto [x, y] = transform(poly[i]);
            d += std::to_string(x) + "," + std::to_string(y);
            if (i < poly.size() - 1) d += " L";
        }
        d += " Z";
        return d;
    };

    // Write SVG header
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height << "\">\n";

    // Background
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#f5f0e1\"/>\n";

    // Define styles
    svg << "<defs>\n";
    svg << "  <style>\n";
    svg << "    .ward { stroke: #333; stroke-width: 0.5; }\n";
    svg << "    .building { stroke: #222; stroke-width: 0.3; }\n";
    svg << "    .wall { fill: none; stroke: #4a3728; stroke-width: 3; }\n";
    svg << "    .tower { fill: #4a3728; }\n";
    svg << "    .gate { fill: #8B4513; stroke: #333; stroke-width: 1; }\n";
    svg << "    .street { stroke: #d4c4a8; stroke-linecap: round; }\n";
    svg << "    .main-street { stroke: #c9b896; stroke-linecap: round; }\n";
    svg << "    .plaza { fill: #e8dcc8; stroke: #b8a888; stroke-width: 1; }\n";
    svg << "    .tree { fill: #228B22; }\n";
    svg << "    .water { fill: #4a90d9; stroke: #2171b5; stroke-width: 1; }\n";
    svg << "    .coast { fill: #4a90d9; stroke: none; }\n";
    svg << "    .bridge { fill: #8B7355; stroke: #4a3728; stroke-width: 1; }\n";
    svg << "    .pier { fill: #8B7355; stroke: #4a3728; stroke-width: 0.5; }\n";
    svg << "  </style>\n";
    svg << "</defs>\n";

    // Layer: Water (draw first, under everything)
    svg << "<g id=\"water\">\n";
    for (const auto& river : model.water.rivers) {
        Polygon shape = river.getMergedShape();
        if (!shape.empty()) {
            std::string cls = river.name == "Coast" ? "coast" : "water";
            svg << "  <path class=\"" << cls << "\" d=\"" << polyToPath(shape) << "\"/>\n";
        }
    }
    for (const auto& pond : model.water.ponds) {
        svg << "  <path class=\"water\" d=\"" << polyToPath(pond.shape) << "\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Wards
    svg << "<g id=\"wards\">\n";
    for (const auto& patch : model.patches) {
        if (!patch->ward) continue;
        svg << "  <path class=\"ward\" fill=\"" << getWardColor(patch->ward->type)
            << "\" fill-opacity=\"0.3\" d=\"" << polyToPath(patch->shape) << "\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Plaza
    if (model.plaza) {
        svg << "<g id=\"plaza\">\n";
        svg << "  <path class=\"plaza\" d=\"" << polyToPath(*model.plaza) << "\"/>\n";
        svg << "</g>\n";
    }

    // Layer: Streets
    svg << "<g id=\"streets\">\n";
    for (const auto& street : model.streets) {
        if (street.path.size() < 2) continue;
        svg << "  <polyline class=\"" << (street.isMainStreet ? "main-street" : "street")
            << "\" stroke-width=\"" << (street.width * 2) << "\" points=\"";
        for (const auto& p : street.path) {
            auto [x, y] = transform(p);
            svg << x << "," << y << " ";
        }
        svg << "\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Buildings
    svg << "<g id=\"buildings\">\n";
    for (const auto& ward : model.wards) {
        for (const auto& building : ward->geometry) {
            svg << "  <path class=\"building\" fill=\"" << getWardColor(ward->type)
                << "\" d=\"" << polyToPath(building) << "\"/>\n";
        }
    }
    svg << "</g>\n";

    // Layer: Walls
    svg << "<g id=\"walls\">\n";
    if (model.wall) {
        svg << "  <path class=\"wall\" d=\"" << polyToPath(model.wall->shape) << "\"/>\n";
    }
    if (model.citadel) {
        svg << "  <path class=\"wall\" stroke-width=\"4\" d=\"" << polyToPath(model.citadel->shape) << "\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Towers
    svg << "<g id=\"towers\">\n";
    if (model.wall) {
        for (const auto& tower : model.wall->towers) {
            auto [x, y] = transform(tower);
            svg << "  <circle class=\"tower\" cx=\"" << x << "\" cy=\"" << y << "\" r=\"4\"/>\n";
        }
    }
    svg << "</g>\n";

    // Layer: Gates
    svg << "<g id=\"gates\">\n";
    for (const auto& gate : model.gates) {
        auto [x, y] = transform(gate);
        svg << "  <rect class=\"gate\" x=\"" << (x-5) << "\" y=\"" << (y-5)
            << "\" width=\"10\" height=\"10\" rx=\"2\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Trees
    std::mt19937 rng(model.params.seed);
    auto trees = generateTrees(model, treeDensity, rng);
    svg << "<g id=\"trees\">\n";
    for (const auto& tree : trees) {
        auto [x, y] = transform(tree);
        svg << "  <circle class=\"tree\" cx=\"" << x << "\" cy=\"" << y << "\" r=\"2\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Bridges (over water)
    svg << "<g id=\"bridges\">\n";
    for (const auto& bridge : model.water.bridges) {
        svg << "  <path class=\"bridge\" d=\"" << polyToPath(bridge.toPolygon()) << "\"/>\n";
    }
    svg << "</g>\n";

    // Layer: Piers (into water)
    svg << "<g id=\"piers\">\n";
    for (const auto& pier : model.water.piers) {
        svg << "  <path class=\"pier\" d=\"" << polyToPath(pier.toPolygon()) << "\"/>\n";
    }
    svg << "</g>\n";

    // Close SVG
    svg << "</svg>\n";
}

} // namespace city
