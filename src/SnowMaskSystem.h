#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "BufferUtils.h"
#include "SystemLifecycleHelper.h"
#include "EnvironmentSettings.h"

// Uniforms for snow accumulation compute shader (256 bytes, aligned)
struct SnowMaskUniforms {
    glm::vec4 maskRegion;           // xy = world origin, z = size, w = texel size
    glm::vec4 accumulationParams;   // x = accumulation rate, y = melt rate, z = delta time, w = is snowing (0/1)
    glm::vec4 snowParams;           // x = snow amount, y = weather intensity, z = unused, w = unused
    float padding[4];               // Align to 64 bytes
};

// Interaction source for snow clearing (footprints, vehicles, etc.)
struct SnowInteractionSource {
    glm::vec4 positionAndRadius;    // xyz = world position, w = radius
    glm::vec4 strengthAndShape;     // x = clearing strength (0-1), y = shape (0=circle, 1=ellipse), zw = ellipse axes
};

class SnowMaskSystem {
public:
    using InitInfo = SystemLifecycleHelper::InitInfo;

    SnowMaskSystem() = default;
    ~SnowMaskSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update uniforms for compute shader
    void updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing, float weatherIntensity,
                        const EnvironmentSettings& settings);

    // Add interaction source (footprint, vehicle track, etc.)
    void addInteraction(const glm::vec3& position, float radius, float strength);
    void clearInteractions();

    // Record compute dispatch for snow accumulation update
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Accessors for other systems to bind the snow mask texture
    VkImageView getSnowMaskView() const { return snowMaskView; }
    VkSampler getSnowMaskSampler() const { return snowMaskSampler; }

    // Get mask parameters for shader uniforms
    glm::vec2 getMaskOrigin() const { return maskOrigin; }
    float getMaskSize() const { return maskSize; }

    // Set mask center (follows camera/player)
    void setMaskCenter(const glm::vec3& worldPos);

    // Environment settings
    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

private:
    bool createBuffers();
    bool createSnowMaskTexture();
    bool createComputeDescriptorSetLayout();
    bool createComputePipeline();
    bool createDescriptorSets();
    void destroyBuffers(VmaAllocator allocator);

    VkDevice getDevice() const { return lifecycle.getDevice(); }
    VmaAllocator getAllocator() const { return lifecycle.getAllocator(); }
    DescriptorManager::Pool* getDescriptorPool() const { return lifecycle.getDescriptorPool(); }
    const std::string& getShaderPath() const { return lifecycle.getShaderPath(); }
    uint32_t getFramesInFlight() const { return lifecycle.getFramesInFlight(); }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return lifecycle.getComputePipeline(); }

    SystemLifecycleHelper lifecycle;

    // Snow mask texture (world-space coverage)
    static constexpr uint32_t SNOW_MASK_SIZE = 512;  // 512x512 texels
    static constexpr uint32_t MAX_INTERACTIONS = 32; // Max interaction sources per frame

    VkImage snowMaskImage = VK_NULL_HANDLE;
    VmaAllocation snowMaskAllocation = VK_NULL_HANDLE;
    VkImageView snowMaskView = VK_NULL_HANDLE;
    VkSampler snowMaskSampler = VK_NULL_HANDLE;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Interaction sources buffer (per frame)
    BufferUtils::PerFrameBufferSet interactionBuffers;

    // Descriptor sets (per frame)
    std::vector<VkDescriptorSet> computeDescriptorSets;

    // Mask world-space parameters
    glm::vec2 maskOrigin = glm::vec2(0.0f);  // World XZ origin of mask
    float maskSize = 500.0f;                  // World units covered by mask

    // Current frame interaction sources
    std::vector<SnowInteractionSource> currentInteractions;

    // Environment settings reference
    const EnvironmentSettings* environmentSettings = nullptr;

    static constexpr uint32_t WORKGROUP_SIZE = 16;  // 16x16 workgroups

    bool isFirstFrame = true;  // Track first frame for layout transitions
};
