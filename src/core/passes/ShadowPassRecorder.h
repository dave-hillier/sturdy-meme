#pragma once

// ============================================================================
// ShadowPassRecorder.h - Shadow pass recording logic
// ============================================================================
//
// Encapsulates all shadow pass recording that was previously in Renderer.
// This class handles:
// - Building callbacks for terrain, grass, trees, and skinned mesh shadows
// - Collecting shadow-casting objects
// - Recording the shadow pass via ShadowSystem
//

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

#include "ShadowPassResources.h"

struct PerformanceToggles;
struct Renderable;

class ShadowPassRecorder {
public:
    // Configuration for shadow recording
    struct Config {
        bool terrainEnabled = true;
        PerformanceToggles* perfToggles = nullptr;
    };

    // Construct with focused resources (explicit dependencies)
    explicit ShadowPassRecorder(const ShadowPassResources& resources);

    // Set configuration (can be updated per-frame if needed)
    void setConfig(const Config& config) { config_ = config; }

    // Record the complete shadow pass
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPosition);

private:
    ShadowPassResources resources_;
    Config config_;
};
