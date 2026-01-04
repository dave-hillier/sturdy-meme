#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * GateWard - Area around city gate
 */
class GateWard : public Ward {
public:
    GateWard() = default;

    std::string getName() const override { return "Gate"; }

    void createGeometry() override;

    bool operator==(const GateWard& other) const { return Ward::operator==(other); }
    bool operator!=(const GateWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
