#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Harbour - Waterfront district with piers and docks
 * Faithful port from mfcg.js Harbour ward
 */
class Harbour : public Ward {
public:
    Harbour() = default;

    std::string getName() const override { return "Harbour"; }

    void createGeometry() override;

    // Get piers for rendering
    const std::vector<geom::Polygon>& getPiers() const { return piers; }

private:
    // Pier structures
    std::vector<geom::Polygon> piers;
};

} // namespace wards
} // namespace town_generator
