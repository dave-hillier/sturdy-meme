#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <memory>
#include "VmaBuffer.h"

// Pre-subdivided meshlet for terrain rendering
// Each CBT leaf node is rendered as an instance of this meshlet,
// providing higher resolution without increasing CBT memory
//
// Uses fence-free upload pattern like VirtualTextureCache:
// - Per-frame staging buffers avoid race conditions
// - recordUpload() records GPU commands without waiting
// - Frame fences handle synchronization naturally
class TerrainMeshlet {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainMeshlet(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator;
        uint32_t subdivisionLevel;  // Number of LEB subdivisions (e.g., 4 = 16 triangles, 6 = 64 triangles)
        uint32_t framesInFlight = 3;  // For per-frame staging buffers
    };

    /**
     * Factory: Create and initialize TerrainMeshlet.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainMeshlet> create(const InitInfo& info);


    ~TerrainMeshlet() = default;

    // Non-copyable, non-movable
    TerrainMeshlet(const TerrainMeshlet&) = delete;
    TerrainMeshlet& operator=(const TerrainMeshlet&) = delete;
    TerrainMeshlet(TerrainMeshlet&&) = delete;
    TerrainMeshlet& operator=(TerrainMeshlet&&) = delete;

    // Buffer accessors
    VkBuffer getVertexBuffer() const { return vertexBuffer_.get(); }
    VkBuffer getIndexBuffer() const { return indexBuffer_.get(); }
    uint32_t getVertexCount() const { return vertexCount_; }
    uint32_t getIndexCount() const { return indexCount_; }
    uint32_t getTriangleCount() const { return triangleCount_; }
    uint32_t getSubdivisionLevel() const { return subdivisionLevel_; }

    /**
     * Request a subdivision level change.
     * Does NOT wait on GPU - generates new geometry to CPU memory.
     * Call recordUpload() for each frame to upload the new geometry.
     * @return true if level changed, false if already at requested level
     */
    bool requestSubdivisionChange(uint32_t newLevel);

    /**
     * Check if there's pending geometry that needs uploading.
     */
    bool hasPendingUpload() const { return pendingUpload_; }

    /**
     * Record GPU commands to upload pending geometry.
     * Uses per-frame staging buffer to avoid race conditions.
     * Must be called once per frame until hasPendingUpload() returns false.
     *
     * @param cmd Command buffer to record into (must be in recording state)
     * @param frameIndex Current frame index for staging buffer selection
     */
    void recordUpload(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Get number of frames still needing upload after subdivision change.
     * Returns 0 when all frames have been uploaded.
     */
    uint32_t getPendingUploadFrames() const { return pendingUploadFrames_; }

private:
    bool initInternal(const InitInfo& info);

    // Generate meshlet geometry using LEB subdivision
    void generateMeshletGeometry(uint32_t level,
                                  std::vector<glm::vec2>& vertices,
                                  std::vector<uint16_t>& indices);

    // Recursive LEB subdivision helper
    void subdivideLEB(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2,
                      uint32_t depth, uint32_t targetDepth,
                      std::vector<glm::vec2>& vertices,
                      std::vector<uint16_t>& indices,
                      std::unordered_map<uint64_t, uint16_t>& vertexMap);

    // Helper to create unique vertex key for deduplication
    static uint64_t makeVertexKey(const glm::vec2& v);

    // Create GPU buffers sized for current geometry
    bool createBuffers();

    // Device-local GPU buffers
    ManagedBuffer vertexBuffer_;
    ManagedBuffer indexBuffer_;

    // Per-frame staging buffers (like VirtualTextureCache pattern)
    std::vector<ManagedBuffer> vertexStagingBuffers_;
    std::vector<ManagedBuffer> indexStagingBuffers_;
    std::vector<void*> vertexStagingMapped_;
    std::vector<void*> indexStagingMapped_;

    // Pending geometry in CPU memory (populated by requestSubdivisionChange)
    std::vector<glm::vec2> pendingVertices_;
    std::vector<uint16_t> pendingIndices_;

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t framesInFlight_ = 3;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    uint32_t triangleCount_ = 0;
    uint32_t subdivisionLevel_ = 0;

    // Upload state tracking
    bool pendingUpload_ = false;
    uint32_t pendingUploadFrames_ = 0;  // Count down as each frame uploads
};
