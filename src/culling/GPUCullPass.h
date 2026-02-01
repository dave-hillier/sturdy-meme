#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <string>

#include "core/PerFrameBuffer.h"
#include "core/vulkan/VmaBuffer.h"
#include "core/material/DescriptorManager.h"
#include "core/InitContext.h"

// Forward declarations
class GPUSceneBuffer;

// GPU Culling uniforms (matches shader struct)
struct alignas(16) GPUCullUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];     // xyz = normal, w = distance
    glm::vec4 cameraPosition;       // xyz = camera pos, w = unused
    glm::vec4 screenParams;         // x = width, y = height, z = 1/width, w = 1/height
    uint32_t objectCount;           // Number of objects to cull
    uint32_t enableHiZ;             // 1 = use Hi-Z, 0 = frustum only
    uint32_t maxDrawCommands;       // Output buffer capacity
    uint32_t padding;
};

/**
 * GPUCullPass - GPU-driven frustum culling for scene objects
 *
 * This class handles:
 * 1. Compute shader-based frustum culling
 * 2. Indirect draw command generation
 * 3. Integration with Hi-Z pyramid for occlusion culling (future)
 *
 * Usage:
 *   1. create() - Factory method to initialize
 *   2. updateUniforms() - Set view/projection matrices each frame
 *   3. recordCulling() - Record compute dispatch in command buffer
 *   4. Use GPUSceneBuffer's indirect buffers for rendering
 */
class GPUCullPass {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GPUCullPass(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Factory methods
    static std::unique_ptr<GPUCullPass> create(const InitInfo& info);
    static std::unique_ptr<GPUCullPass> create(const InitContext& ctx);

    ~GPUCullPass();

    // Non-copyable, non-movable
    GPUCullPass(const GPUCullPass&) = delete;
    GPUCullPass& operator=(const GPUCullPass&) = delete;
    GPUCullPass(GPUCullPass&&) = delete;
    GPUCullPass& operator=(GPUCullPass&&) = delete;

    // Update culling uniforms (call before recording)
    void updateUniforms(uint32_t frameIndex,
                        const glm::mat4& view,
                        const glm::mat4& proj,
                        const glm::vec3& cameraPos,
                        uint32_t objectCount);

    // Bind scene buffer for culling
    void bindSceneBuffer(GPUSceneBuffer* sceneBuffer, uint32_t frameIndex);

    // Record culling compute pass
    // Assumes scene buffer is already uploaded
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex);

    // Get the uniform buffer for external binding
    VkBuffer getUniformBuffer(uint32_t frameIndex) const;

    // Statistics
    struct CullingStats {
        uint32_t totalObjects;
        uint32_t visibleObjects;
    };
    CullingStats getStats(uint32_t frameIndex) const;

    // Enable/disable Hi-Z occlusion culling
    void setHiZEnabled(bool enabled) { hiZEnabled_ = enabled; }
    bool isHiZEnabled() const { return hiZEnabled_; }

    // Set Hi-Z pyramid for occlusion culling (optional)
    void setHiZPyramid(VkImageView pyramidView, VkSampler sampler);

    // Set placeholder image for when Hi-Z is not available (required for MoltenVK)
    void setPlaceholderImage(VkImageView view, VkSampler sampler);

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createPipeline();
    bool createBuffers();
    bool createDescriptorSets();

    void destroyPipeline();
    void destroyBuffers();
    void destroyDescriptorSets();

    // Extract frustum planes from view-projection matrix
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    // Per-frame descriptor sets
    std::vector<VkDescriptorSet> descSets_;

    // Per-frame uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Currently bound scene buffer
    GPUSceneBuffer* currentSceneBuffer_ = nullptr;

    // Hi-Z pyramid reference (optional)
    VkImageView hiZPyramidView_ = VK_NULL_HANDLE;
    VkSampler hiZSampler_ = VK_NULL_HANDLE;
    bool hiZEnabled_ = false;

    // Placeholder image for descriptor binding when Hi-Z is unavailable
    VkImageView placeholderImageView_ = VK_NULL_HANDLE;
    VkSampler placeholderSampler_ = VK_NULL_HANDLE;

    // Workgroup size (must match shader)
    static constexpr uint32_t WORKGROUP_SIZE = 64;
    static constexpr uint32_t MAX_OBJECTS = 8192;
};
