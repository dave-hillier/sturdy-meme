#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Market - Market square with stalls
 */
class Market : public Ward {
public:
    Market() = default;

    std::string getName() const override { return "Market"; }

    void createGeometry() override;

    bool operator==(const Market& other) const { return Ward::operator==(other); }
    bool operator!=(const Market& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
