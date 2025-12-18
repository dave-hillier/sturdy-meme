#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <limits>
#include <cstdint>
#include <memory>
#include "VulkanRAII.h"

// Height map for terrain - handles generation, GPU texture, and CPU queries
// Also handles hole mask for caves/wells (areas with no collision/rendering)
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

    // Special return value indicating a hole in terrain (no ground)
    static constexpr float NO_GROUND = -std::numeric_limits<float>::infinity();

    /**
     * Factory: Create and initialize TerrainHeightMap.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainHeightMap> create(const InitInfo& info);

    ~TerrainHeightMap();

    // Non-copyable, non-movable
    TerrainHeightMap(const TerrainHeightMap&) = delete;
    TerrainHeightMap& operator=(const TerrainHeightMap&) = delete;
    TerrainHeightMap(TerrainHeightMap&&) = delete;
    TerrainHeightMap& operator=(TerrainHeightMap&&) = delete;

    // GPU resource accessors
    VkImageView getView() const { return imageView; }
    VkSampler getSampler() const { return sampler.get(); }

    // Hole mask GPU resource accessors
    VkImageView getHoleMaskView() const { return holeMaskImageView; }
    VkSampler getHoleMaskSampler() const { return holeMaskSampler.get(); }

    // CPU-side height query (for physics/collision)
    // Returns NO_GROUND if position is inside a hole
    float getHeightAt(float x, float z) const;

    // Hole mask queries and modification
    bool isHole(float x, float z) const;
    void setHole(float x, float z, bool isHole);
    void setHoleCircle(float centerX, float centerZ, float radius, bool isHole);
    void uploadHoleMaskToGPU();  // Call after modifying holes to sync with GPU

    // Raw data accessors
    const float* getData() const { return cpuData.data(); }
    const uint8_t* getHoleMaskData() const { return holeMaskCpuData.data(); }
    uint32_t getResolution() const { return resolution; }
    float getHeightScale() const { return heightScale; }
    float getTerrainSize() const { return terrainSize; }

private:
    TerrainHeightMap() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool generateHeightData();
    bool loadHeightDataFromFile(const std::string& path, float minAlt, float maxAlt);
    bool createGPUResources();
    bool createHoleMaskResources();
    bool uploadToGPU();
    bool uploadHoleMaskToGPUInternal();

    // Helper to convert world coords to texel coords
    void worldToTexel(float x, float z, int& texelX, int& texelY) const;
    void worldToHoleMaskTexel(float x, float z, int& texelX, int& texelY) const;

    // Init params (stored for queries)
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    float terrainSize = 500.0f;
    float heightScale = 50.0f;
    uint32_t resolution = 512;
    uint32_t holeMaskResolution = 2048;  // Higher res for finer hole detail (~8m/texel on 16km terrain)

    // GPU resources for height map
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    ManagedSampler sampler;

    // GPU resources for hole mask (R8_UNORM: 0=solid, 255=hole)
    VkImage holeMaskImage = VK_NULL_HANDLE;
    VmaAllocation holeMaskAllocation = VK_NULL_HANDLE;
    VkImageView holeMaskImageView = VK_NULL_HANDLE;
    ManagedSampler holeMaskSampler;

    // CPU-side data for collision queries
    std::vector<float> cpuData;
    std::vector<uint8_t> holeMaskCpuData;  // 0 = solid, 255 = hole
    bool holeMaskDirty = false;  // True if CPU data has been modified but not uploaded
};
