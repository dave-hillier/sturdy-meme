#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * AdministrationWard - Government buildings
 */
class AdministrationWard : public Ward {
public:
    AdministrationWard() = default;

    std::string getName() const override { return "Administration"; }

    void createGeometry() override;

    bool operator==(const AdministrationWard& other) const { return Ward::operator==(other); }
    bool operator!=(const AdministrationWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
