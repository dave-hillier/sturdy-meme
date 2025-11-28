#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Cloud Temporal Reprojection System (Phase 4.2.7)
// Implements temporal stability for volumetric clouds using:
// - Double-buffered cloud render targets (current and history)
// - Motion-based reprojection for camera movement
// - Wind-based reprojection for cloud motion
// - Adaptive blending with rejection for disoccluded regions

// Cloud temporal uniforms (must match GLSL layout)
struct CloudTemporalUniforms {
    glm::mat4 invViewProj;        // Current frame inverse view-projection
    glm::mat4 prevViewProj;       // Previous frame view-projection
    glm::vec4 cameraPosition;     // xyz = camera pos, w = camera altitude
    glm::vec4 sunDirection;       // xyz = sun dir, w = sun intensity
    glm::vec4 sunColor;           // rgb = sun color, w = unused
    glm::vec4 moonDirection;      // xyz = moon dir, w = moon intensity
    glm::vec4 moonColor;          // rgb = moon color, a = moon phase
    glm::vec4 windParams;         // xy = wind direction, z = wind speed, w = time
    glm::vec4 cloudParams;        // x = coverage, y = density, z = blend factor, w = frame index
    glm::vec4 atmosphereParams;   // x = planet radius, y = atmosphere radius, z = cloud bottom, w = cloud top
};

class CloudTemporalSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        // LUT views for atmosphere sampling
        VkImageView transmittanceLUTView;
        VkImageView multiScatterLUTView;
        VkSampler lutSampler;
    };

    // Cloud render target dimensions (paraboloid projection)
    // Using 512x512 for good quality while maintaining performance
    static constexpr uint32_t CLOUD_MAP_SIZE = 512;

    CloudTemporalSystem() = default;
    ~CloudTemporalSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Record cloud rendering with temporal reprojection
    // Call before sky rendering each frame
    void recordCloudUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& view, const glm::mat4& proj,
                          const glm::vec3& cameraPos,
                          const glm::vec3& sunDir, float sunIntensity,
                          const glm::vec3& sunColor,
                          const glm::vec3& moonDir, float moonIntensity,
                          const glm::vec3& moonColor, float moonPhase,
                          const glm::vec2& windDir, float windSpeed, float windTime);

    // Get the current cloud map for sky shader sampling
    VkImageView getCloudMapView() const { return cloudMapViews[currentWriteIndex]; }
    VkSampler getCloudMapSampler() const { return cloudSampler; }

    // Temporal blend parameters
    void setTemporalBlend(float blend) { temporalBlend = blend; }
    float getTemporalBlend() const { return temporalBlend; }

    // Cloud parameters
    void setCoverage(float c) { coverage = c; }
    float getCoverage() const { return coverage; }
    void setDensity(float d) { density = d; }
    float getDensity() const { return density; }

    // Enable/disable temporal reprojection
    void setTemporalEnabled(bool e) { temporalEnabled = e; }
    bool isTemporalEnabled() const { return temporalEnabled; }

private:
    bool createCloudMaps();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createComputePipeline();

    void destroyResources();
    void swapBuffers();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // External LUT resources (not owned)
    VkImageView transmittanceLUTView = VK_NULL_HANDLE;
    VkImageView multiScatterLUTView = VK_NULL_HANDLE;
    VkSampler lutSampler = VK_NULL_HANDLE;

    // Double-buffered cloud maps for temporal reprojection
    // Format: RGBA16F - RGB = in-scattered light, A = transmittance
    static constexpr uint32_t NUM_CLOUD_BUFFERS = 2;
    VkImage cloudMaps[NUM_CLOUD_BUFFERS] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation cloudMapAllocations[NUM_CLOUD_BUFFERS] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView cloudMapViews[NUM_CLOUD_BUFFERS] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Index management for ping-pong buffering
    uint32_t currentWriteIndex = 0;  // Index to write current frame
    uint32_t currentReadIndex = 1;   // Index to read history (previous frame)

    // Cloud sampler (bilinear filtering)
    VkSampler cloudSampler = VK_NULL_HANDLE;

    // Compute pipeline
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    // One descriptor set per frame in flight (for uniform buffer updates)
    std::vector<VkDescriptorSet> descriptorSets;

    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Previous frame's view-projection for reprojection
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    uint32_t frameCounter = 0;

    // Temporal parameters
    float temporalBlend = 0.9f;      // History blend factor (0.9 = stable, 0.5 = responsive)
    bool temporalEnabled = true;

    // Cloud parameters
    float coverage = 0.5f;
    float density = 0.3f;
};
