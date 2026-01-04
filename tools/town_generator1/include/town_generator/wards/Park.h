#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Park - Open green space
 */
class Park : public Ward {
public:
    Park() = default;

    std::string getName() const override { return "Park"; }

    void createGeometry() override;

    bool operator==(const Park& other) const { return Ward::operator==(other); }
    bool operator!=(const Park& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
