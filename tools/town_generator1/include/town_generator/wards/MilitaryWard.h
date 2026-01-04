#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * MilitaryWard - Barracks and military installations
 */
class MilitaryWard : public Ward {
public:
    MilitaryWard() = default;

    std::string getName() const override { return "Military"; }

    void createGeometry() override;

    bool operator==(const MilitaryWard& other) const { return Ward::operator==(other); }
    bool operator!=(const MilitaryWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
