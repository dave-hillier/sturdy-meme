#pragma once

#include <vulkan/vulkan.h>
#include <functional>

// Forward declaration
class TerrainSystem;

/**
 * TerrainResources - Resources provided by TerrainSystem
 *
 * Captures heightmap texture and height query function needed by
 * systems that need terrain data (grass, rocks, water, object placement).
 */
struct TerrainResources {
    VkImageView heightMapView = VK_NULL_HANDLE;
    VkSampler heightMapSampler = VK_NULL_HANDLE;
    std::function<float(float, float)> getHeightAt;
    float size = 0.0f;
    float heightScale = 0.0f;

    bool isValid() const { return heightMapView != VK_NULL_HANDLE; }

    // Collect from TerrainSystem
    static TerrainResources collect(const TerrainSystem& terrain);
};
