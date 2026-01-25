#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <utility>

class VmaImageHandle {
public:
    VmaImageHandle() = default;

    VmaImageHandle(VmaAllocator allocator, VkDevice device, VkImage image,
                   VmaAllocation allocation, VkImageView view)
        : allocator_(allocator)
        , device_(device)
        , image_(image)
        , allocation_(allocation)
        , view_(view) {}

    ~VmaImageHandle() {
        reset();
    }

    VmaImageHandle(const VmaImageHandle&) = delete;
    VmaImageHandle& operator=(const VmaImageHandle&) = delete;

    VmaImageHandle(VmaImageHandle&& other) noexcept {
        *this = std::move(other);
    }

    VmaImageHandle& operator=(VmaImageHandle&& other) noexcept {
        if (this != &other) {
            reset();
            allocator_ = other.allocator_;
            device_ = other.device_;
            image_ = other.image_;
            allocation_ = other.allocation_;
            view_ = other.view_;

            other.allocator_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
            other.image_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.view_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void reset() {
        if (device_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE) {
            vk::Device vkDevice(device_);
            vkDevice.destroyImageView(view_);
            view_ = VK_NULL_HANDLE;
        }
        if (allocator_ != VK_NULL_HANDLE && image_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
            image_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        }
    }

    VkImage getImage() const { return image_; }
    VkImageView getView() const { return view_; }
    VmaAllocation getAllocation() const { return allocation_; }

    bool isValid() const { return image_ != VK_NULL_HANDLE && view_ != VK_NULL_HANDLE; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
};

struct VmaImageSpec {
    vk::Format format = vk::Format::eUndefined;
    vk::Extent3D extent{1, 1, 1};
    vk::ImageUsageFlags usage{};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    vk::ImageType imageType = vk::ImageType::e2D;
    vk::ImageViewType viewType = vk::ImageViewType::e2D;
    vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
    uint32_t baseMipLevel = 0;
    uint32_t levelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    vk::ImageCreateFlags flags{};
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
    VkMemoryPropertyFlags requiredFlags = 0;

    [[nodiscard]] VmaImageSpec withFormat(vk::Format newFormat) const {
        auto copy = *this;
        copy.format = newFormat;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withExtent(vk::Extent3D newExtent) const {
        auto copy = *this;
        copy.extent = newExtent;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withUsage(vk::ImageUsageFlags newUsage) const {
        auto copy = *this;
        copy.usage = newUsage;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withMipLevels(uint32_t newMipLevels) const {
        auto copy = *this;
        copy.mipLevels = newMipLevels;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withArrayLayers(uint32_t newArrayLayers) const {
        auto copy = *this;
        copy.arrayLayers = newArrayLayers;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withView(vk::ImageViewType newViewType,
                                        vk::ImageAspectFlags newAspectMask,
                                        uint32_t newBaseMipLevel,
                                        uint32_t newLevelCount,
                                        uint32_t newBaseArrayLayer,
                                        uint32_t newLayerCount) const {
        auto copy = *this;
        copy.viewType = newViewType;
        copy.aspectMask = newAspectMask;
        copy.baseMipLevel = newBaseMipLevel;
        copy.levelCount = newLevelCount;
        copy.baseArrayLayer = newBaseArrayLayer;
        copy.layerCount = newLayerCount;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withSamples(vk::SampleCountFlagBits newSamples) const {
        auto copy = *this;
        copy.samples = newSamples;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withTiling(vk::ImageTiling newTiling) const {
        auto copy = *this;
        copy.tiling = newTiling;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withFlags(vk::ImageCreateFlags newFlags) const {
        auto copy = *this;
        copy.flags = newFlags;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withMemoryUsage(VmaMemoryUsage newUsage) const {
        auto copy = *this;
        copy.memoryUsage = newUsage;
        return copy;
    }

    [[nodiscard]] VmaImageSpec withRequiredFlags(VkMemoryPropertyFlags newFlags) const {
        auto copy = *this;
        copy.requiredFlags = newFlags;
        return copy;
    }

    VmaImageHandle build(VmaAllocator allocator, VkDevice device) const {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(imageType)
            .setFormat(format)
            .setExtent(extent)
            .setMipLevels(mipLevels)
            .setArrayLayers(arrayLayers)
            .setSamples(samples)
            .setTiling(tiling)
            .setUsage(usage)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFlags(flags);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;
        allocInfo.requiredFlags = requiredFlags;

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkResult result = vmaCreateImage(allocator,
                                         reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                                         &allocInfo, &image, &allocation, nullptr);
        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VmaImageSpec::build failed to create image: %d", result);
            return {};
        }

        uint32_t resolvedLevelCount = levelCount == 0 ? mipLevels : levelCount;
        uint32_t resolvedLayerCount = layerCount == 0 ? arrayLayers : layerCount;

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(image)
            .setViewType(viewType)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(aspectMask)
                .setBaseMipLevel(baseMipLevel)
                .setLevelCount(resolvedLevelCount)
                .setBaseArrayLayer(baseArrayLayer)
                .setLayerCount(resolvedLayerCount));

        vk::Device vkDevice(device);
        VkImageView view = VK_NULL_HANDLE;
        try {
            view = static_cast<VkImageView>(vkDevice.createImageView(viewInfo));
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VmaImageSpec::build failed to create view: %s", e.what());
            vmaDestroyImage(allocator, image, allocation);
            return {};
        }

        return VmaImageHandle(allocator, device, image, allocation, view);
    }
};
