#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Cathedral - Religious district with large church
 */
class Cathedral : public Ward {
public:
    Cathedral() = default;

    std::string getName() const override { return "Cathedral"; }

    void createGeometry() override;

    bool operator==(const Cathedral& other) const { return Ward::operator==(other); }
    bool operator!=(const Cathedral& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
