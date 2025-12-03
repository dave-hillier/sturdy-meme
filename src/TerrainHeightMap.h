#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

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
        std::string heightmapPath;  // Optional: path to 16-bit PNG heightmap (empty = procedural)
        float minAltitude = 0.0f;   // Altitude for height value 0 (when loading from file)
        float maxAltitude = 200.0f; // Altitude for height value 65535 (when loading from file)
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
    bool loadHeightDataFromFile(const std::string& path, float minAlt, float maxAlt);
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
