#include "SkinnedMesh.h"
#include "VmaResources.h"
#include "CommandBufferUtils.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
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
        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(vertexBufferSize)
            .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
            .setSharingMode(vk::SharingMode::eExclusive);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        if (!ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, stagingVertexBuffer)) {
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
        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(indexBufferSize)
            .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
            .setSharingMode(vk::SharingMode::eExclusive);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        if (!ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, stagingIndexBuffer)) {
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
        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(vertexBufferSize)
            .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer)
            .setSharingMode(vk::SharingMode::eExclusive);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (!ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, managedVertexBuffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMesh: Failed to create device vertex buffer");
            return false;
        }
    }

    // Create device-local index buffer using RAII
    ManagedBuffer managedIndexBuffer;
    {
        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(indexBufferSize)
            .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer)
            .setSharingMode(vk::SharingMode::eExclusive);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (!ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, managedIndexBuffer)) {
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

    vk::CommandBuffer vkCmd(cmd.get());

    // Copy vertex buffer
    auto vertexCopy = vk::BufferCopy{}.setSrcOffset(0).setDstOffset(0).setSize(vertexBufferSize);
    vkCmd.copyBuffer(stagingVertexBuffer.get(), managedVertexBuffer.get(), vertexCopy);

    // Copy index buffer
    auto indexCopy = vk::BufferCopy{}.setSrcOffset(0).setDstOffset(0).setSize(indexBufferSize);
    vkCmd.copyBuffer(stagingIndexBuffer.get(), managedIndexBuffer.get(), indexCopy);

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
