#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

// Height map for terrain - handles generation, GPU texture, and CPU queries
class TerrainHeightMap {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        uint32_t resolution;
        float terrainSize;
        float heightScale;
    };

    TerrainHeightMap() = default;
    ~TerrainHeightMap() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // GPU resource accessors
    VkImageView getView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }

    // CPU-side height query (for physics/collision)
    float getHeightAt(float x, float z) const;

    // Raw data accessors
    const float* getData() const { return cpuData.data(); }
    uint32_t getResolution() const { return resolution; }

private:
    bool generateHeightData();
    bool createGPUResources();
    bool uploadToGPU();

    // Init params (stored for queries)
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    float terrainSize = 500.0f;
    float heightScale = 50.0f;
    uint32_t resolution = 512;

    // GPU resources
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // CPU-side data for collision queries
    std::vector<float> cpuData;
};
