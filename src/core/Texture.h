#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>

class Texture {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit Texture(ConstructToken) {}

    // Factory methods - return nullptr on failure
    static std::unique_ptr<Texture> loadFromFile(const std::string& path, VmaAllocator allocator, VkDevice device,
                                                  VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                                  bool useSRGB = true);
    static std::unique_ptr<Texture> loadFromFileWithMipmaps(const std::string& path, VmaAllocator allocator, VkDevice device,
                                                             VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                                             bool useSRGB = true, bool enableAnisotropy = true);
    static std::unique_ptr<Texture> createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                                      VmaAllocator allocator, VkDevice device,
                                                      VkCommandPool commandPool, VkQueue queue);


    ~Texture();

    // Move-only
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }
    VkImage getImage() const { return image; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    bool loadInternal(const std::string& path, VmaAllocator allocator, VkDevice device,
                      VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                      bool useSRGB);
    bool loadWithMipmapsInternal(const std::string& path, VmaAllocator allocator, VkDevice device,
                                  VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                  bool useSRGB, bool enableAnisotropy);
    bool createSolidColorInternal(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                   VmaAllocator allocator, VkDevice device,
                                   VkCommandPool commandPool, VkQueue queue);
    bool loadDDS(const std::string& path, VmaAllocator allocator, VkDevice device,
                 VkCommandPool commandPool, VkQueue queue, bool useSRGB);
    bool transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                               VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    bool copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                           VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    // Stored for cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
};
