#pragma once

#include <Jolt/Jolt.h>

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Renderer/DebugRendererSimple.h>
#include <glm/glm.hpp>
#include <vector>
#include <mutex>

// Forward declarations
namespace JPH {
    class PhysicsSystem;
    class BodyManager;
}

// Simple debug line for rendering
struct DebugLine {
    glm::vec3 start;
    glm::vec3 end;
    glm::vec4 color;
};

// Simple debug triangle for rendering
struct DebugTriangle {
    glm::vec3 v0, v1, v2;
    glm::vec4 color;
};

// Jolt debug renderer implementation that collects primitives for Vulkan rendering
class PhysicsDebugRenderer : public JPH::DebugRendererSimple {
public:
    PhysicsDebugRenderer();
    virtual ~PhysicsDebugRenderer() override = default;

    // Initialize after Jolt is initialized (must be called before use)
    void init();

    // DebugRenderer interface
    virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    virtual void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                              JPH::ColorArg inColor, ECastShadow inCastShadow) override;
    virtual void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString,
                            JPH::ColorArg inColor, float inHeight) override;

    // Frame management
    void beginFrame(const glm::vec3& cameraPos);
    void endFrame();

    // Draw all physics bodies
    void drawBodies(JPH::PhysicsSystem& physicsSystem);

    // Access collected primitives for rendering
    const std::vector<DebugLine>& getLines() const { return lines; }
    const std::vector<DebugTriangle>& getTriangles() const { return triangles; }

    // Clear all primitives
    void clear();

    // Visualization options
    struct Options {
        bool drawShapes = true;
        bool drawShapeWireframe = true;
        bool drawBoundingBox = false;
        bool drawCenterOfMassTransform = false;
        bool drawWorldTransform = false;
        bool drawVelocity = false;
        bool drawMassAndInertia = false;
        bool drawSleepStats = false;
        bool drawConstraints = false;
        bool drawConstraintLimits = false;
        bool drawConstraintReferenceFrame = false;
        bool drawContactPoint = false;
        bool drawContactNormal = false;

        // Body type filters (static disabled by default - terrain heightfields are huge!)
        bool drawStaticBodies = false;
        bool drawDynamicBodies = true;
        bool drawKinematicBodies = true;
        bool drawCharacter = true;
    };

    Options& getOptions() { return options; }
    const Options& getOptions() const { return options; }

private:
    glm::vec4 toGLM(JPH::ColorArg color) const;
    glm::vec3 toGLM(JPH::RVec3Arg v) const;

    std::vector<DebugLine> lines;
    std::vector<DebugTriangle> triangles;
    Options options;
    std::mutex mutex;
    bool initialized = false;
};

#endif // JPH_DEBUG_RENDERER
