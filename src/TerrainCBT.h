#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

// Concurrent Binary Tree (CBT) buffer for terrain subdivision
class TerrainCBT {
public:
    struct InitInfo {
        VmaAllocator allocator;
        int maxDepth;
        int initDepth;  // Initial subdivision depth (e.g., 6 for 64 triangles)
    };

    TerrainCBT() = default;
    ~TerrainCBT() = default;

    bool init(const InitInfo& info);
    void destroy(VmaAllocator allocator);

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer; }
    uint32_t getBufferSize() const { return bufferSize; }
    int getMaxDepth() const { return maxDepth; }

private:
    static uint32_t calculateBufferSize(int maxDepth);

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t bufferSize = 0;
    int maxDepth = 20;
};
