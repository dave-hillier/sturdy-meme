#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <memory>
#include "VmaResources.h"

// Concurrent Binary Tree (CBT) buffer for terrain subdivision
class TerrainCBT {
public:
    struct InitInfo {
        VmaAllocator allocator;
        int maxDepth;
        int initDepth;  // Initial subdivision depth (e.g., 6 for 64 triangles)
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<TerrainCBT> create(const InitInfo& info);

    ~TerrainCBT() = default;

    // Move-only (ManagedBuffer handles resources)
    TerrainCBT(TerrainCBT&&) = default;
    TerrainCBT& operator=(TerrainCBT&&) = default;
    TerrainCBT(const TerrainCBT&) = delete;
    TerrainCBT& operator=(const TerrainCBT&) = delete;

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer_.get(); }
    uint32_t getBufferSize() const { return bufferSize; }
    int getMaxDepth() const { return maxDepth; }

private:
    TerrainCBT() = default;
    bool initInternal(const InitInfo& info);
    static uint32_t calculateBufferSize(int maxDepth);

    ManagedBuffer buffer_;
    uint32_t bufferSize = 0;
    int maxDepth = 20;
};
