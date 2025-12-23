#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include "VulkanRAII.h"

// Terrain textures - albedo, grass far LOD, and biome visualization textures
class TerrainTextures {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        std::string resourcePath;
        std::string biomeMapPath;  // Optional: path to biome_debug.png for visualization
    };

    TerrainTextures() = default;
    ~TerrainTextures() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Terrain albedo texture
    VkImageView getAlbedoView() const { return albedoView; }
    VkSampler getAlbedoSampler() const { return albedoSampler.get(); }

    // Grass far LOD texture (for terrain blending at distance)
    VkImageView getGrassFarLODView() const { return grassFarLODView; }
    VkSampler getGrassFarLODSampler() const { return grassFarLODSampler.get(); }

    // Biome map texture (for debug visualization)
    VkImageView getBiomeMapView() const { return biomeMapView; }
    VkSampler getBiomeMapSampler() const { return biomeMapSampler.get(); }
    bool hasBiomeMap() const { return biomeMapImage != VK_NULL_HANDLE; }

private:
    bool createAlbedoTexture();
    bool createGrassFarLODTexture();
    bool createBiomeMapTexture();
    bool uploadImageData(VkImage image, const void* data, uint32_t width, uint32_t height,
                         VkFormat format, uint32_t bytesPerPixel);

    // Init params
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::string resourcePath;
    std::string biomeMapPath;

    // Terrain albedo texture
    VkImage albedoImage = VK_NULL_HANDLE;
    VmaAllocation albedoAllocation = VK_NULL_HANDLE;
    VkImageView albedoView = VK_NULL_HANDLE;
    ManagedSampler albedoSampler;

    // Grass far LOD texture
    VkImage grassFarLODImage = VK_NULL_HANDLE;
    VmaAllocation grassFarLODAllocation = VK_NULL_HANDLE;
    VkImageView grassFarLODView = VK_NULL_HANDLE;
    ManagedSampler grassFarLODSampler;

    // Biome map texture (optional, for debug visualization)
    VkImage biomeMapImage = VK_NULL_HANDLE;
    VmaAllocation biomeMapAllocation = VK_NULL_HANDLE;
    VkImageView biomeMapView = VK_NULL_HANDLE;
    ManagedSampler biomeMapSampler;
};
