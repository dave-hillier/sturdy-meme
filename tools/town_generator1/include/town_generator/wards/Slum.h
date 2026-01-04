#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Slum - Poor district with dense, irregular housing
 */
class Slum : public Ward {
public:
    Slum() = default;

    std::string getName() const override { return "Slum"; }

    void createGeometry() override;

    bool operator==(const Slum& other) const { return Ward::operator==(other); }
    bool operator!=(const Slum& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
