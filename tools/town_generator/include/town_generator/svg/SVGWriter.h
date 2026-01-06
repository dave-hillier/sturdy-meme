#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Model.h"
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace town_generator {
namespace svg {

/**
 * SVGWriter - Outputs city model as SVG
 */
class SVGWriter {
public:
    // Style configuration matching mfcg.js default palette
    struct Style {
        // Building colors
        std::string buildingFill;       // colorRoof
        std::string buildingStroke;     // colorDark
        double buildingStrokeWidth;

        // Street colors - use dark ink for outlines
        std::string streetStroke;       // colorDark
        double streetStrokeWidth;

        std::string arteryStroke;       // colorDark
        double arteryStrokeWidth;

        std::string roadStroke;         // colorDark (for outlines)
        double roadStrokeWidth;

        // Alley settings
        std::string alleyStroke;        // colorDark
        double alleyStrokeWidth;

        // Wall colors
        std::string wallStroke;         // colorDark (walls same as ink)
        double wallStrokeWidth;

        std::string towerFill;          // colorDark
        std::string towerStroke;        // colorDark (for outline)
        double towerRadius;
        double citadelTowerRadius;      // larger for citadel

        // Gate rendering (as gaps with flanking towers)
        double gateWidth;               // gap width between flanking towers

        // Patches (debug)
        std::string patchStroke;
        double patchStrokeWidth;

        // Background
        std::string backgroundColor;    // colorPaper

        // Water
        std::string waterFill;          // colorWater
        std::string waterStroke;
        double waterStrokeWidth;
        std::string shoreFill;

        // Green areas (farms, parks)
        std::string greenFill;          // colorGreen

        // mfcg.js default palette values:
        // colorPaper = 13419960 = #CCC6B8
        // colorDark = 1710359 = #1A1917
        // colorRoof = 10854549 = #A5A095
        // colorWater = 8354417 = #7F7671
        // colorGreen = 10854291 = #A5A013

        Style()
            : buildingFill("#A5A095")     // colorRoof - grayish
            , buildingStroke("#1A1917")   // colorDark - ink
            , buildingStrokeWidth(0.3)    // thin strokes like mfcg
            , streetStroke("#1A1917")     // colorDark
            , streetStrokeWidth(0.8)      // slightly thicker than alleys
            , arteryStroke("#1A1917")     // colorDark
            , arteryStrokeWidth(2.0)      // MFCG: roadWidth = 2 * SCALE (base 2.0)
            , roadStroke("#1A1917")       // colorDark
            , roadStrokeWidth(2.0)        // MFCG: roadWidth = 2 * SCALE (base 2.0)
            , alleyStroke("#1A1917")      // colorDark
            , alleyStrokeWidth(0.6)       // MFCG: drawFill(.6) for alleys
            , wallStroke("#1A1917")       // colorDark (walls = ink)
            , wallStrokeWidth(1.9)        // THICKNESS from mfcg
            , towerFill("#1A1917")        // colorDark
            , towerStroke("#1A1917")      // colorDark
            , towerRadius(1.9)            // TOWER_RADIUS from mfcg
            , citadelTowerRadius(2.5)     // LTOWER_RADIUS from mfcg
            , gateWidth(6.0)              // wider gate opening
            , patchStroke("#e0d5c0")
            , patchStrokeWidth(0.3)
            , backgroundColor("#CCC6B8")  // colorPaper - warm beige
            , waterFill("#7F7671")        // colorWater
            , waterStroke("#1A1917")      // colorDark
            , waterStrokeWidth(0.3)
            , shoreFill("#CCC6B8")        // same as paper
            , greenFill("#A5A013")        // colorGreen
        {}
    };

    static bool write(const building::Model& model, const std::string& filename, const Style& style = Style());
    static std::string generate(const building::Model& model, const Style& style = Style());

    // Equality
    bool operator==(const SVGWriter& other) const { return true; }
    bool operator!=(const SVGWriter& other) const { return false; }

private:
    static std::string polygonToPath(const geom::Polygon& poly);
    static std::string polylineToPath(const std::vector<geom::Point>& points);
    static std::string polylineToPath(const building::Model::Street& street);  // For PointPtr streets
};

} // namespace svg
} // namespace town_generator
