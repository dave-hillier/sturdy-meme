#include "GrassControlAdapter.h"
#include "GrassSystem.h"
#include "GrassTileManager.h"
#include "GrassTileTracker.h"
#include "GrassLODStrategy.h"

GrassControlAdapter::GrassControlAdapter(GrassSystem& grassSystem)
    : grassSystem_(grassSystem) {
}

IGrassControl::LODPreset GrassControlAdapter::getLODPreset() const {
    return currentPreset_;
}

void GrassControlAdapter::setLODPreset(LODPreset preset) {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    if (!tileManager) return;

    std::unique_ptr<IGrassLODStrategy> strategy;
    switch (preset) {
        case LODPreset::Default:
            strategy = createDefaultGrassLODStrategy();
            break;
        case LODPreset::Performance:
            strategy = createPerformanceGrassLODStrategy();
            break;
        case LODPreset::Quality:
            strategy = createQualityGrassLODStrategy();
            break;
        case LODPreset::Ultra:
            strategy = createUltraGrassLODStrategy();
            break;
    }

    tileManager->setLODStrategy(std::move(strategy));
    currentPreset_ = preset;
}

std::string GrassControlAdapter::getLODStrategyName() const {
    const IGrassLODStrategy* strategy = getLODStrategy();
    if (strategy) {
        return strategy->getName();
    }
    return "Unknown";
}

uint32_t GrassControlAdapter::getActiveTileCount() const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    return tileManager ? static_cast<uint32_t>(tileManager->getActiveTileCount()) : 0;
}

uint32_t GrassControlAdapter::getPendingLoadCount() const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    return tileManager ? static_cast<uint32_t>(tileManager->getPendingLoadCount()) : 0;
}

uint32_t GrassControlAdapter::getTotalLoadedTiles() const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    return tileManager ? static_cast<uint32_t>(tileManager->getTotalTileCount()) : 0;
}

uint32_t GrassControlAdapter::getActiveTileCountAtLOD(uint32_t lod) const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    if (!tileManager) return 0;

    const GrassTileTracker& tracker = tileManager->getTracker();
    auto tiles = tracker.getActiveTilesAtLod(lod);
    return static_cast<uint32_t>(tiles.size());
}

bool GrassControlAdapter::isDebugVisualizationEnabled() const {
    return debugVisualizationEnabled_;
}

void GrassControlAdapter::setDebugVisualizationEnabled(bool enabled) {
    debugVisualizationEnabled_ = enabled;
    // TODO: Hook up to actual debug rendering system when implemented
}

bool GrassControlAdapter::isTileBoundsVisualizationEnabled() const {
    return tileBoundsVisualizationEnabled_;
}

void GrassControlAdapter::setTileBoundsVisualizationEnabled(bool enabled) {
    tileBoundsVisualizationEnabled_ = enabled;
    // TODO: Hook up to tile bounds debug rendering when implemented
}

const IGrassLODStrategy* GrassControlAdapter::getLODStrategy() const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    return tileManager ? tileManager->getLODStrategy() : nullptr;
}

uint32_t GrassControlAdapter::getMaxLoadsPerFrame() const {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    if (!tileManager) return 0;

    return tileManager->getLoadQueue().getMaxLoadsPerFrame();
}

void GrassControlAdapter::setMaxLoadsPerFrame(uint32_t max) {
    GrassTileManager* tileManager = grassSystem_.getTileManager();
    if (!tileManager) return;

    auto config = tileManager->getLoadQueue().getConfig();
    config.maxLoadsPerFrame = max;
    tileManager->getLoadQueue().setConfig(config);
}

float GrassControlAdapter::getTileSizeForLOD(uint32_t lod) const {
    const IGrassLODStrategy* strategy = getLODStrategy();
    return strategy ? strategy->getTileSize(lod) : 0.0f;
}

uint32_t GrassControlAdapter::getNumLODLevels() const {
    const IGrassLODStrategy* strategy = getLODStrategy();
    return strategy ? strategy->getNumLODLevels() : 0;
}
