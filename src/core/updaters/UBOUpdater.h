#pragma once

// ============================================================================
// UBOUpdater.h - Orchestrates per-frame UBO updates
// ============================================================================
//
// Moves the UBO update logic from Renderer::updateUniformBuffer() into a
// dedicated updater class, following the existing updater pattern.
//
// Responsibilities:
// - Calculate lighting parameters via UBOBuilder
// - Update cascade matrices via ShadowSystem
// - Build and upload all UBO data (main UBO, snow, cloud shadow, lights)
// - Update post-process state (sun screen position, HDR enabled)
//

#include <glm/glm.hpp>

class Camera;
class RendererSystems;

namespace ecs {
class World;
}

class UBOUpdater {
public:
    // Configuration for UBO updates
    struct Config {
        bool showCascadeDebug = false;
        bool useVolumetricSnow = true;
        bool showSnowDepthDebug = false;
        bool shadowsEnabled = true;
        bool hdrEnabled = true;
        float maxSnowHeight = 0.3f;
        float lightCullRadius = 100.0f;
        ecs::World* ecsWorld = nullptr;  // Optional: ECS world for light updates
        float deltaTime = 0.016f;         // For flicker animation
    };

    // Output data from UBO update (for state that needs to be tracked)
    struct Result {
        float sunIntensity = 1.0f;
    };

    /**
     * Update all UBOs for the current frame
     *
     * @param systems Reference to renderer systems
     * @param frameIndex Current frame index (for buffer selection)
     * @param camera Camera for view/projection matrices
     * @param config UBO configuration
     * @return Result containing computed values needed by caller
     */
    static Result update(
        RendererSystems& systems,
        uint32_t frameIndex,
        const Camera& camera,
        const Config& config);
};
