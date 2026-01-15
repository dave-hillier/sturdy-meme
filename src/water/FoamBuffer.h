#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "VmaResources.h"

/**
 * FoamBuffer - Phase 14 & 16: Temporal Foam Persistence + Wake System
 *
 * Implements persistent foam that fades over time:
 * - Foam render target that persists between frames
 * - Progressive blur to simulate foam dissipation
 * - Advection using flow map
 * - Sharp foam at wave crests, gradual fade
 *
 * Phase 16 additions:
 * - Wake injection for moving objects
 * - V-shaped bow wave patterns
 * - Kelvin wake angle simulation
 *
 * Based on Sea of Thieves GDC 2018 talk.
 */

// Maximum wake sources per frame
constexpr uint32_t MAX_WAKE_SOURCES = 16;

// Wake source data - represents a moving object creating a wake
struct WakeSource {
    glm::vec2 position;     // World position XZ
    glm::vec2 velocity;     // Velocity direction and speed
    float radius;           // Object radius (affects wake width)
    float intensity;        // Wake foam intensity (0-1)
    float wakeAngle;        // Kelvin wake angle (typically 19.47 degrees)
    float padding;          // Alignment padding
};

class FoamBuffer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        uint32_t resolution = 512;      // Foam buffer resolution
        float worldSize = 16384.0f;     // World size covered by foam buffer
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Push constants for compute shader
    struct FoamPushConstants {
        glm::vec4 worldExtent;    // xy = center, zw = size
        float deltaTime;
        float blurStrength;       // How much to blur each frame
        float decayRate;          // How fast foam fades
        float injectionStrength;  // Strength of new foam injection
        uint32_t wakeCount;       // Number of active wake sources
        float padding[3];         // Alignment
    };

    // Wake uniform buffer data (matches shader layout)
    struct WakeUniformData {
        WakeSource sources[MAX_WAKE_SOURCES];
    };

    /**
     * Factory: Create and initialize FoamBuffer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<FoamBuffer> create(const InitInfo& info);

    ~FoamBuffer();

    // Non-copyable, non-movable
    FoamBuffer(const FoamBuffer&) = delete;
    FoamBuffer& operator=(const FoamBuffer&) = delete;
    FoamBuffer(FoamBuffer&&) = delete;
    FoamBuffer& operator=(FoamBuffer&&) = delete;

    // Record compute shader dispatch for blur/decay
    // Call this each frame before water rendering
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime,
                       VkImageView flowMapView, VkSampler flowMapSampler);

    // Get foam buffer for sampling in water shader
    VkImageView getFoamBufferView() const { return foamBufferView_[currentBuffer] ? **foamBufferView_[currentBuffer] : VK_NULL_HANDLE; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Configuration
    void setWorldExtent(const glm::vec2& center, const glm::vec2& size);
    void setBlurStrength(float strength) { blurStrength = strength; }
    void setDecayRate(float rate) { decayRate = rate; }
    void setInjectionStrength(float strength) { injectionStrength = strength; }

    float getBlurStrength() const { return blurStrength; }
    float getDecayRate() const { return decayRate; }
    float getInjectionStrength() const { return injectionStrength; }

    // Clear foam buffer
    void clear(VkCommandBuffer cmd);

    // Phase 16: Wake System
    // Add a wake source for this frame (cleared after compute pass)
    void addWakeSource(const glm::vec2& position, const glm::vec2& velocity,
                       float radius, float intensity = 1.0f);

    // Add a simple wake at position (velocity inferred from position change)
    void addWake(const glm::vec2& position, float radius, float intensity = 1.0f);

    // Clear all wake sources (called automatically after compute)
    void clearWakeSources();

    // Get current wake count
    uint32_t getWakeCount() const { return wakeCount; }

private:
    FoamBuffer() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createFoamBuffers();
    bool createComputePipeline();
    bool createDescriptorSets();

    // Device handles
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Configuration
    uint32_t framesInFlight = 0;
    uint32_t resolution = 512;
    float worldSize = 16384.0f;
    glm::vec2 worldCenter = glm::vec2(0.0f);

    // Foam parameters
    float blurStrength = 0.02f;      // Subtle blur per frame
    float decayRate = 0.5f;          // Foam decay per second
    float injectionStrength = 1.0f;  // New foam injection strength

    // Double-buffered foam maps (ping-pong for blur)
    // R16F format - single channel foam intensity
    VkImage foamBuffer[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::optional<vk::raii::ImageView> foamBufferView_[2];
    VmaAllocation foamAllocation[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    int currentBuffer = 0;  // Which buffer to read from

    // Sampler (RAII-managed)
    std::optional<vk::raii::Sampler> sampler_;

    // Compute pipeline (RAII-managed)
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::optional<vk::raii::PipelineLayout> computePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets;

    // Phase 16: Wake system
    WakeUniformData wakeData{};
    uint32_t wakeCount = 0;
    std::vector<ManagedBuffer> wakeUniformBuffers_;
    std::vector<void*> wakeUniformMapped;

    bool createWakeBuffers();
};
