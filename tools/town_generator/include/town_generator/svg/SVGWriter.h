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
    // Style configuration
    struct Style {
        std::string buildingFill;
        std::string buildingStroke;
        double buildingStrokeWidth;

        std::string streetStroke;
        double streetStrokeWidth;

        std::string arteryStroke;
        double arteryStrokeWidth;

        std::string roadStroke;
        double roadStrokeWidth;

        std::string wallStroke;
        double wallStrokeWidth;

        std::string towerFill;
        std::string towerStroke;
        double towerRadius;

        std::string gateFill;

        std::string patchStroke;
        double patchStrokeWidth;

        std::string backgroundColor;

        Style()
            : buildingFill("#d4c4a8")
            , buildingStroke("#5c4033")  // darker stroke for visible divisions
            , buildingStrokeWidth(1.2)   // thicker to match 4x scale
            , streetStroke("#0000ff")  // DEBUG: blue for streets/alleys
            , streetStrokeWidth(1.0)
            , arteryStroke("#ff0000")  // DEBUG: red for arteries/roads
            , arteryStrokeWidth(2.0)
            , roadStroke("#ff0000")    // DEBUG: red for roads
            , roadStrokeWidth(1.5)
            , wallStroke("#5c4033")
            , wallStrokeWidth(3.0)
            , towerFill("#8b7355")
            , towerStroke("#5c4033")
            , towerRadius(2.0)
            , gateFill("#c8b89a")
            , patchStroke("#e0d5c0")
            , patchStrokeWidth(0.3)
            , backgroundColor("#f5f0e6")
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
