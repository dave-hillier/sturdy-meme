#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <optional>

#include "BufferUtils.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include "Mesh.h"
#include "VmaResources.h"
#include "core/ImageBuilder.h"

// GPU-side object data for culling (matches shader struct)
struct alignas(16) CullObjectData {
    glm::vec4 boundingSphere;   // xyz = center (world space), w = radius
    glm::vec4 aabbMin;          // xyz = min corner (world space), w = unused
    glm::vec4 aabbMax;          // xyz = max corner (world space), w = unused
    uint32_t meshIndex;         // Index into mesh data for indirect draw
    uint32_t firstIndex;        // First index in index buffer
    uint32_t indexCount;        // Number of indices
    uint32_t vertexOffset;      // Vertex offset
};

// Indirect draw command (matches VkDrawIndexedIndirectCommand)
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// Hi-Z culling uniforms (matches shader UBO)
struct alignas(16) HiZCullUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];     // Frustum planes for culling
    glm::vec4 cameraPosition;       // xyz = camera pos, w = unused
    glm::vec4 screenParams;         // x = width, y = height, z = 1/width, w = 1/height
    glm::vec4 depthParams;          // x = near, y = far, z = numMipLevels, w = unused
    uint32_t objectCount;           // Number of objects to cull
    uint32_t enableHiZ;             // 1 = use Hi-Z, 0 = frustum only
    uint32_t padding[2];
};

// Hi-Z pyramid generation push constants
struct HiZPyramidPushConstants {
    uint32_t srcWidth, srcHeight;
    uint32_t dstWidth, dstHeight;
    uint32_t srcMipLevel;
    uint32_t isFirstPass;
};

// Hierarchical Z-Buffer Occlusion Culling System
// Generates a depth pyramid from the depth buffer and uses it to cull objects
class HiZSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkFormat depthFormat;       // Format of the source depth buffer
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize Hi-Z system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<HiZSystem> create(const InitInfo& info);
    static std::unique_ptr<HiZSystem> create(const InitContext& ctx, VkFormat depthFormat);

    ~HiZSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    HiZSystem(HiZSystem&&) = delete;
    HiZSystem& operator=(HiZSystem&&) = delete;
    HiZSystem(const HiZSystem&) = delete;
    HiZSystem& operator=(const HiZSystem&) = delete;

    void resize(VkExtent2D newExtent);

    // Update the source depth buffer view for pyramid generation
    void setDepthBuffer(VkImageView depthView, VkSampler depthSampler);

    // Update culling uniforms (call before recording culling)
    void updateUniforms(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj,
                        const glm::vec3& cameraPos, float nearPlane, float farPlane);

    // Submit objects to be culled (call once when scene changes)
    void updateObjectData(const std::vector<CullObjectData>& objects);

    // Record Hi-Z pyramid generation compute pass
    // Call AFTER the main depth pass completes
    void recordPyramidGeneration(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record occlusion culling compute pass
    // Call AFTER pyramid generation
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex);

    // Get the indirect draw buffer for rendering
    VkBuffer getIndirectDrawBuffer(uint32_t frameIndex) const;

    // Get the draw count buffer (for indirect count draws)
    VkBuffer getDrawCountBuffer(uint32_t frameIndex) const;

    // Get current object count (for draw limits)
    uint32_t getObjectCount() const { return objectCount; }

    // Get actual draw count after culling (read back for debugging)
    uint32_t getVisibleCount(uint32_t frameIndex) const;

    // Hi-Z pyramid access (for debugging/visualization and external occlusion culling)
    VkImageView getHiZPyramidView() const { return hiZPyramid.fullView ? **hiZPyramid.fullView : VK_NULL_HANDLE; }
    VkSampler getHiZSampler() const { return hiZSampler_ ? **hiZSampler_ : VK_NULL_HANDLE; }
    VkImageView getHiZMipView(uint32_t mipLevel) const;
    uint32_t getMipLevelCount() const { return mipLevelCount; }

    // Enable/disable Hi-Z culling (falls back to frustum-only)
    void setHiZEnabled(bool enabled) { hiZEnabled = enabled; }
    bool isHiZEnabled() const { return hiZEnabled; }

    // Statistics
    struct CullingStats {
        uint32_t totalObjects;
        uint32_t visibleObjects;
        uint32_t frustumCulled;
        uint32_t occlusionCulled;
    };
    CullingStats getStats() const { return stats; }

private:
    HiZSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    // Hi-Z pyramid resources
    bool createHiZPyramid();
    void destroyHiZPyramid();

    // Compute pipelines
    bool createPyramidPipeline();
    bool createCullingPipeline();
    void destroyPipelines();

    // Descriptor sets
    bool createDescriptorSets();
    void destroyDescriptorSets();

    // Buffers
    bool createBuffers();
    void destroyBuffers();

    // Calculate number of mip levels for given extent
    static uint32_t calculateMipLevels(VkExtent2D extent) {
        uint32_t maxDim = std::max(extent.width, extent.height);
        return static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;
    }

    // Extract frustum planes from view-projection matrix
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    // Synchronize culling output for indirect draw consumption
    void barrierCullingToIndirectDraw(VkCommandBuffer cmd);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Hi-Z pyramid texture (R32_SFLOAT for max depth values)
    static constexpr VkFormat HIZ_FORMAT = VK_FORMAT_R32_SFLOAT;
    MipChainBuilder::Result hiZPyramid;                  // Image with per-mip views
    std::optional<vk::raii::Sampler> hiZSampler_;        // Sampler for Hi-Z reads
    uint32_t mipLevelCount = 0;

    // Source depth buffer reference
    VkImageView sourceDepthView = VK_NULL_HANDLE;
    VkSampler sourceDepthSampler = VK_NULL_HANDLE;

    // Pyramid generation pipeline
    std::optional<vk::raii::DescriptorSetLayout> pyramidDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> pyramidPipelineLayout_;
    std::optional<vk::raii::Pipeline> pyramidPipeline_;
    std::vector<VkDescriptorSet> pyramidDescSets;  // One per mip level

    // Culling pipeline
    std::optional<vk::raii::DescriptorSetLayout> cullingDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> cullingPipelineLayout_;
    std::optional<vk::raii::Pipeline> cullingPipeline_;
    std::vector<VkDescriptorSet> cullingDescSets;  // Per frame

    // Object data buffer (input to culling, RAII-managed)
    ManagedBuffer objectDataBuffer_;
    uint32_t objectCount = 0;
    uint32_t objectBufferCapacity = 0;

    // Indirect draw buffer (output from culling)
    BufferUtils::PerFrameBufferSet indirectDrawBuffers;

    // Draw count buffer (for vkCmdDrawIndexedIndirectCount)
    BufferUtils::PerFrameBufferSet drawCountBuffers;

    // Culling uniforms (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // State
    bool hiZEnabled = true;
    CullingStats stats = {};

    static constexpr uint32_t MAX_OBJECTS = 4096;
    static constexpr uint32_t WORKGROUP_SIZE = 64;
};
