#pragma once

#include "interfaces/IDebugControl.h"

class DebugLineSystem;
class HiZSystem;
class RendererSystems;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

/**
 * DebugControlSubsystem - Implements IDebugControl
 * Coordinates debug visualization systems.
 */
class DebugControlSubsystem : public IDebugControl {
public:
    DebugControlSubsystem(DebugLineSystem& debugLine, HiZSystem& hiZ, RendererSystems& systems)
        : debugLine_(debugLine)
        , hiZ_(hiZ)
        , systems_(systems)
    {}

    void toggleCascadeDebug() override { showCascadeDebug_ = !showCascadeDebug_; }
    bool isShowingCascadeDebug() const override { return showCascadeDebug_; }
    void toggleSnowDepthDebug() override { showSnowDepthDebug_ = !showSnowDepthDebug_; }
    bool isShowingSnowDepthDebug() const override { return showSnowDepthDebug_; }

    void setPhysicsDebugEnabled(bool enabled) override { physicsDebugEnabled_ = enabled; }
    bool isPhysicsDebugEnabled() const override { return physicsDebugEnabled_; }

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* getPhysicsDebugRenderer() override;
    const PhysicsDebugRenderer* getPhysicsDebugRenderer() const override;
#endif

    DebugLineSystem& getDebugLineSystem() override;
    const DebugLineSystem& getDebugLineSystem() const override;

    void setHiZCullingEnabled(bool enabled) override;
    bool isHiZCullingEnabled() const override;
    CullingStats getHiZCullingStats() const override;

    // Access to local state for Renderer
    bool& showCascadeDebug() { return showCascadeDebug_; }
    bool& showSnowDepthDebug() { return showSnowDepthDebug_; }
    bool& physicsDebugEnabled() { return physicsDebugEnabled_; }

private:
    DebugLineSystem& debugLine_;
    HiZSystem& hiZ_;
    RendererSystems& systems_;
    bool showCascadeDebug_ = false;
    bool showSnowDepthDebug_ = false;
    bool physicsDebugEnabled_ = false;
};
