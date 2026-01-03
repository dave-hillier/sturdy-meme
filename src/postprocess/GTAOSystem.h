#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "InitContext.h"
#include "DescriptorManager.h"

/**
 * GTAOSystem - Ground-Truth Ambient Occlusion
 *
 * Implements horizon-based ambient occlusion in screen space.
 * Based on "Practical Real-Time Strategies for Accurate Indirect Occlusion"
 * (SIGGRAPH 2016) and XeGTAO (Intel).
 *
 * Features:
 * - Horizon-based occlusion (more accurate than SSAO)
 * - Hi-Z acceleration for long-range samples
 * - Temporal filtering for stability
 * - Bilateral spatial filter to preserve edges
 */

class GTAOSystem {
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
        DescriptorManager::Pool* descriptorPool;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Push constants for GTAO compute shader
    struct GTAOPushConstants {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invProjMatrix;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
        glm::vec4 aoParams;         // x = radius, y = falloff, z = intensity, w = bias
        glm::vec4 sampleParams;     // x = numSlices, y = numSteps, z = temporalOffset, w = thickness
        float nearPlane;
        float farPlane;
        float frameTime;
        float padding;
    };

    // Push constants for spatial filter
    struct FilterPushConstants {
        glm::vec2 resolution;
        glm::vec2 texelSize;
        float depthThreshold;
        float blurSharpness;
        float padding1;
        float padding2;
    };

    /**
     * Factory: Create and initialize GTAO system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GTAOSystem> create(const InitInfo& info);
    static std::unique_ptr<GTAOSystem> create(const InitContext& ctx);

    ~GTAOSystem();

    // Non-copyable, non-movable
    GTAOSystem(GTAOSystem&&) = delete;
    GTAOSystem& operator=(GTAOSystem&&) = delete;
    GTAOSystem(const GTAOSystem&) = delete;
    GTAOSystem& operator=(const GTAOSystem&) = delete;

    void resize(VkExtent2D newExtent);

    // Record GTAO compute pass - call after depth pass, uses Hi-Z if available
    // depthView: Scene depth buffer
    // hiZView: Hi-Z pyramid (optional, for acceleration)
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                       VkImageView depthView,
                       VkImageView hiZView,
                       VkSampler depthSampler,
                       const glm::mat4& view, const glm::mat4& proj,
                       float nearPlane, float farPlane,
                       float frameTime);

    // Get AO result texture for sampling in lighting shaders
    VkImageView getAOResultView() const { return aoResultView[currentBuffer]; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Configuration
    void setRadius(float r) { radius = r; }
    void setFalloff(float f) { falloff = f; }
    void setIntensity(float i) { intensity = i; }
    void setNumSlices(int n) { numSlices = n; }
    void setNumSteps(int n) { numSteps = n; }
    void setEnabled(bool enable) { enabled = enable; }
    void setSpatialFilterEnabled(bool enable) { spatialFilterEnabled = enable; }
    void setTemporalFilterEnabled(bool enable) { temporalFilterEnabled = enable; }

    float getRadius() const { return radius; }
    float getFalloff() const { return falloff; }
    float getIntensity() const { return intensity; }
    int getNumSlices() const { return numSlices; }
    int getNumSteps() const { return numSteps; }
    bool isEnabled() const { return enabled; }
    bool isSpatialFilterEnabled() const { return spatialFilterEnabled; }
    bool isTemporalFilterEnabled() const { return temporalFilterEnabled; }

private:
    GTAOSystem() = default;

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createAOBuffers();
    bool createComputePipeline();
    bool createFilterPipeline();
    bool createDescriptorSets();

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;
    const vk::raii::Device* raiiDevice_ = nullptr;

    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    bool enabled = true;
    bool spatialFilterEnabled = true;
    bool temporalFilterEnabled = true;

    // GTAO parameters
    float radius = 0.5f;           // World-space AO radius in meters
    float falloff = 2.0f;          // Distance falloff exponent
    float intensity = 1.0f;        // AO intensity multiplier
    int numSlices = 4;             // Number of angular slices
    int numSteps = 3;              // Steps per slice
    float thickness = 0.1f;        // Depth thickness for thin geometry
    float bias = 0.01f;            // Bias to prevent self-occlusion

    // Double-buffered AO result (R8_UNORM for efficiency)
    VkImage aoResult[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView aoResultView[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation aoAllocation[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    int currentBuffer = 0;

    // Intermediate buffer for spatial filter
    VkImage aoIntermediate = VK_NULL_HANDLE;
    VkImageView aoIntermediateView = VK_NULL_HANDLE;
    VmaAllocation aoIntermediateAllocation = VK_NULL_HANDLE;

    // Sampler
    std::optional<vk::raii::Sampler> sampler_;

    // Main GTAO compute pipeline
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::optional<vk::raii::PipelineLayout> computePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::vector<VkDescriptorSet> descriptorSets;

    // Spatial filter pipeline
    std::optional<vk::raii::Pipeline> filterPipeline_;
    std::optional<vk::raii::PipelineLayout> filterPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> filterDescriptorSetLayout_;
    std::vector<VkDescriptorSet> filterDescriptorSets;
};
