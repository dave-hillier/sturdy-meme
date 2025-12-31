#pragma once

#include <cstdint>

class DebugLineSystem;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

/**
 * Interface for debug visualization controls.
 * Used by GuiDebugTab to control debug overlays and occlusion culling.
 */
class IDebugControl {
public:
    virtual ~IDebugControl() = default;

    // Shadow cascade debug visualization
    virtual void toggleCascadeDebug() = 0;
    virtual bool isShowingCascadeDebug() const = 0;

    // Snow depth debug visualization
    virtual void toggleSnowDepthDebug() = 0;
    virtual bool isShowingSnowDepthDebug() const = 0;

    // Physics debug rendering
    virtual void setPhysicsDebugEnabled(bool enabled) = 0;
    virtual bool isPhysicsDebugEnabled() const = 0;

#ifdef JPH_DEBUG_RENDERER
    virtual PhysicsDebugRenderer* getPhysicsDebugRenderer() = 0;
    virtual const PhysicsDebugRenderer* getPhysicsDebugRenderer() const = 0;
#endif

    // Debug line system
    virtual DebugLineSystem& getDebugLineSystem() = 0;
    virtual const DebugLineSystem& getDebugLineSystem() const = 0;

    // Road/river visualization
    virtual void setRoadRiverVisualizationEnabled(bool enabled) = 0;
    virtual bool isRoadRiverVisualizationEnabled() const = 0;

    // Hi-Z occlusion culling
    virtual void setHiZCullingEnabled(bool enabled) = 0;
    virtual bool isHiZCullingEnabled() const = 0;

    // Culling statistics
    struct CullingStats {
        uint32_t totalObjects;
        uint32_t visibleObjects;
        uint32_t frustumCulled;
        uint32_t occlusionCulled;
    };
    virtual CullingStats getHiZCullingStats() const = 0;
};
