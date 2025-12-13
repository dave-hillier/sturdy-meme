#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

/**
 * SSRSystem - Phase 10: Screen-Space Reflections
 *
 * Implements hierarchical ray marching in screen space to generate reflections.
 * For water surfaces, provides dynamic reflections of the scene that update per frame.
 *
 * Based on:
 * - "Stochastic Screen-Space Reflections" (SIGGRAPH 2015)
 * - Far Cry 5 GDC 2018 water rendering
 *
 * Features:
 * - Hierarchical depth buffer tracing for efficiency
 * - Fresnel-weighted reflection intensity
 * - Fallback to environment map where SSR fails
 * - Temporal filtering for stability
 */

class SSRSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
    };

    // Push constants for SSR compute shader
    struct SSRPushConstants {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::vec4 cameraPos;        // xyz = position, w = unused
        glm::vec4 screenParams;     // xy = resolution, z = 1/width, w = 1/height
        float maxDistance;          // Maximum ray march distance
        float thickness;            // Depth thickness for hit detection
        float stride;               // Initial step size
        int maxSteps;               // Maximum ray march steps
        float fadeStart;            // Start fading at this distance
        float fadeEnd;              // End fade at this distance
        float temporalBlend;        // Blend with previous frame
        float padding;              // Alignment
    };

    // Push constants for blur compute shader
    struct BlurPushConstants {
        glm::vec2 resolution;       // SSR texture resolution
        glm::vec2 texelSize;        // 1.0 / resolution
        float depthThreshold;       // Depth difference threshold for bilateral weight
        float blurRadius;           // Blur kernel radius in pixels
        float padding1;
        float padding2;
    };

    SSRSystem() = default;
    ~SSRSystem() = default;

    bool init(const InitInfo& info);
    void destroy();
    void resize(VkExtent2D newExtent);

    // Record SSR compute pass - must be called after scene rendering, before water
    // hdrColorView: Scene color buffer to reflect
    // hdrDepthView: Scene depth buffer for ray marching
    // normalView: Optional normal buffer for better hit detection
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                       VkImageView hdrColorView, VkImageView hdrDepthView,
                       const glm::mat4& view, const glm::mat4& proj,
                       const glm::vec3& cameraPos);

    // Get SSR result texture for sampling in water shader
    VkImageView getSSRResultView() const { return ssrResultView[currentBuffer]; }
    VkSampler getSampler() const { return sampler; }

    // Configuration
    void setMaxDistance(float dist) { maxDistance = dist; }
    void setThickness(float t) { thickness = t; }
    void setMaxSteps(int steps) { maxSteps = steps; }
    void setFadeDistance(float start, float end) { fadeStart = start; fadeEnd = end; }
    void setEnabled(bool enable) { enabled = enable; }
    void setBlurEnabled(bool enable) { blurEnabled = enable; }
    void setBlurRadius(float radius) { blurRadius = radius; }
    void setBlurDepthThreshold(float threshold) { blurDepthThreshold = threshold; }

    float getMaxDistance() const { return maxDistance; }
    float getThickness() const { return thickness; }
    int getMaxSteps() const { return maxSteps; }
    bool isEnabled() const { return enabled; }
    bool isBlurEnabled() const { return blurEnabled; }
    float getBlurRadius() const { return blurRadius; }

private:
    bool createSSRBuffers();
    bool createComputePipeline();
    bool createBlurPipeline();
    bool createDescriptorSets();

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;

    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    bool enabled = true;
    bool blurEnabled = false;        // Disabled - causes screen corruption, needs barrier/sync debugging

    // SSR parameters
    float maxDistance = 300.0f;     // Max reflection distance (world units)
    float thickness = 1.0f;         // Depth comparison thickness (more forgiving)
    float stride = 1.5f;            // Ray march step size in pixels (finer tracing)
    int maxSteps = 128;             // Max ray march iterations (longer traces)
    float fadeStart = 0.7f;         // Start fading reflections at 70% of max distance
    float fadeEnd = 1.0f;           // Fully fade at 100% of max distance
    float temporalBlend = 0.9f;     // Temporal stability blend factor

    // Blur parameters
    float blurRadius = 1.5f;        // Blur kernel radius in pixels (conservative)
    float blurDepthThreshold = 0.005f; // Depth threshold for bilateral weight (tighter)

    // Double-buffered SSR result (ping-pong for temporal filtering)
    // RGBA16F format - rgb = reflection color, a = confidence
    VkImage ssrResult[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView ssrResultView[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation ssrAllocation[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    int currentBuffer = 0;

    // Sampler
    VkSampler sampler = VK_NULL_HANDLE;

    // Main SSR compute pipeline
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Blur compute pipeline
    VkPipeline blurPipeline = VK_NULL_HANDLE;
    VkPipelineLayout blurPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> blurDescriptorSets;

    // Intermediate buffer for blur (SSR writes here, blur reads and writes to final)
    VkImage ssrIntermediate = VK_NULL_HANDLE;
    VkImageView ssrIntermediateView = VK_NULL_HANDLE;
    VmaAllocation ssrIntermediateAllocation = VK_NULL_HANDLE;

    // Store depth view for blur pass
    VkImageView cachedDepthView = VK_NULL_HANDLE;
};
