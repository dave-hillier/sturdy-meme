#include "PhysicsDebugRenderer.h"

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Body/Body.h>
#include <SDL3/SDL_log.h>

// Custom body filter that respects our options
class OptionsBodyFilter : public JPH::BodyDrawFilter {
public:
    OptionsBodyFilter(const PhysicsDebugRenderer::Options& opts) : options(opts) {}

    virtual bool ShouldDraw(const JPH::Body& inBody) const override {
        JPH::EMotionType motionType = inBody.GetMotionType();
        switch (motionType) {
            case JPH::EMotionType::Static:
                return options.drawStaticBodies;
            case JPH::EMotionType::Dynamic:
                return options.drawDynamicBodies;
            case JPH::EMotionType::Kinematic:
                return options.drawKinematicBodies;
            default:
                return true;
        }
    }

private:
    const PhysicsDebugRenderer::Options& options;
};

PhysicsDebugRenderer::PhysicsDebugRenderer() {
    // Don't initialize here - Jolt allocator not yet registered
    // Call init() after Jolt is initialized
}

void PhysicsDebugRenderer::init() {
    if (initialized) return;

    // Initialize the base class - this sets up internal geometry
    Initialize();

    // Register as singleton instance for Jolt's body drawing
    JPH::DebugRenderer::sInstance = this;

    initialized = true;
    SDL_Log("PhysicsDebugRenderer: Initialized");
}

void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
    std::lock_guard<std::mutex> lock(mutex);
    lines.push_back({
        toGLM(inFrom),
        toGLM(inTo),
        toGLM(inColor)
    });
}

void PhysicsDebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                                         JPH::ColorArg inColor, ECastShadow /*inCastShadow*/) {
    std::lock_guard<std::mutex> lock(mutex);
    triangles.push_back({
        toGLM(inV1),
        toGLM(inV2),
        toGLM(inV3),
        toGLM(inColor)
    });
}

void PhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg /*inPosition*/, const std::string_view& /*inString*/,
                                       JPH::ColorArg /*inColor*/, float /*inHeight*/) {
    // Text rendering not implemented - would require font system
}

void PhysicsDebugRenderer::beginFrame(const glm::vec3& cameraPos) {
    clear();
    SetCameraPos(JPH::RVec3(cameraPos.x, cameraPos.y, cameraPos.z));
}

void PhysicsDebugRenderer::endFrame() {
    // Call NextFrame to clean up cached geometry
    NextFrame();
}

void PhysicsDebugRenderer::drawBodies(JPH::PhysicsSystem& physicsSystem) {
    // Build draw settings from our options
    JPH::BodyManager::DrawSettings drawSettings;
    drawSettings.mDrawShape = options.drawShapes;
    drawSettings.mDrawShapeWireframe = options.drawShapeWireframe;
    drawSettings.mDrawBoundingBox = options.drawBoundingBox;
    drawSettings.mDrawCenterOfMassTransform = options.drawCenterOfMassTransform;
    drawSettings.mDrawWorldTransform = options.drawWorldTransform;
    drawSettings.mDrawVelocity = options.drawVelocity;
    drawSettings.mDrawMassAndInertia = options.drawMassAndInertia;
    drawSettings.mDrawSleepStats = options.drawSleepStats;

    // Create body filter to respect our options
    OptionsBodyFilter bodyFilter(options);

    // Draw bodies with filter
    physicsSystem.DrawBodies(drawSettings, this, &bodyFilter);

    // Draw constraints if enabled
    if (options.drawConstraints) {
        physicsSystem.DrawConstraints(this);
    }

    if (options.drawConstraintLimits) {
        physicsSystem.DrawConstraintLimits(this);
    }

    // Note: DrawConstraintReferenceFrames may not exist in all versions
}

void PhysicsDebugRenderer::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    lines.clear();
    triangles.clear();
}

glm::vec4 PhysicsDebugRenderer::toGLM(JPH::ColorArg color) const {
    return glm::vec4(
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f
    );
}

glm::vec3 PhysicsDebugRenderer::toGLM(JPH::RVec3Arg v) const {
    return glm::vec3(
        static_cast<float>(v.GetX()),
        static_cast<float>(v.GetY()),
        static_cast<float>(v.GetZ())
    );
}

#endif // JPH_DEBUG_RENDERER
