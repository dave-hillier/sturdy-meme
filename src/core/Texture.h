#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

class Texture {
public:
    Texture() = default;
    ~Texture() = default;

    bool load(const std::string& path, VmaAllocator allocator, VkDevice device,
              VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
              bool useSRGB = true);
    bool createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          VmaAllocator allocator, VkDevice device,
                          VkCommandPool commandPool, VkQueue queue);
    void destroy(VmaAllocator allocator, VkDevice device);

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }

private:
    bool loadDDS(const std::string& path, VmaAllocator allocator, VkDevice device,
                 VkCommandPool commandPool, VkQueue queue, bool useSRGB);
    bool transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                               VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    bool copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                           VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
};
