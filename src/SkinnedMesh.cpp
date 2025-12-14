#include "SkinnedMesh.h"
#include "VulkanRAII.h"
#include <SDL3/SDL_log.h>
#include <cstring>

void SkinnedMesh::setData(const SkinnedMeshData& data) {
    vertices = data.vertices;
    indices = data.indices;
    skeleton = data.skeleton;
}

bool SkinnedMesh::upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    if (vertices.empty() || indices.empty()) {
        SDL_Log("SkinnedMesh: No data to upload");
        return false;
    }

    VkDeviceSize vertexBufferSize = sizeof(SkinnedVertex) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    // Create vertex staging buffer using RAII
    ManagedBuffer stagingVertexBuffer;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        if (!ManagedBuffer::create(allocator, bufferInfo, allocInfo, stagingVertexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create vertex staging buffer");
            return false;
        }

        void* data = stagingVertexBuffer.map();
        if (!data) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to map vertex staging buffer");
            return false;
        }
        memcpy(data, vertices.data(), vertexBufferSize);
        stagingVertexBuffer.unmap();
    }

    // Create index staging buffer using RAII
    ManagedBuffer stagingIndexBuffer;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        if (!ManagedBuffer::create(allocator, bufferInfo, allocInfo, stagingIndexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create index staging buffer");
            return false;
        }

        void* data = stagingIndexBuffer.map();
        if (!data) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to map index staging buffer");
            return false;
        }
        memcpy(data, indices.data(), indexBufferSize);
        stagingIndexBuffer.unmap();
    }

    // Create device-local vertex buffer using RAII
    ManagedBuffer managedVertexBuffer;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (!ManagedBuffer::create(allocator, bufferInfo, allocInfo, managedVertexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create device vertex buffer");
            return false;
        }
    }

    // Create device-local index buffer using RAII
    ManagedBuffer managedIndexBuffer;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (!ManagedBuffer::create(allocator, bufferInfo, allocInfo, managedIndexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create device index buffer");
            return false;
        }
    }

    // Copy staging buffers to device-local buffers using command scope
    CommandScope cmd(device, commandPool, queue);
    if (!cmd.begin()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to begin command buffer");
        return false;
    }

    VkBufferCopy copyRegion{};

    // Copy vertex buffer
    copyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(cmd.get(), stagingVertexBuffer.get(), managedVertexBuffer.get(), 1, &copyRegion);

    // Copy index buffer
    copyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(cmd.get(), stagingIndexBuffer.get(), managedIndexBuffer.get(), 1, &copyRegion);

    if (!cmd.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to submit command buffer");
        return false;
    }

    // Success - transfer ownership to member variables
    managedVertexBuffer.releaseToRaw(vertexBuffer, vertexAllocation);
    managedIndexBuffer.releaseToRaw(indexBuffer, indexAllocation);

    // Staging buffers automatically destroyed here

    SDL_Log("SkinnedMesh: Uploaded %zu vertices, %zu indices", vertices.size(), indices.size());
    return true;
}

void SkinnedMesh::destroy(VmaAllocator allocator) {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
    }
}
