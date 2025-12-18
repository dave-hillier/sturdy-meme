#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include "VulkanRAII.h"

/*
 * FlowMapGenerator - Generates flow maps for water rendering
 *
 * Based on Far Cry 5's flow map system:
 * - Uses flood-fill algorithm guided by signed distance fields
 * - Flow follows terrain slopes toward water bodies
 * - Generates world-space flow atlas for streaming
 *
 * Flow map format (RGBA8):
 *   R: Flow direction X (-1 to 1, encoded as 0-1)
 *   G: Flow direction Z (-1 to 1, encoded as 0-1)
 *   B: Flow speed (0 to 1)
 *   A: Signed distance to shore (normalized)
 */

class FlowMapGenerator {
public:
    struct Config {
        uint32_t resolution = 512;       // Flow map resolution
        float worldSize = 16384.0f;      // World size in meters
        float waterLevel = 0.0f;         // Water surface Y level
        float maxFlowSpeed = 1.0f;       // Maximum flow speed
        float slopeInfluence = 1.0f;     // How much terrain slope affects flow
        float shoreDistance = 50.0f;     // Max distance for shore SDF (meters)
    };

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue queue;
    };

    /**
     * Factory: Create and initialize FlowMapGenerator.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<FlowMapGenerator> create(const InitInfo& info);

    ~FlowMapGenerator();

    // Non-copyable, non-movable
    FlowMapGenerator(const FlowMapGenerator&) = delete;
    FlowMapGenerator& operator=(const FlowMapGenerator&) = delete;
    FlowMapGenerator(FlowMapGenerator&&) = delete;
    FlowMapGenerator& operator=(FlowMapGenerator&&) = delete;

    // Generate flow map from terrain heightmap data
    // heightData: raw heightmap values (normalized 0-1)
    // heightScale: world-space height multiplier
    bool generateFromTerrain(const std::vector<float>& heightData,
                             uint32_t heightmapSize,
                             float heightScale,
                             const Config& config);

    // Generate a simple radial flow map (for testing/lakes)
    bool generateRadialFlow(const Config& config, const glm::vec2& center);

    // Generate flow from terrain slopes (rivers flow downhill)
    bool generateSlopeBasedFlow(const std::vector<float>& heightData,
                                uint32_t heightmapSize,
                                float heightScale,
                                const Config& config);

    // Access the generated flow map
    VkImageView getFlowMapView() const { return flowMapView; }
    VkSampler getFlowMapSampler() const { return flowMapSampler.get(); }
    VkImage getFlowMapImage() const { return flowMapImage; }

    // Get flow map data for CPU-side queries
    const std::vector<glm::vec4>& getFlowData() const { return flowData; }

    // Sample flow at world position (CPU-side)
    glm::vec4 sampleFlow(const glm::vec2& worldPos) const;

    // Check if flow map is valid
    bool isValid() const { return flowMapImage != VK_NULL_HANDLE; }

    // Get resolution
    uint32_t getResolution() const { return currentResolution; }

private:
    FlowMapGenerator() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    // Create GPU resources
    bool createImage(uint32_t resolution);
    bool createSampler();
    void uploadToGPU();

    // Flow generation algorithms
    void computeSignedDistanceField(const std::vector<bool>& waterMask);
    void computeFlowDirections(const std::vector<float>& heightData,
                               uint32_t heightmapSize,
                               float heightScale);
    void computeFlowSpeed();
    void normalizeAndEncode();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    VkImage flowMapImage = VK_NULL_HANDLE;
    VmaAllocation flowMapAllocation = VK_NULL_HANDLE;
    VkImageView flowMapView = VK_NULL_HANDLE;
    ManagedSampler flowMapSampler;

    // CPU-side flow data
    std::vector<glm::vec4> flowData;  // RGBA: flowX, flowZ, speed, shoreDist
    std::vector<float> signedDistanceField;

    // Current configuration
    uint32_t currentResolution = 0;
    float currentWorldSize = 0.0f;
    float currentWaterLevel = 0.0f;
};
