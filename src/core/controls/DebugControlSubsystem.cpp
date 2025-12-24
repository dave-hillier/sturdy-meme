#include "DebugControlSubsystem.h"
#include "DebugLineSystem.h"
#include "HiZSystem.h"
#include "RendererSystems.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#ifdef JPH_DEBUG_RENDERER
PhysicsDebugRenderer* DebugControlSubsystem::getPhysicsDebugRenderer() {
    return systems_.physicsDebugRenderer();
}

const PhysicsDebugRenderer* DebugControlSubsystem::getPhysicsDebugRenderer() const {
    return systems_.physicsDebugRenderer();
}
#endif

DebugLineSystem& DebugControlSubsystem::getDebugLineSystem() {
    return debugLine_;
}

const DebugLineSystem& DebugControlSubsystem::getDebugLineSystem() const {
    return debugLine_;
}

void DebugControlSubsystem::setHiZCullingEnabled(bool enabled) {
    hiZ_.setHiZEnabled(enabled);
}

bool DebugControlSubsystem::isHiZCullingEnabled() const {
    return hiZ_.isHiZEnabled();
}

IDebugControl::CullingStats DebugControlSubsystem::getHiZCullingStats() const {
    auto stats = hiZ_.getStats();
    return CullingStats{stats.totalObjects, stats.visibleObjects, stats.frustumCulled, stats.occlusionCulled};
}
