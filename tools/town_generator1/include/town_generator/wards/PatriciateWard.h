#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * PatriciateWard - Wealthy district with large houses
 */
class PatriciateWard : public Ward {
public:
    PatriciateWard() = default;

    std::string getName() const override { return "Patriciate"; }

    void createGeometry() override;

    bool operator==(const PatriciateWard& other) const { return Ward::operator==(other); }
    bool operator!=(const PatriciateWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
