#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Castle - Castle ward with keep and curtain wall
 */
class Castle : public Ward {
public:
    Castle() = default;

    std::string getName() const override { return "Castle"; }

    void createGeometry() override;

    bool operator==(const Castle& other) const { return Ward::operator==(other); }
    bool operator!=(const Castle& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
