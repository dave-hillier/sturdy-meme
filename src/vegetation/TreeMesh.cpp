#include "TreeMesh.h"
#include "VulkanRAII.h"
#include "VulkanResourceFactory.h"
#include <cstring>
#include <SDL3/SDL.h>

void TreeMesh::calculateBounds() {
    bounds = TreeAABB();
    for (const auto& vertex : vertices) {
        bounds.expand(vertex.position);
    }
}

void TreeMesh::setCustomGeometry(const std::vector<TreeVertex>& verts, const std::vector<uint32_t>& inds) {
    vertices = verts;
    indices = inds;
    calculateBounds();
}

bool TreeMesh::upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    if (vertices.empty() || indices.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: No vertex or index data");
        return false;
    }

    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

    // Create staging buffer using RAII
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, vertexBufferSize + indexBufferSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to create staging buffer");
        return false;
    }

    // Copy data to staging buffer
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to map staging buffer");
        return false;
    }
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char*>(data) + vertexBufferSize, indices.data(), indexBufferSize);
    stagingBuffer.unmap();

    // Create vertex buffer using RAII
    ManagedBuffer managedVertexBuffer;
    if (!VulkanResourceFactory::createVertexBuffer(allocator, vertexBufferSize, managedVertexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to create vertex buffer");
        return false;
    }

    // Create index buffer using RAII
    ManagedBuffer managedIndexBuffer;
    if (!VulkanResourceFactory::createIndexBuffer(allocator, indexBufferSize, managedIndexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to create index buffer");
        return false;
    }

    // Copy using command scope
    CommandScope cmd(device, commandPool, queue);
    if (!cmd.begin()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to begin command buffer");
        return false;
    }

    VkBufferCopy vertexCopyRegion{};
    vertexCopyRegion.srcOffset = 0;
    vertexCopyRegion.dstOffset = 0;
    vertexCopyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(cmd.get(), stagingBuffer.get(), managedVertexBuffer.get(), 1, &vertexCopyRegion);

    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.srcOffset = vertexBufferSize;
    indexCopyRegion.dstOffset = 0;
    indexCopyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(cmd.get(), stagingBuffer.get(), managedIndexBuffer.get(), 1, &indexCopyRegion);

    if (!cmd.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeMesh::upload: Failed to submit command buffer");
        return false;
    }

    // Success - transfer ownership to member variables
    managedVertexBuffer.releaseToRaw(vertexBuffer, vertexAllocation);
    managedIndexBuffer.releaseToRaw(indexBuffer, indexAllocation);

    SDL_Log("TreeMesh::upload: Uploaded %zu vertices (%zu bytes), %zu indices",
            vertices.size(), vertexBufferSize, indices.size());

    return true;
}

void TreeMesh::destroy(VmaAllocator allocator) {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
    }
}
