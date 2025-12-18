#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <vector>
#include <memory>
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

    /**
     * Factory: Create and initialize CatmullClarkCBT.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<CatmullClarkCBT> create(const InitInfo& info);

    ~CatmullClarkCBT() = default;

    // Non-copyable, non-movable
    CatmullClarkCBT(const CatmullClarkCBT&) = delete;
    CatmullClarkCBT& operator=(const CatmullClarkCBT&) = delete;
    CatmullClarkCBT(CatmullClarkCBT&&) = delete;
    CatmullClarkCBT& operator=(CatmullClarkCBT&&) = delete;

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer_.get(); }
    uint32_t getBufferSize() const { return bufferSize; }
    int getMaxDepth() const { return maxDepth; }
    int getFaceCount() const { return faceCount; }

private:
    CatmullClarkCBT() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);

    static uint32_t calculateBufferSize(int maxDepth, int faceCount);

    ManagedBuffer buffer_;
    uint32_t bufferSize = 0;
    int maxDepth = 20;
    int faceCount = 6;  // Default cube
};
