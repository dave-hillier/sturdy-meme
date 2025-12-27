#pragma once

#include "VulkanRAII.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Halfedge mesh structure for Catmull-Clark subdivision
// Based on the implementation from https://github.com/jdupuy/LongestEdgeBisection2D
struct CatmullClarkMesh {
    // Vertex struct aligned for std430 SSBO layout
    // vec3 requires 16-byte alignment in std430, so we add explicit padding
    struct Vertex {
        glm::vec3 position;
        float _pad0 = 0.0f;    // Padding to align normal to 16 bytes
        glm::vec3 normal;
        float _pad1 = 0.0f;    // Padding to align uv to 8 bytes (and keep struct 16-byte aligned)
        glm::vec2 uv;
        glm::vec2 _pad2 = {0.0f, 0.0f};  // Padding to make struct size multiple of 16

        // Constructor for convenient initialization
        Vertex() = default;
        Vertex(glm::vec3 pos, glm::vec3 norm, glm::vec2 texCoord)
            : position(pos), _pad0(0), normal(norm), _pad1(0), uv(texCoord), _pad2(0, 0) {}
    };
    static_assert(sizeof(Vertex) == 48, "Vertex must be 48 bytes for std430 layout");

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
