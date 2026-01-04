#include "TerrainMeshlet.h"
#include "VmaResources.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
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

bool TerrainMeshlet::createBuffers() {
    VkDeviceSize vertexBufferSize = pendingVertices_.size() * sizeof(glm::vec2);
    VkDeviceSize indexBufferSize = pendingIndices_.size() * sizeof(uint16_t);

    // Create device-local vertex buffer (with transfer dst for staging uploads)
    if (!VmaBufferFactory::createVertexStorageBuffer(allocator_, vertexBufferSize, vertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet vertex buffer");
        return false;
    }

    // Create device-local index buffer
    if (!VmaBufferFactory::createIndexBuffer(allocator_, indexBufferSize, indexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index buffer");
        return false;
    }

    return true;
}

bool TerrainMeshlet::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    framesInFlight_ = info.framesInFlight;
    subdivisionLevel_ = info.subdivisionLevel;

    // Generate meshlet geometry to pending buffers
    generateMeshletGeometry(subdivisionLevel_, pendingVertices_, pendingIndices_);

    vertexCount_ = static_cast<uint32_t>(pendingVertices_.size());
    indexCount_ = static_cast<uint32_t>(pendingIndices_.size());
    triangleCount_ = indexCount_ / 3;

    // Create device-local GPU buffers
    if (!createBuffers()) {
        return false;
    }

    // Create per-frame staging buffers (like VirtualTextureCache pattern)
    VkDeviceSize vertexBufferSize = pendingVertices_.size() * sizeof(glm::vec2);
    VkDeviceSize indexBufferSize = pendingIndices_.size() * sizeof(uint16_t);

    vertexStagingBuffers_.resize(framesInFlight_);
    indexStagingBuffers_.resize(framesInFlight_);
    vertexStagingMapped_.resize(framesInFlight_);
    indexStagingMapped_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        if (!VmaBufferFactory::createStagingBuffer(allocator_, vertexBufferSize, vertexStagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet vertex staging buffer %u", i);
            return false;
        }
        vertexStagingMapped_[i] = vertexStagingBuffers_[i].map();

        if (!VmaBufferFactory::createStagingBuffer(allocator_, indexBufferSize, indexStagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index staging buffer %u", i);
            return false;
        }
        indexStagingMapped_[i] = indexStagingBuffers_[i].map();
    }

    // Mark that we need to upload for all frames in flight
    pendingUpload_ = true;
    pendingUploadFrames_ = framesInFlight_;

    SDL_Log("TerrainMeshlet initialized: level %u, %u triangles, %u vertices, %u staging buffers",
            subdivisionLevel_, triangleCount_, vertexCount_, framesInFlight_);

    return true;
}

bool TerrainMeshlet::requestSubdivisionChange(uint32_t newLevel) {
    if (newLevel == subdivisionLevel_ && !pendingUpload_) {
        return false;  // Already at this level and no pending upload
    }

    // Generate new geometry to CPU memory (no GPU wait needed!)
    generateMeshletGeometry(newLevel, pendingVertices_, pendingIndices_);

    // Check if buffer sizes changed - if so, we need to recreate buffers
    uint32_t newVertexCount = static_cast<uint32_t>(pendingVertices_.size());
    uint32_t newIndexCount = static_cast<uint32_t>(pendingIndices_.size());

    if (newVertexCount != vertexCount_ || newIndexCount != indexCount_) {
        // Buffer sizes changed - need to recreate GPU buffers and staging buffers
        // This is the only case where we might need to be careful about in-flight frames
        // But since we're using per-frame staging, we just recreate everything

        VkDeviceSize vertexBufferSize = pendingVertices_.size() * sizeof(glm::vec2);
        VkDeviceSize indexBufferSize = pendingIndices_.size() * sizeof(uint16_t);

        // Recreate GPU buffers
        vertexBuffer_.reset();
        indexBuffer_.reset();
        if (!createBuffers()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate meshlet buffers");
            return false;
        }

        // Recreate staging buffers with new sizes
        for (uint32_t i = 0; i < framesInFlight_; ++i) {
            if (vertexStagingMapped_[i]) {
                vertexStagingBuffers_[i].unmap();
            }
            if (indexStagingMapped_[i]) {
                indexStagingBuffers_[i].unmap();
            }

            vertexStagingBuffers_[i].reset();
            indexStagingBuffers_[i].reset();

            if (!VmaBufferFactory::createStagingBuffer(allocator_, vertexBufferSize, vertexStagingBuffers_[i])) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate meshlet vertex staging buffer %u", i);
                return false;
            }
            vertexStagingMapped_[i] = vertexStagingBuffers_[i].map();

            if (!VmaBufferFactory::createStagingBuffer(allocator_, indexBufferSize, indexStagingBuffers_[i])) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate meshlet index staging buffer %u", i);
                return false;
            }
            indexStagingMapped_[i] = indexStagingBuffers_[i].map();
        }

        vertexCount_ = newVertexCount;
        indexCount_ = newIndexCount;
    }

    subdivisionLevel_ = newLevel;
    triangleCount_ = indexCount_ / 3;

    // Mark that we need to upload for all frames in flight
    pendingUpload_ = true;
    pendingUploadFrames_ = framesInFlight_;

    SDL_Log("TerrainMeshlet subdivision change requested: level %u (%u triangles)",
            subdivisionLevel_, triangleCount_);

    return true;
}

void TerrainMeshlet::recordUpload(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!pendingUpload_) {
        return;  // Nothing to upload
    }

    uint32_t bufferIndex = frameIndex % framesInFlight_;

    if (bufferIndex >= vertexStagingBuffers_.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainMeshlet::recordUpload: invalid frame index %u", frameIndex);
        return;
    }

    // Copy geometry to this frame's staging buffers
    VkDeviceSize vertexDataSize = pendingVertices_.size() * sizeof(glm::vec2);
    VkDeviceSize indexDataSize = pendingIndices_.size() * sizeof(uint16_t);

    if (vertexStagingMapped_[bufferIndex]) {
        std::memcpy(vertexStagingMapped_[bufferIndex], pendingVertices_.data(), vertexDataSize);
    }
    if (indexStagingMapped_[bufferIndex]) {
        std::memcpy(indexStagingMapped_[bufferIndex], pendingIndices_.data(), indexDataSize);
    }

    // Record copy commands: staging -> device-local
    vk::CommandBuffer vkCmd(cmd);
    auto vertexCopy = vk::BufferCopy{}
        .setSrcOffset(0)
        .setDstOffset(0)
        .setSize(vertexDataSize);
    vkCmd.copyBuffer(vertexStagingBuffers_[bufferIndex].get(), vertexBuffer_.get(), vertexCopy);

    auto indexCopy = vk::BufferCopy{}
        .setSrcOffset(0)
        .setDstOffset(0)
        .setSize(indexDataSize);
    vkCmd.copyBuffer(indexStagingBuffers_[bufferIndex].get(), indexBuffer_.get(), indexCopy);

    // Barrier: transfer -> vertex input (so the buffers are ready for drawing)
    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eVertexInput,
                              {}, barrier, {}, {});
    }

    // Track upload progress
    if (pendingUploadFrames_ > 0) {
        pendingUploadFrames_--;
    }

    // After all frames have uploaded, clear pending state
    if (pendingUploadFrames_ == 0) {
        pendingUpload_ = false;
        // Keep pending geometry in memory in case we need it again
        // (optional: could clear to save memory if subdivision changes are rare)
    }
}
