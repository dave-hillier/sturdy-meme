#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * CommonWard - Common residential district
 */
class CommonWard : public Ward {
public:
    CommonWard() = default;

    std::string getName() const override { return "Common"; }

    void createGeometry() override;

    bool operator==(const CommonWard& other) const { return Ward::operator==(other); }
    bool operator!=(const CommonWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
