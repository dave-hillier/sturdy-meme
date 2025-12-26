#pragma once

#include "VulkanRAII.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Halfedge mesh structure for Catmull-Clark subdivision
// Based on the implementation from https://github.com/jdupuy/LongestEdgeBisection2D
struct CatmullClarkMesh {
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };

    struct Halfedge {
        uint32_t vertexID;     // Vertex at the start of this halfedge
        uint32_t nextID;       // Next halfedge in the face
        uint32_t twinID;       // Opposite halfedge (or ~0u if boundary)
        uint32_t faceID;       // Face this halfedge belongs to
    };

    struct Face {
        uint32_t halfedgeID;   // Any halfedge belonging to this face
        uint32_t valence;      // Number of edges/vertices in this face (usually 4 for quads)
    };

    std::vector<Vertex> vertices;
    std::vector<Halfedge> halfedges;
    std::vector<Face> faces;

    // GPU buffers (RAII-managed)
    ManagedBuffer vertexBuffer_;
    ManagedBuffer halfedgeBuffer_;
    ManagedBuffer faceBuffer_;

    // Buffer accessors
    VkBuffer getVertexBuffer() const { return vertexBuffer_.get(); }
    VkBuffer getHalfedgeBuffer() const { return halfedgeBuffer_.get(); }
    VkBuffer getFaceBuffer() const { return faceBuffer_.get(); }

    bool uploadToGPU(VmaAllocator allocator);

    // Factory methods for common meshes
    static CatmullClarkMesh createCube();
    static CatmullClarkMesh createQuad();
};
