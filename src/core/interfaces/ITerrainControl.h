#pragma once

#include <cstdint>

class TerrainSystem;

/**
 * Interface for terrain rendering controls.
 * Used by GuiTerrainTab to control terrain LOD, wireframe, and meshlets.
 */
class ITerrainControl {
public:
    virtual ~ITerrainControl() = default;

    // Terrain enable/disable
    virtual void setTerrainEnabled(bool enabled) = 0;
    virtual bool isTerrainEnabled() const = 0;

    // Wireframe mode
    virtual void toggleTerrainWireframe() = 0;
    virtual bool isTerrainWireframeMode() const = 0;

    // Biome debug visualization
    virtual void toggleBiomeDebug() = 0;
    virtual bool isShowingBiomeDebug() const = 0;

    // Terrain statistics
    virtual uint32_t getTerrainNodeCount() const = 0;
    virtual float getTerrainHeightAt(float x, float z) const = 0;

    // Terrain system access (for detailed configuration)
    virtual TerrainSystem& getTerrainSystem() = 0;
    virtual const TerrainSystem& getTerrainSystem() const = 0;
};
