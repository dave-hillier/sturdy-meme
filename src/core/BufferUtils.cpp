#include "BufferUtils.h"

using namespace vk;  // Vulkan-Hpp type-safe wrappers

namespace BufferUtils {
namespace {
void destroyCreatedBuffers(VmaAllocator allocator,
                           const std::vector<VkBuffer>& buffers,
                           const std::vector<VmaAllocation>& allocations,
                           size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (buffers[i] != VK_NULL_HANDLE && allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers[i], allocations[i]);
        }
    }
}
}  // namespace

PerFrameBufferBuilder& PerFrameBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setFrameCount(uint32_t count) {
    frameCount = count;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize = size;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags = flags;
    return *this;
}

bool PerFrameBufferBuilder::build(PerFrameBufferSet& outBuffers) const {
    if (!allocator || frameCount == 0 || bufferSize == 0) {
        SDL_Log("PerFrameBufferBuilder missing required fields (allocator=%p, frameCount=%u, size=%zu)",
                allocator, frameCount, static_cast<size_t>(bufferSize));
        return false;
    }

    PerFrameBufferSet result{};
    result.buffers.resize(frameCount, VK_NULL_HANDLE);
    result.allocations.resize(frameCount, VK_NULL_HANDLE);
    result.mappedPointers.resize(frameCount, nullptr);

    BufferCreateInfo bufferInfo{
        {},                                          // flags
        bufferSize,                                  // size
        static_cast<BufferUsageFlags>(usage),
        SharingMode::eExclusive,
        0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocationFlags;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    for (uint32_t i = 0; i < frameCount; i++) {
        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo, &result.buffers[i], &result.allocations[i],
                            &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create per-frame buffer %u", i);
            destroyCreatedBuffers(allocator, result.buffers, result.allocations, i);
            return false;
        }
        result.mappedPointers[i] = allocationInfo.pMappedData;
    }

    outBuffers = std::move(result);
    return true;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSetCount(uint32_t count) {
    setCount = count;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize = size;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage = newUsage;
    return *this;
}

bool DoubleBufferedBufferBuilder::build(DoubleBufferedBufferSet& outBuffers) const {
    if (!allocator || setCount == 0 || bufferSize == 0 || usage == 0) {
        SDL_Log("DoubleBufferedBufferBuilder missing required fields (allocator=%p, setCount=%u, size=%zu, usage=%u)",
                allocator, setCount, static_cast<size_t>(bufferSize), usage);
        return false;
    }

    DoubleBufferedBufferSet result{};
    result.buffers.resize(setCount, VK_NULL_HANDLE);
    result.allocations.resize(setCount, VK_NULL_HANDLE);

    BufferCreateInfo bufferInfo{
        {},                                          // flags
        bufferSize,                                  // size
        static_cast<BufferUsageFlags>(usage),
        SharingMode::eExclusive,
        0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    for (uint32_t i = 0; i < setCount; i++) {
        if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo, &result.buffers[i], &result.allocations[i], nullptr) !=
            VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered buffer %u", i);
            destroyCreatedBuffers(allocator, result.buffers, result.allocations, i);
            return false;
        }
    }

    outBuffers = std::move(result);
    return true;
}

void destroyBuffers(VmaAllocator allocator, const PerFrameBufferSet& buffers) {
    if (!allocator) return;
    for (size_t i = 0; i < buffers.buffers.size(); i++) {
        if (buffers.buffers[i] != VK_NULL_HANDLE && buffers.allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers.buffers[i], buffers.allocations[i]);
        }
    }
}

void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers) {
    if (!allocator) return;
    for (size_t i = 0; i < buffers.buffers.size(); i++) {
        if (buffers.buffers[i] != VK_NULL_HANDLE && buffers.allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers.buffers[i], buffers.allocations[i]);
        }
    }
}

// SingleBufferBuilder implementation
SingleBufferBuilder& SingleBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize = size;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage = newUsage;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags = flags;
    return *this;
}

bool SingleBufferBuilder::build(SingleBuffer& outBuffer) const {
    if (!allocator || bufferSize == 0) {
        SDL_Log("SingleBufferBuilder missing required fields (allocator=%p, size=%zu)",
                allocator, static_cast<size_t>(bufferSize));
        return false;
    }

    BufferCreateInfo bufferInfo{
        {},                                          // flags
        bufferSize,                                  // size
        static_cast<BufferUsageFlags>(usage),
        SharingMode::eExclusive,
        0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocationFlags;

    SingleBuffer result{};
    VmaAllocationInfo allocationInfo{};

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo, &result.buffer, &result.allocation,
                        &allocationInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create single buffer");
        return false;
    }

    result.mappedPointer = allocationInfo.pMappedData;
    outBuffer = result;
    return true;
}

void destroyBuffer(VmaAllocator allocator, SingleBuffer& buffer) {
    if (!allocator) return;
    if (buffer.buffer != VK_NULL_HANDLE && buffer.allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

void destroyBuffer(VmaAllocator allocator, DynamicUniformBuffer& buffer) {
    if (!allocator) return;
    if (buffer.buffer != VK_NULL_HANDLE && buffer.allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

// DynamicUniformBufferBuilder implementation
DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setPhysicalDevice(VkPhysicalDevice device) {
    physicalDevice = device;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setFrameCount(uint32_t count) {
    frameCount = count;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setElementSize(VkDeviceSize size) {
    elementSize = size;
    return *this;
}

bool DynamicUniformBufferBuilder::build(DynamicUniformBuffer& outBuffer) const {
    if (!allocator || !physicalDevice || frameCount == 0 || elementSize == 0) {
        SDL_Log("DynamicUniformBufferBuilder missing required fields");
        return false;
    }

    // Get minimum uniform buffer offset alignment
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    VkDeviceSize minAlignment = props.limits.minUniformBufferOffsetAlignment;

    // Calculate aligned size (round up to alignment)
    DeviceSize alignedSize = (elementSize + minAlignment - 1) & ~(minAlignment - 1);
    DeviceSize totalSize = alignedSize * frameCount;

    BufferCreateInfo bufferInfo{
        {},                                          // flags
        totalSize,                                   // size
        BufferUsageFlagBits::eUniformBuffer,
        SharingMode::eExclusive,
        0, nullptr                                   // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    DynamicUniformBuffer result{};
    VmaAllocationInfo allocationInfo{};

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo, &result.buffer, &result.allocation,
                        &allocationInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic uniform buffer");
        return false;
    }

    result.mappedPointer = allocationInfo.pMappedData;
    result.alignedSize = alignedSize;
    result.elementSize = elementSize;
    result.frameCount = frameCount;

    outBuffer = result;
    return true;
}

// DoubleBufferedImageBuilder implementation
DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setDevice(VkDevice newDevice) {
    device = newDevice;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setExtent(uint32_t w, uint32_t h) {
    width = w;
    height = h;
    depth = 1;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setExtent3D(uint32_t w, uint32_t h, uint32_t d) {
    width = w;
    height = h;
    depth = d;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setFormat(VkFormat newFormat) {
    format = newFormat;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setUsage(VkImageUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setAspectMask(VkImageAspectFlags aspect) {
    aspectMask = aspect;
    return *this;
}

bool DoubleBufferedImageBuilder::build(DoubleBufferedImageSet& outImages) const {
    if (!device || !allocator || width == 0 || height == 0) {
        SDL_Log("DoubleBufferedImageBuilder missing required fields (device=%p, allocator=%p, width=%u, height=%u)",
                device, allocator, width, height);
        return false;
    }

    ImageCreateInfo imageInfo{
        {},                                          // flags
        (depth > 1) ? ImageType::e3D : ImageType::e2D,
        static_cast<Format>(format),
        Extent3D{width, height, depth},
        1, 1,                                        // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        static_cast<ImageUsageFlags>(usage),
        SharingMode::eExclusive,
        0, nullptr,                                  // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    DoubleBufferedImageSet result{};

    // Create both images
    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &result.images[i],
                           &result.allocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered image %d", i);
            // Clean up any already created
            for (int j = 0; j < i; j++) {
                vmaDestroyImage(allocator, result.images[j], result.allocations[j]);
            }
            return false;
        }
    }

    // Create image views
    ImageViewCreateInfo viewInfo{
        {},                                              // flags
        {},                                              // image (set per iteration)
        (depth > 1) ? ImageViewType::e3D : ImageViewType::e2D,
        static_cast<Format>(format),
        {},                                              // components (identity)
        ImageSubresourceRange{
            static_cast<ImageAspectFlags>(aspectMask),
            0, 1,                                        // baseMipLevel, levelCount
            0, 1                                         // baseArrayLayer, layerCount
        }
    };

    for (int i = 0; i < 2; i++) {
        viewInfo.image = result.images[i];
        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &result.views[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered image view %d", i);
            // Clean up views and images
            for (int j = 0; j < i; j++) {
                vkDestroyImageView(device, result.views[j], nullptr);
            }
            for (int j = 0; j < 2; j++) {
                vmaDestroyImage(allocator, result.images[j], result.allocations[j]);
            }
            return false;
        }
    }

    outImages = result;
    return true;
}

void destroyImages(VkDevice device, VmaAllocator allocator, DoubleBufferedImageSet& images) {
    if (!device || !allocator) return;

    for (int i = 0; i < 2; i++) {
        if (images.views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, images.views[i], nullptr);
        }
        if (images.images[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, images.images[i], images.allocations[i]);
        }
    }
    images = {};
}

}  // namespace BufferUtils

