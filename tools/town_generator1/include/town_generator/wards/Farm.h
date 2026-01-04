#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Farm - Agricultural land with farmhouses
 */
class Farm : public Ward {
public:
    Farm() = default;

    std::string getName() const override { return "Farm"; }

    void createGeometry() override;

    bool operator==(const Farm& other) const { return Ward::operator==(other); }
    bool operator!=(const Farm& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
