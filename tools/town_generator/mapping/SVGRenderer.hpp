/**
 * SVGRenderer.hpp - Port of CityMap.hx rendering to SVG
 *
 * This is a direct port of the OpenFL/Flash rendering logic from CityMap.hx.
 * The goal is to preserve the exact visual output while outputting SVG instead
 * of using OpenFL's Graphics API. Do NOT "fix" issues by changing how the
 * rendering works - fix root causes instead.
 */
#pragma once

#include <string>
#include <memory>
#include <cmath>

#include "../mapping/SVGWriter.hpp"
#include "../mapping/Palette.hpp"
#include "../mapping/Brush.hpp"
#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../building/Model.hpp"
#include "../building/Patch.hpp"
#include "../wards/Ward.hpp"
#include "../building/CurtainWall.hpp"

// Ward types for identification
#include "../wards/Castle.hpp"
#include "../wards/Cathedral.hpp"
#include "../wards/Market.hpp"
#include "../wards/Park.hpp"
#include "../wards/CommonWard.hpp"
#include "../wards/CraftsmenWard.hpp"
#include "../wards/MerchantWard.hpp"
#include "../wards/GateWard.hpp"
#include "../wards/Slum.hpp"
#include "../wards/AdministrationWard.hpp"
#include "../wards/MilitaryWard.hpp"
#include "../wards/PatriciateWard.hpp"
#include "../wards/Farm.hpp"

namespace town {

class SVGRenderer {
private:
    std::shared_ptr<Model> model;
    Palette palette;
    float strokeScale;
    SVGWriter svg;

    // Rendering methods ported from CityMap.hx

    void renderRoad(const Polygon& road) {
        // First stroke: outline (medium color, thicker)
        // g.lineStyle( Ward.MAIN_STREET + Brush.NORMAL_STROKE, palette.medium, false, null, CapsStyle.NONE );
        svg.polyline(road, palette.mediumHex(),
                     (Ward::MAIN_STREET + Brush::NORMAL_STROKE) * strokeScale,
                     "butt");

        // Second stroke: center line (paper color, thinner)
        // g.lineStyle( Ward.MAIN_STREET - Brush.NORMAL_STROKE, palette.paper );
        svg.polyline(road, palette.paperHex(),
                     (Ward::MAIN_STREET - Brush::NORMAL_STROKE) * strokeScale,
                     "butt");
    }

    void renderBuilding(const std::vector<Polygon>& blocks,
                        const std::string& fillColor,
                        const std::string& lineColor,
                        float thickness) {
        // First pass: draw outlines (thick strokes)
        // brush.setStroke( g, line, thickness * 2 );
        // for (block in blocks) g.drawPolygon( block );
        for (const auto& block : blocks) {
            svg.polygon(block, "none", lineColor, thickness * 2 * strokeScale, "miter");
        }

        // Second pass: draw fills (no stroke)
        // brush.noStroke( g );
        // brush.setFill( g, fill );
        // for (block in blocks) g.drawPolygon( block );
        for (const auto& block : blocks) {
            svg.polygon(block, fillColor, "none", 0);
        }
    }

    void renderTower(const PointPtr& p, float r) {
        // g.beginFill( palette.dark );
        // g.drawCircle( p.x, p.y, r );
        // g.endFill();
        svg.circle(p->x, p->y, r * strokeScale, palette.darkHex());
    }

    void renderGate(const Polygon& wall, const PointPtr& gate) {
        // g.lineStyle( Brush.THICK_STROKE * 2, palette.dark, false, null, CapsStyle.NONE );
        // var dir = wall.next( gate ).subtract( wall.prev( gate ) );
        // dir.normalize( Brush.THICK_STROKE * 1.5 );
        // g.moveToPoint( gate.subtract( dir ) );
        // g.lineToPoint( gate.add( dir ) );

        PointPtr nextPt = wall.next(gate);
        PointPtr prevPt = wall.prev(gate);
        if (!nextPt || !prevPt) return;
        Point dir = nextPt->subtract(*prevPt);
        dir.normalize(Brush::THICK_STROKE * 1.5f * strokeScale);

        Point p1 = gate->subtract(dir);
        Point p2 = gate->add(dir);

        svg.line(p1.x, p1.y, p2.x, p2.y,
                 palette.darkHex(),
                 Brush::THICK_STROKE * 2 * strokeScale,
                 "butt");
    }

    void renderWall(CurtainWall& wall, bool large) {
        // g.lineStyle( Brush.THICK_STROKE, palette.dark );
        // g.drawPolygon( wall.shape );
        svg.polygon(wall.shape, "none", palette.darkHex(),
                    Brush::THICK_STROKE * strokeScale, "miter");

        // for (gate in wall.gates) drawGate( g, wall.shape, gate );
        for (const auto& gate : wall.gates) {
            renderGate(wall.shape, gate);
        }

        // for (t in wall.towers) drawTower( g, t, Brush.THICK_STROKE * (large ? 1.5 : 1) );
        float towerRadius = Brush::THICK_STROKE * (large ? 1.5f : 1.0f);
        for (const auto& t : wall.towers) {
            renderTower(t, towerRadius);
        }
    }

    void renderPatch(std::shared_ptr<Patch> patch, int index) {
        if (!patch->ward) return;

        const auto& ward = patch->ward;
        const std::string label = ward->getLabel();
        std::string wardClass = label.empty() ? "unknown" : label;

        // Convert to lowercase for class name
        for (auto& c : wardClass) c = std::tolower(c);

        svg.beginGroup("patch-" + std::to_string(index), wardClass);

        // Switch on ward type - same logic as CityMap.hx
        // Labels returned by getLabel(): Castle, Temple, Market, Craftsmen, Merchant,
        // Gate, Slum, Administration, Military, Patriciate, Farm, Park
        if (label == "Castle") {
            // drawBuilding with 2x stroke
            renderBuilding(ward->geometry, palette.lightHex(), palette.darkHex(),
                           Brush::NORMAL_STROKE * 2);
        }
        else if (label == "Temple") {  // Cathedral returns "Temple"
            // drawBuilding with normal stroke
            renderBuilding(ward->geometry, palette.lightHex(), palette.darkHex(),
                           Brush::NORMAL_STROKE);
        }
        else if (label == "Market" || label == "Craftsmen" || label == "Merchant" ||
                 label == "Gate" || label == "Slum" || label == "Administration" ||
                 label == "Military" || label == "Patriciate" || label == "Farm") {
            // Standard fill + stroke for residential/commercial wards
            for (const auto& building : ward->geometry) {
                svg.polygon(building, palette.lightHex(), palette.darkHex(),
                            Brush::NORMAL_STROKE * strokeScale, "miter");
            }
        }
        else if (label == "Park") {
            // Fill with medium color, no stroke (green areas)
            for (const auto& grove : ward->geometry) {
                svg.polygon(grove, palette.mediumHex(), "none", 0);
            }
        }
        // default: nothing rendered (empty label or base Ward)

        svg.endGroup();
    }

public:
    SVGRenderer(std::shared_ptr<Model> model, const Palette& palette, float strokeScale = 1.0f)
        : model(model), palette(palette), strokeScale(strokeScale) {}

    std::string render(float width, float height, bool useViewBox = false) {
        svg.clear();

        // Calculate bounds for viewBox
        float cityRadius = model->cityRadius;
        float margin = cityRadius * 0.1f;
        float viewMinX = -cityRadius - margin;
        float viewMinY = -cityRadius - margin;
        float viewWidth = (cityRadius + margin) * 2;
        float viewHeight = (cityRadius + margin) * 2;

        if (useViewBox) {
            svg.beginDocument(width, height, viewMinX, viewMinY, viewWidth, viewHeight);
        } else {
            // Fixed dimensions with viewBox for scaling
            svg.beginDocument(width, height, viewMinX, viewMinY, viewWidth, viewHeight);
        }

        // Background rectangle
        svg.rect(viewMinX, viewMinY, viewWidth, viewHeight, palette.paperHex());

        // Render roads
        // for (road in model.roads) { ... drawRoad( roadView.graphics, road ); }
        svg.beginGroup("roads");
        for (const auto& road : model->roads) {
            renderRoad(road);
        }
        svg.endGroup();

        // Render patches
        // for (patch in model.patches) { ... }
        svg.beginGroup("patches");
        int patchIndex = 0;
        for (const auto& patch : model->patches) {
            renderPatch(patch, patchIndex++);
        }
        svg.endGroup();

        // Render walls
        svg.beginGroup("walls");

        // if (model.wall != null) drawWall( walls.graphics, model.wall, false );
        if (model->wall) {
            renderWall(*model->wall, false);
        }

        // if (model.citadel != null) drawWall( walls.graphics, cast( model.citadel.ward, Castle).wall, true );
        if (model->citadel && model->citadel->ward) {
            auto castle = std::dynamic_pointer_cast<Castle>(model->citadel->ward);
            if (castle && castle->wall) {
                renderWall(*castle->wall, true);
            }
        }

        svg.endGroup();

        svg.endDocument();

        return svg.toString();
    }
};

} // namespace town
