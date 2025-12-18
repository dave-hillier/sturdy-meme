#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <memory>
#include "core/VulkanRAII.h"

// Pre-subdivided meshlet for terrain rendering
// Each CBT leaf node is rendered as an instance of this meshlet,
// providing higher resolution without increasing CBT memory
class TerrainMeshlet {
public:
    struct InitInfo {
        VmaAllocator allocator;
        uint32_t subdivisionLevel;  // Number of LEB subdivisions (e.g., 4 = 16 triangles, 6 = 64 triangles)
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
    uint32_t getVertexCount() const { return vertexCount; }
    uint32_t getIndexCount() const { return indexCount; }
    uint32_t getTriangleCount() const { return triangleCount; }
    uint32_t getSubdivisionLevel() const { return subdivisionLevel; }

private:
    TerrainMeshlet() = default;  // Private: use factory

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

    ManagedBuffer vertexBuffer_;
    ManagedBuffer indexBuffer_;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t subdivisionLevel = 0;
};
