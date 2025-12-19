#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <limits>

#include "TreeVertex.h"

// Axis-Aligned Bounding Box for culling (same as Mesh.h)
struct TreeAABB {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 getExtents() const {
        return (max - min) * 0.5f;
    }

    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
};

// Mesh class for tree geometry with wind animation data
// Uses TreeVertex which includes branch origin and wind parameters per vertex
class TreeMesh {
public:
    TreeMesh() = default;
    ~TreeMesh() = default;

    void setCustomGeometry(const std::vector<TreeVertex>& verts, const std::vector<uint32_t>& inds);

    bool upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    void destroy(VmaAllocator allocator);

    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(indices.size()); }

    const std::vector<TreeVertex>& getVertices() const { return vertices; }
    const TreeAABB& getBounds() const { return bounds; }

    bool hasData() const { return !vertices.empty() && !indices.empty(); }
    bool isUploaded() const { return vertexBuffer != VK_NULL_HANDLE; }

private:
    void calculateBounds();

    std::vector<TreeVertex> vertices;
    std::vector<uint32_t> indices;
    TreeAABB bounds;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
};
