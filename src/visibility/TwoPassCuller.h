#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "InitContext.h"
#include "DescriptorManager.h"
#include "VmaBuffer.h"
#include "VmaImage.h"
#include "PerFrameBuffer.h"

class GPUClusterBuffer;
class HiZSystem;

// Uniform data for the cluster culling compute shader
struct alignas(16) ClusterCullUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::vec4 screenParams;    // width, height, 1/width, 1/height
    glm::vec4 depthParams;     // near, far, numMipLevels, unused
    uint32_t clusterCount;
    uint32_t instanceCount;
    uint32_t enableHiZ;
    uint32_t maxDrawCommands;
    uint32_t passIndex;        // 0 = pass 1 (prev visible), 1 = pass 2 (remaining)
    uint32_t _pad0, _pad1, _pad2;
};

// Uniform data for the cluster LOD selection compute shader
struct alignas(16) ClusterSelectUniforms {
    glm::mat4 viewProjMatrix;
    glm::vec4 screenParams;     // width, height, 1/width, 1/height
    uint32_t totalClusterCount; // total clusters in the DAG
    uint32_t instanceCount;
    float errorThreshold;       // max acceptable screen-space error in pixels
    uint32_t maxSelectedClusters;
};

/**
 * TwoPassCuller - Two-phase GPU occlusion culling for mesh clusters
 *
 * Implements the nanite-style two-pass approach:
 *
 * Pass 1 (early):
 *   - Test clusters visible in the previous frame (high hit rate)
 *   - Render these to produce an initial depth buffer
 *   - Build Hi-Z pyramid from this depth
 *
 * Pass 2 (late):
 *   - Test remaining clusters against the Hi-Z from pass 1
 *   - Catches newly visible clusters (disocclusion)
 *   - Results merged with pass 1 for final rendering
 *
 * The key insight: most clusters visible last frame are still visible,
 * so pass 1 produces a good depth buffer for pass 2's occlusion tests.
 *
 * LOD Selection uses top-down DAG traversal:
 *   - CPU seeds root cluster indices into the input buffer
 *   - Multiple dispatches process one level per pass, ping-ponging
 *     between input/output node buffers
 *   - Only clusters whose parents exceed the error threshold are visited
 *   - Selected clusters are accumulated across all passes
 *
 * Usage:
 *   1. create()
 *   2. setRootClusters() - set root cluster indices for DAG traversal
 *   3. updateUniforms() - set camera, frustum
 *   4. recordLODSelection() - top-down DAG traversal
 *   5. recordPass1() - cull previous frame's visible clusters
 *   6. [render pass 1 visible clusters]
 *   7. [build Hi-Z from pass 1 depth]
 *   8. recordPass2() - cull remaining clusters against Hi-Z
 *   9. [render pass 2 visible clusters]
 *   10. swapBuffers() - swap visible lists for next frame
 */
class TwoPassCuller {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TwoPassCuller(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        uint32_t maxClusters;      // Max clusters to cull per frame
        uint32_t maxDrawCommands;   // Max indirect draw commands
        uint32_t maxDAGLevels = 8;  // Max depth of DAG hierarchy
        const vk::raii::Device* raiiDevice = nullptr;
        bool hasDrawIndirectCount = false;  // vkCmdDrawIndexedIndirectCount support
    };

    static std::unique_ptr<TwoPassCuller> create(const InitInfo& info);
    static std::unique_ptr<TwoPassCuller> create(const InitContext& ctx,
                                                   uint32_t maxClusters,
                                                   uint32_t maxDrawCommands);

    ~TwoPassCuller();

    // Non-copyable, non-movable
    TwoPassCuller(const TwoPassCuller&) = delete;
    TwoPassCuller& operator=(const TwoPassCuller&) = delete;

    /**
     * Set root cluster indices for DAG traversal.
     * Call after uploading meshes. Each root is the coarsest LOD of a mesh.
     * These seed the first pass of the top-down LOD selection.
     */
    void setRootClusters(const std::vector<uint32_t>& rootIndices);

    /**
     * Update culling uniforms for the current frame.
     */
    void updateUniforms(uint32_t frameIndex,
                         const glm::mat4& view, const glm::mat4& proj,
                         const glm::vec3& cameraPos,
                         const glm::vec4 frustumPlanes[6],
                         uint32_t clusterCount, uint32_t instanceCount,
                         float nearPlane, float farPlane, uint32_t hiZMipLevels);

    /**
     * Set the LOD error threshold in pixels (default 1.0).
     * Lower = more detail, higher = more aggressive LOD.
     */
    void setErrorThreshold(float pixelError) { errorThreshold_ = pixelError; }
    float getErrorThreshold() const { return errorThreshold_; }

    /**
     * Record LOD selection via top-down DAG traversal.
     *
     * Dispatches cluster_select.comp once per DAG level, ping-ponging between
     * node buffers. Only evaluates clusters whose parents exceeded the error
     * threshold, avoiding wasted work on unreachable clusters.
     *
     * Must be called BEFORE recordPass1().
     */
    void recordLODSelection(VkCommandBuffer cmd, uint32_t frameIndex,
                             uint32_t totalDAGClusters, uint32_t instanceCount);

    /**
     * Get the buffer of selected cluster indices (output of LOD selection).
     * This is the input to the culling passes.
     */
    VkBuffer getSelectedClusterBuffer(uint32_t frameIndex) const;
    VkBuffer getSelectedCountBuffer(uint32_t frameIndex) const;

    /**
     * Record pass 1: cull previous frame's visible clusters.
     * After this, render the visible clusters and build Hi-Z.
     */
    void recordPass1(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Record pass 2: cull remaining clusters against Hi-Z.
     * hiZView must be the Hi-Z pyramid built from pass 1.
     */
    void recordPass2(VkCommandBuffer cmd, uint32_t frameIndex, VkImageView hiZView);

    /**
     * Swap the visible cluster buffers (call at end of frame).
     * Current frame's visible list becomes next frame's "previous" list.
     */
    void swapBuffers();

    // Access indirect draw commands and counts
    VkBuffer getPass1IndirectBuffer(uint32_t frameIndex) const;
    VkBuffer getPass1DrawCountBuffer(uint32_t frameIndex) const;
    VkBuffer getPass2IndirectBuffer(uint32_t frameIndex) const;
    VkBuffer getPass2DrawCountBuffer(uint32_t frameIndex) const;

    // Access per-draw data buffers (parallel to indirect commands, indexed by gl_DrawID)
    VkBuffer getPass1DrawDataBuffer(uint32_t frameIndex) const;
    VkBuffer getPass2DrawDataBuffer(uint32_t frameIndex) const;
    VkDeviceSize getDrawDataBufferSize() const;

    uint32_t getMaxDrawCommands() const { return maxDrawCommands_; }
    bool supportsDrawIndirectCount() const { return hasDrawIndirectCount_; }

    /**
     * Set external buffer references needed by culling descriptor sets.
     * Must be called before the first frame that uses the culler.
     * clusterBuffer: GPUClusterBuffer::getClusterBuffer()
     * instanceBuffers: per-frame instance buffers from GPUSceneBuffer
     */
    void setExternalBuffers(VkBuffer clusterBuffer, VkDeviceSize clusterSize,
                            const std::vector<VkBuffer>& instanceBuffers,
                            VkDeviceSize instanceSize);
    bool hasDescriptorSets() const { return !pass1DescSets_.empty(); }

    // Statistics
    struct Stats {
        uint32_t pass1Visible;
        uint32_t pass2Visible;
        uint32_t totalCulled;
    };

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createBuffers();
    void destroyBuffers();

    bool createPipeline();
    void destroyPipeline();

    bool createLODSelectPipeline();
    void destroyLODSelectPipeline();

    bool createDescriptorSets();
    void destroyDescriptorSets();

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    uint32_t maxClusters_ = 0;
    uint32_t maxDrawCommands_ = 0;
    uint32_t maxDAGLevels_ = 8;
    bool hasDrawIndirectCount_ = false;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Compute pipeline (cluster_cull.comp)
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Per-frame buffers (double-buffered for pass 1 / pass 2)
    // Indirect draw command buffers
    BufferUtils::PerFrameBufferSet pass1IndirectBuffers_;
    BufferUtils::PerFrameBufferSet pass1DrawCountBuffers_;
    BufferUtils::PerFrameBufferSet pass2IndirectBuffers_;
    BufferUtils::PerFrameBufferSet pass2DrawCountBuffers_;

    // Per-draw data buffers (parallel to indirect commands, indexed by gl_DrawID in raster shader)
    BufferUtils::PerFrameBufferSet pass1DrawDataBuffers_;
    BufferUtils::PerFrameBufferSet pass2DrawDataBuffers_;

    // Visible cluster tracking (double-buffered)
    BufferUtils::PerFrameBufferSet visibleClusterBuffers_;     // Current frame output
    BufferUtils::PerFrameBufferSet visibleCountBuffers_;       // Current frame count
    BufferUtils::PerFrameBufferSet prevVisibleClusterBuffers_; // Previous frame (pass 1 input)
    BufferUtils::PerFrameBufferSet prevVisibleCountBuffers_;   // Previous frame count

    // Uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Descriptor sets per frame
    std::vector<VkDescriptorSet> pass1DescSets_;
    std::vector<VkDescriptorSet> pass2DescSets_;

    // LOD selection pipeline (cluster_select.comp)
    std::optional<vk::raii::DescriptorSetLayout> lodSelectDescSetLayout_;
    VkPipelineLayout lodSelectPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline lodSelectPipeline_ = VK_NULL_HANDLE;

    // LOD selection buffers
    BufferUtils::PerFrameBufferSet selectedClusterBuffers_;   // Output: selected cluster indices
    BufferUtils::PerFrameBufferSet selectedCountBuffers_;     // Output: selected cluster count
    BufferUtils::PerFrameBufferSet lodSelectUniformBuffers_;  // Uniforms

    // Top-down DAG traversal: ping-pong node buffers
    // Buffer A and B alternate as input/output each level
    BufferUtils::PerFrameBufferSet nodeBufferA_;     // Node indices (ping)
    BufferUtils::PerFrameBufferSet nodeBufferB_;     // Node indices (pong)
    BufferUtils::PerFrameBufferSet nodeCountA_;      // Node count (ping)
    BufferUtils::PerFrameBufferSet nodeCountB_;      // Node count (pong)

    // Root cluster indices (CPU-seeded, copied to node buffer at start of traversal)
    std::vector<uint32_t> rootClusterIndices_;

    // LOD selection descriptor sets per frame (2 per frame for ping-pong)
    std::vector<VkDescriptorSet> lodSelectDescSetsAB_;  // input=A, output=B
    std::vector<VkDescriptorSet> lodSelectDescSetsBA_;  // input=B, output=A

    float errorThreshold_ = 1.0f;  // Default: 1 pixel error threshold

    // Ping-pong index for visible buffer swapping
    uint32_t currentBufferIndex_ = 0;

    // External buffer references for descriptor sets
    VkBuffer externalClusterBuffer_ = VK_NULL_HANDLE;
    VkDeviceSize externalClusterSize_ = 0;
    std::vector<VkBuffer> externalInstanceBuffers_;
    VkDeviceSize externalInstanceSize_ = 0;

    // Hi-Z sampler for pass 2 occlusion testing
    std::optional<vk::raii::Sampler> hiZSampler_;

    // Placeholder image for unbound Hi-Z descriptor in pass 1
    ManagedImage placeholderHiZImage_;
    VkImageView placeholderHiZView_ = VK_NULL_HANDLE;
};
