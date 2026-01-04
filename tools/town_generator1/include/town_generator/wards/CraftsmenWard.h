#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * CraftsmenWard - Artisan district with workshops
 */
class CraftsmenWard : public Ward {
public:
    CraftsmenWard() = default;

    std::string getName() const override { return "Craftsmen"; }

    void createGeometry() override;

    bool operator==(const CraftsmenWard& other) const { return Ward::operator==(other); }
    bool operator!=(const CraftsmenWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
