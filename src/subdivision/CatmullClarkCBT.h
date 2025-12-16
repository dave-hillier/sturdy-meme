#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <vector>
#include "core/VulkanRAII.h"

// Concurrent Binary Tree (CBT) buffer for Catmull-Clark subdivision
// Based on the implementation from https://github.com/jdupuy/LongestEdgeBisection2D
class CatmullClarkCBT {
public:
    struct InitInfo {
        VmaAllocator allocator;
        int maxDepth;        // Maximum subdivision depth (e.g., 20)
        int faceCount;       // Number of base mesh faces (e.g., 6 for cube)
    };

    CatmullClarkCBT() = default;
    ~CatmullClarkCBT() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer_.get(); }
    uint32_t getBufferSize() const { return bufferSize; }
    int getMaxDepth() const { return maxDepth; }
    int getFaceCount() const { return faceCount; }

private:
    static uint32_t calculateBufferSize(int maxDepth, int faceCount);

    ManagedBuffer buffer_;
    uint32_t bufferSize = 0;
    int maxDepth = 20;
    int faceCount = 6;  // Default cube
};
