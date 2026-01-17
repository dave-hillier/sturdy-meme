#pragma once

// ============================================================================
// HDRPassRecorder.h - HDR render pass recording logic
// ============================================================================
//
// Encapsulates all HDR pass recording that was previously in Renderer.
// This class handles:
// - Beginning/ending the HDR render pass
// - Drawing sky, terrain, scene objects, grass, water, weather, debug lines
// - Secondary command buffer recording for parallel execution
//

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>

#include "HDRPassResources.h"

class RendererSystems;

class HDRPassRecorder {
public:
    // Configuration for HDR recording
    // These are pointers to resources owned by Renderer
    struct Config {
        bool terrainEnabled = true;
        const vk::Pipeline* sceneObjectsPipeline = nullptr;       // Legacy graphics pipeline
        const vk::PipelineLayout* pipelineLayout = nullptr;       // Legacy pipeline layout
        glm::mat4* lastViewProj = nullptr;                        // For debug line rendering
    };

    // Construct with focused resources (preferred - reduced coupling)
    explicit HDRPassRecorder(const HDRPassResources& resources);

    // Construct with RendererSystems (convenience, collects resources internally)
    explicit HDRPassRecorder(RendererSystems& systems);

    // Set configuration (must be called before recording)
    void setConfig(const Config& config) { config_ = config; }

    // Record the complete HDR pass (sequential path)
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time);

    // Record HDR pass with pre-recorded secondary command buffers (parallel path)
    void recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                               const std::vector<vk::CommandBuffer>& secondaries);

    // Record a specific slot to a secondary command buffer
    // Slot 0: Sky + Terrain + Catmull-Clark
    // Slot 1: Scene Objects + Skinned Character
    // Slot 2: Grass + Water + Leaves + Weather + Debug lines
    void recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot);

private:
    // Helper to record scene objects using the legacy pipeline
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex);

    // Helper to record debug lines with viewport/scissor setup
    void recordDebugLines(VkCommandBuffer cmd);

    HDRPassResources resources_;
    Config config_;
};
