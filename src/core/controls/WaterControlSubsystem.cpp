#include "WaterControlSubsystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"

WaterSystem& WaterControlSubsystem::getWaterSystem() {
    return water_;
}

const WaterSystem& WaterControlSubsystem::getWaterSystem() const {
    return water_;
}

WaterTileCull& WaterControlSubsystem::getWaterTileCull() {
    return waterTileCull_;
}

const WaterTileCull& WaterControlSubsystem::getWaterTileCull() const {
    return waterTileCull_;
}
