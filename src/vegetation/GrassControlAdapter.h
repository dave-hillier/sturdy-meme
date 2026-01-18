#pragma once

#include "core/interfaces/IGrassControl.h"
#include "GrassLODStrategy.h"
#include <memory>

class GrassSystem;
class GrassTileManager;

/**
 * Adapter that implements IGrassControl by delegating to GrassSystem.
 * Follows composition pattern to avoid modifying GrassSystem's interface.
 */
class GrassControlAdapter : public IGrassControl {
public:
    explicit GrassControlAdapter(GrassSystem& grassSystem);

    // LOD preset management
    LODPreset getLODPreset() const override;
    void setLODPreset(LODPreset preset) override;
    std::string getLODStrategyName() const override;

    // Statistics
    uint32_t getActiveTileCount() const override;
    uint32_t getPendingLoadCount() const override;
    uint32_t getTotalLoadedTiles() const override;
    uint32_t getActiveTileCountAtLOD(uint32_t lod) const override;

    // Debug visualization
    bool isDebugVisualizationEnabled() const override;
    void setDebugVisualizationEnabled(bool enabled) override;
    bool isTileBoundsVisualizationEnabled() const override;
    void setTileBoundsVisualizationEnabled(bool enabled) override;

    // LOD strategy access
    const IGrassLODStrategy* getLODStrategy() const override;

    // Configuration
    uint32_t getMaxLoadsPerFrame() const override;
    void setMaxLoadsPerFrame(uint32_t max) override;

    // LOD info
    float getTileSizeForLOD(uint32_t lod) const override;
    uint32_t getNumLODLevels() const override;

private:
    GrassSystem& grassSystem_;
    LODPreset currentPreset_ = LODPreset::Default;
    bool debugVisualizationEnabled_ = false;
    bool tileBoundsVisualizationEnabled_ = false;
};
