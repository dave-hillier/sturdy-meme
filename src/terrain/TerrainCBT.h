#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include "VulkanRAII.h"

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
    void destroy();

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer_.get(); }
    uint32_t getBufferSize() const { return bufferSize; }
    int getMaxDepth() const { return maxDepth; }

private:
    static uint32_t calculateBufferSize(int maxDepth);

    ManagedBuffer buffer_;
    uint32_t bufferSize = 0;
    int maxDepth = 20;
};
