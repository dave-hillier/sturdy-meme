#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include "VulkanRAII.h"

// Terrain textures - albedo and grass far LOD textures
class TerrainTextures {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        std::string resourcePath;
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

private:
    bool createAlbedoTexture();
    bool createGrassFarLODTexture();
    bool uploadImageData(VkImage image, const void* data, uint32_t width, uint32_t height,
                         VkFormat format, uint32_t bytesPerPixel);

    // Init params
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::string resourcePath;

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
};
