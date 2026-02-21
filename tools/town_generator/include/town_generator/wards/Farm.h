#pragma once

#include "town_generator/wards/Ward.h"
#include <vector>

namespace town_generator {
namespace wards {

/**
 * Farm - Agricultural land with farmhouses
 * Faithful port from mfcg.js (lines 12608-12765)
 */
class Farm : public Ward {
public:
    // Constants matching mfcg.js
    static constexpr double MIN_SUBPLOT = 400.0;  // Minimum subplot area (faithful to MFCG)
    static constexpr double MIN_FURROW = 1.3;     // Minimum furrow spacing (faithful to MFCG)

    // Subplot and furrow data for rendering
    std::vector<geom::Polygon> subPlots;
    struct Furrow {
        geom::Point start;
        geom::Point end;
    };
    std::vector<Furrow> furrows;
    std::vector<geom::Polygon> farmBuildings;  // Separate from geometry for proper filtering

    Farm() = default;

    std::string getName() const override { return "Farm"; }

    void createGeometry() override;

    bool operator==(const Farm& other) const { return Ward::operator==(other); }
    bool operator!=(const Farm& other) const { return !(*this == other); }

private:
    // Get available area after accounting for roads, walls, and neighboring ward types
    geom::Polygon getAvailable() override;

    // Recursively split field into subplots
    std::vector<geom::Polygon> splitField(const geom::Polygon& field);

    // Round sharp corners of subplot
    geom::Polygon roundCorners(const geom::Polygon& subplot);

    // Create housing building for a subplot
    geom::Polygon createHousing(const geom::Polygon& subplot);

    // Check if an edge touches a non-Farm neighboring ward
    bool edgeTouchesNonFarm(const geom::Point& v0, const geom::Point& v1);
};

} // namespace wards
} // namespace town_generator
