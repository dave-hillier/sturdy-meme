#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * MerchantWard - Trading district with shops
 */
class MerchantWard : public Ward {
public:
    MerchantWard() = default;

    std::string getName() const override { return "Merchant"; }

    void createGeometry() override;

    bool operator==(const MerchantWard& other) const { return Ward::operator==(other); }
    bool operator!=(const MerchantWard& other) const { return !(*this == other); }
};

} // namespace wards
} // namespace town_generator
