#pragma once

#include "interfaces/IWaterControl.h"

class WaterSystem;
class WaterTileCull;

/**
 * WaterControlSubsystem - Implements IWaterControl
 * Wraps WaterSystem and WaterTileCull for water rendering control.
 */
class WaterControlSubsystem : public IWaterControl {
public:
    WaterControlSubsystem(WaterSystem& water, WaterTileCull& waterTileCull)
        : water_(water)
        , waterTileCull_(waterTileCull) {}

    WaterSystem& getWaterSystem() override;
    const WaterSystem& getWaterSystem() const override;
    WaterTileCull& getWaterTileCull() override;
    const WaterTileCull& getWaterTileCull() const override;

private:
    WaterSystem& water_;
    WaterTileCull& waterTileCull_;
};
