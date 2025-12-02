#include "SkinnedMesh.h"
#include <SDL3/SDL_log.h>
#include <cstring>

void SkinnedMesh::setData(const SkinnedMeshData& data) {
    vertices = data.vertices;
    indices = data.indices;
    skeleton = data.skeleton;
}

void SkinnedMesh::upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    if (vertices.empty() || indices.empty()) {
        SDL_Log("SkinnedMesh: No data to upload");
        return;
    }

    // Create staging buffers and upload to GPU (similar to Mesh::upload)
    VkDeviceSize vertexBufferSize = sizeof(SkinnedVertex) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    // Create vertex staging buffer
    VkBuffer stagingVertexBuffer;
    VmaAllocation stagingVertexAllocation;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingVertexBuffer, &stagingVertexAllocation, nullptr);

        void* data;
        vmaMapMemory(allocator, stagingVertexAllocation, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vmaUnmapMemory(allocator, stagingVertexAllocation);
    }

    // Create index staging buffer
    VkBuffer stagingIndexBuffer;
    VmaAllocation stagingIndexAllocation;
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingIndexBuffer, &stagingIndexAllocation, nullptr);

        void* data;
        vmaMapMemory(allocator, stagingIndexAllocation, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vmaUnmapMemory(allocator, stagingIndexAllocation);
    }

    // Create device-local vertex buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &vertexBuffer, &vertexAllocation, nullptr);
    }

    // Create device-local index buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indexBuffer, &indexAllocation, nullptr);
    }

    // Copy staging buffers to device-local buffers
    VkCommandBufferAllocateInfo allocInfoCmd{};
    allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCmd.commandPool = commandPool;
    allocInfoCmd.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfoCmd, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};

    // Copy vertex buffer
    copyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingVertexBuffer, vertexBuffer, 1, &copyRegion);

    // Copy index buffer
    copyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingIndexBuffer, indexBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

    // Cleanup staging buffers
    vmaDestroyBuffer(allocator, stagingVertexBuffer, stagingVertexAllocation);
    vmaDestroyBuffer(allocator, stagingIndexBuffer, stagingIndexAllocation);

    SDL_Log("SkinnedMesh: Uploaded %zu vertices, %zu indices", vertices.size(), indices.size());
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
