#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <optional>
#include <memory>

// Terrain textures - albedo and grass far LOD textures
class TerrainTextures {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainTextures(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        std::string resourcePath;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<TerrainTextures> create(const InitInfo& info);


    ~TerrainTextures();

    // Move-only
    TerrainTextures(TerrainTextures&& other) noexcept;
    TerrainTextures& operator=(TerrainTextures&& other) noexcept;
    TerrainTextures(const TerrainTextures&) = delete;
    TerrainTextures& operator=(const TerrainTextures&) = delete;

    // Terrain albedo texture
    VkImageView getAlbedoView() const { return albedoView; }
    VkSampler getAlbedoSampler() const { return albedoSampler_ ? **albedoSampler_ : VK_NULL_HANDLE; }

    // Grass far LOD texture (for terrain blending at distance)
    VkImageView getGrassFarLODView() const { return grassFarLODView; }
    VkSampler getGrassFarLODSampler() const { return grassFarLODSampler_ ? **grassFarLODSampler_ : VK_NULL_HANDLE; }

private:
    bool initInternal(const InitInfo& info);
    bool createAlbedoTexture();
    bool createGrassFarLODTexture();
    bool uploadImageDataMipLevel(VkImage image, const void* data, uint32_t width, uint32_t height,
                                  VkFormat format, uint32_t bytesPerPixel, uint32_t mipLevel);
    bool generateMipmaps(VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels);

    // Init params
    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::string resourcePath;

    // Terrain albedo texture
    VkImage albedoImage = VK_NULL_HANDLE;
    VmaAllocation albedoAllocation = VK_NULL_HANDLE;
    VkImageView albedoView = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> albedoSampler_;
    uint32_t albedoMipLevels = 1;

    // Grass far LOD texture
    VkImage grassFarLODImage = VK_NULL_HANDLE;
    VmaAllocation grassFarLODAllocation = VK_NULL_HANDLE;
    VkImageView grassFarLODView = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> grassFarLODSampler_;
    uint32_t grassFarLODMipLevels = 1;
};
