#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Market - Market square with stalls
 * Faithful to mfcg.js Market ward
 */
class Market : public Ward {
public:
    Market() = default;

    std::string getName() const override { return "Market"; }

    void createGeometry() override;

    /**
     * Get available space for Market (custom - uses zero insets)
     * Faithful to mfcg.js Market.getAvailable (lines 652-677)
     * Market fills the entire patch except for canal edges
     */
    geom::Polygon getAvailable();

    // Monument/fountain placed in the space
    geom::Polygon space;

    bool operator==(const Market& other) const { return Ward::operator==(other); }
    bool operator!=(const Market& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
