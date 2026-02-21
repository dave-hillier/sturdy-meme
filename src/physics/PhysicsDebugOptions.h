#pragma once

// Physics debug visualization options
// Stored independently from the renderer so they persist across enable/disable
// and can be configured before the renderer is created.
struct PhysicsDebugOptions {
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
