#include "SkinnedMesh.h"
#include "VulkanRAII.h"
#include <SDL3/SDL_log.h>
#include <cstring>

using namespace vk;

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
        BufferCreateInfo bufferInfo{
            {},                                          // flags
            vertexBufferSize,                            // size
            BufferUsageFlagBits::eTransferSrc,
            SharingMode::eExclusive,
            0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        if (!ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, stagingVertexBuffer)) {
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
        BufferCreateInfo bufferInfo{
            {},                                          // flags
            indexBufferSize,                             // size
            BufferUsageFlagBits::eTransferSrc,
            SharingMode::eExclusive,
            0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        if (!ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, stagingIndexBuffer)) {
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
        BufferCreateInfo bufferInfo{
            {},                                          // flags
            vertexBufferSize,                            // size
            BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eVertexBuffer,
            SharingMode::eExclusive,
            0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        if (!ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, managedVertexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create device vertex buffer");
            return false;
        }
    }

    // Create device-local index buffer using RAII
    ManagedBuffer managedIndexBuffer;
    {
        BufferCreateInfo bufferInfo{
            {},                                          // flags
            indexBufferSize,                             // size
            BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eIndexBuffer,
            SharingMode::eExclusive,
            0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        if (!ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, managedIndexBuffer)) {
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
