#include "TerrainMeshlet.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <unordered_map>
#include <cmath>

uint64_t TerrainMeshlet::makeVertexKey(const glm::vec2& v) {
    // Quantize to avoid floating point comparison issues
    // Use 16-bit precision per component (65536 steps in [0,1])
    uint32_t x = static_cast<uint32_t>(std::round(v.x * 65535.0f));
    uint32_t y = static_cast<uint32_t>(std::round(v.y * 65535.0f));
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
}

void TerrainMeshlet::subdivideLEB(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2,
                                   uint32_t depth, uint32_t targetDepth,
                                   std::vector<glm::vec2>& vertices,
                                   std::vector<uint16_t>& indices,
                                   std::unordered_map<uint64_t, uint16_t>& vertexMap) {
    if (depth >= targetDepth) {
        // Output this triangle
        auto addVertex = [&](const glm::vec2& v) -> uint16_t {
            uint64_t key = makeVertexKey(v);
            auto it = vertexMap.find(key);
            if (it != vertexMap.end()) {
                return it->second;
            }
            uint16_t idx = static_cast<uint16_t>(vertices.size());
            vertices.push_back(v);
            vertexMap[key] = idx;
            return idx;
        };

        indices.push_back(addVertex(v0));
        indices.push_back(addVertex(v1));
        indices.push_back(addVertex(v2));
        return;
    }

    // LEB bisection: split the edge opposite to v0 (the edge v1-v2)
    // This follows the LEB convention where:
    // - v0 is the apex (opposite to the longest edge)
    // - v1, v2 are the endpoints of the longest edge
    glm::vec2 midpoint = (v1 + v2) * 0.5f;

    // Create two child triangles following LEB convention:
    // Left child: apex=v1, longest edge endpoints=(v0, midpoint)
    // Right child: apex=v2, longest edge endpoints=(midpoint, v0)
    // This maintains proper winding and LEB structure
    subdivideLEB(v1, v0, midpoint, depth + 1, targetDepth, vertices, indices, vertexMap);
    subdivideLEB(v2, midpoint, v0, depth + 1, targetDepth, vertices, indices, vertexMap);
}

void TerrainMeshlet::generateMeshletGeometry(uint32_t level,
                                              std::vector<glm::vec2>& vertices,
                                              std::vector<uint16_t>& indices) {
    vertices.clear();
    indices.clear();

    std::unordered_map<uint64_t, uint16_t> vertexMap;

    // Generate a uniformly tessellated triangle using barycentric coordinates.
    // The output (u, v) coordinates are interpreted in the shader as:
    //   weight0 = 1 - u - v  (contribution from v0)
    //   weight1 = u          (contribution from v1)
    //   weight2 = v          (contribution from v2)
    //
    // So the triangle corners are:
    //   (0, 0) -> 100% v0
    //   (1, 0) -> 100% v1
    //   (0, 1) -> 100% v2
    //
    // We use a regular grid subdivision for uniform tessellation.
    // Each subdivision level doubles the edge resolution.
    uint32_t edgeSubdivisions = 1u << level;  // 2^level subdivisions per edge

    // Generate vertices as a grid in barycentric space
    // For a triangle with n subdivisions per edge, we need vertices at
    // (i/n, j/n) where i + j <= n
    for (uint32_t i = 0; i <= edgeSubdivisions; ++i) {
        for (uint32_t j = 0; j <= edgeSubdivisions - i; ++j) {
            float u = static_cast<float>(i) / static_cast<float>(edgeSubdivisions);
            float v = static_cast<float>(j) / static_cast<float>(edgeSubdivisions);
            vertices.push_back(glm::vec2(u, v));
        }
    }

    // Generate indices for the triangles
    // We iterate through the grid and create triangles
    auto getVertexIndex = [edgeSubdivisions](uint32_t i, uint32_t j) -> uint16_t {
        // Vertices are stored row by row, where row i has (edgeSubdivisions - i + 1) vertices
        // Index = sum of previous rows + j
        // Sum of (n+1) + n + (n-1) + ... + (n-i+2) = i*(n+1) - i*(i-1)/2
        uint32_t n = edgeSubdivisions;
        uint32_t rowStart = i * (n + 1) - (i * (i - 1)) / 2;
        return static_cast<uint16_t>(rowStart + j);
    };

    for (uint32_t i = 0; i < edgeSubdivisions; ++i) {
        for (uint32_t j = 0; j < edgeSubdivisions - i; ++j) {
            // Two triangles form a parallelogram (except at edges)
            // Triangle 1: (i,j), (i+1,j), (i,j+1)
            uint16_t idx00 = getVertexIndex(i, j);
            uint16_t idx10 = getVertexIndex(i + 1, j);
            uint16_t idx01 = getVertexIndex(i, j + 1);

            indices.push_back(idx00);
            indices.push_back(idx10);
            indices.push_back(idx01);

            // Triangle 2: (i+1,j), (i+1,j+1), (i,j+1) - only if not at the diagonal edge
            if (j < edgeSubdivisions - i - 1) {
                uint16_t idx11 = getVertexIndex(i + 1, j + 1);
                indices.push_back(idx10);
                indices.push_back(idx11);
                indices.push_back(idx01);
            }
        }
    }

    SDL_Log("TerrainMeshlet: Generated %zu vertices, %zu indices (%zu triangles) at level %u",
            vertices.size(), indices.size(), indices.size() / 3, level);
}

std::unique_ptr<TerrainMeshlet> TerrainMeshlet::create(const InitInfo& info) {
    std::unique_ptr<TerrainMeshlet> meshlet(new TerrainMeshlet());
    if (!meshlet->initInternal(info)) {
        return nullptr;
    }
    return meshlet;
}

bool TerrainMeshlet::initInternal(const InitInfo& info) {
    subdivisionLevel = info.subdivisionLevel;

    // Generate meshlet geometry
    std::vector<glm::vec2> vertices;
    std::vector<uint16_t> indices;
    generateMeshletGeometry(subdivisionLevel, vertices, indices);

    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount = static_cast<uint32_t>(indices.size());
    triangleCount = indexCount / 3;

    // Create vertex buffer (vertex + storage for compute access, host-writable for upload)
    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(glm::vec2);
    if (!VulkanResourceFactory::createVertexStorageBufferHostWritable(info.allocator, vertexBufferSize, vertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet vertex buffer");
        return false;
    }

    // Copy vertex data
    void* data = vertexBuffer_.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map meshlet vertex buffer");
        return false;
    }
    memcpy(data, vertices.data(), vertexBufferSize);
    vertexBuffer_.unmap();

    // Create index buffer
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint16_t);
    if (!VulkanResourceFactory::createIndexBufferHostWritable(info.allocator, indexBufferSize, indexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index buffer");
        return false;
    }

    // Copy index data
    data = indexBuffer_.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map meshlet index buffer");
        return false;
    }
    memcpy(data, indices.data(), indexBufferSize);
    indexBuffer_.unmap();

    SDL_Log("TerrainMeshlet initialized: level %u, %u triangles, %u vertices",
            subdivisionLevel, triangleCount, vertexCount);

    return true;
}

