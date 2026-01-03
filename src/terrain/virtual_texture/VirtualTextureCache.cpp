#include "VirtualTextureCache.h"
#include "VulkanBarriers.h"
#include "VmaResources.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace VirtualTexture {

std::unique_ptr<VirtualTextureCache> VirtualTextureCache::create(const InitInfo& info) {
    std::unique_ptr<VirtualTextureCache> cache(new VirtualTextureCache());
    if (!cache->initInternal(info)) {
        return nullptr;
    }
    return cache;
}

VirtualTextureCache::~VirtualTextureCache() {
    // VmaBuffer cleanup - unmap first
    for (size_t i = 0; i < stagingBuffers_.size(); ++i) {
        if (stagingMapped_[i]) {
            stagingBuffers_[i].unmap();
            stagingMapped_[i] = nullptr;
        }
        stagingBuffers_[i].reset();
    }
    stagingBuffers_.clear();
    stagingMapped_.clear();

    cacheSampler_.reset();

    if (cacheImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, cacheImageView, nullptr);
        cacheImageView = VK_NULL_HANDLE;
    }

    if (cacheImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, cacheImage, cacheAllocation);
        cacheImage = VK_NULL_HANDLE;
        cacheAllocation = VK_NULL_HANDLE;
    }

    slots.clear();
    tileToSlot.clear();
}

VirtualTextureCache::VirtualTextureCache(VirtualTextureCache&& other) noexcept
    : device_(other.device_)
    , allocator_(other.allocator_)
    , config(other.config)
    , useCompression_(other.useCompression_)
    , raiiDevice_(other.raiiDevice_)
    , cacheImage(other.cacheImage)
    , cacheAllocation(other.cacheAllocation)
    , cacheImageView(other.cacheImageView)
    , cacheSampler_(std::move(other.cacheSampler_))
    , stagingBuffers_(std::move(other.stagingBuffers_))
    , stagingMapped_(std::move(other.stagingMapped_))
    , framesInFlight_(other.framesInFlight_)
    , slots(std::move(other.slots))
    , tileToSlot(std::move(other.tileToSlot))
{
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
    other.cacheImage = VK_NULL_HANDLE;
    other.cacheImageView = VK_NULL_HANDLE;
}

VirtualTextureCache& VirtualTextureCache::operator=(VirtualTextureCache&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        for (size_t i = 0; i < stagingBuffers_.size(); ++i) {
            if (stagingMapped_[i]) {
                stagingBuffers_[i].unmap();
            }
            stagingBuffers_[i].reset();
        }
        cacheSampler_.reset();
        if (cacheImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, cacheImageView, nullptr);
        }
        if (cacheImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, cacheImage, cacheAllocation);
        }

        // Move from other
        device_ = other.device_;
        allocator_ = other.allocator_;
        config = other.config;
        useCompression_ = other.useCompression_;
        raiiDevice_ = other.raiiDevice_;
        cacheImage = other.cacheImage;
        cacheAllocation = other.cacheAllocation;
        cacheImageView = other.cacheImageView;
        cacheSampler_ = std::move(other.cacheSampler_);
        stagingBuffers_ = std::move(other.stagingBuffers_);
        stagingMapped_ = std::move(other.stagingMapped_);
        framesInFlight_ = other.framesInFlight_;
        slots = std::move(other.slots);
        tileToSlot = std::move(other.tileToSlot);

        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.cacheImage = VK_NULL_HANDLE;
        other.cacheImageView = VK_NULL_HANDLE;
    }
    return *this;
}

bool VirtualTextureCache::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTextureCache::init requires raiiDevice");
        return false;
    }

    device_ = info.device;
    allocator_ = info.allocator;
    raiiDevice_ = info.raiiDevice;
    config = info.config;
    framesInFlight_ = info.framesInFlight;
    useCompression_ = info.useCompression;

    VkDevice device = info.device;
    VmaAllocator allocator = info.allocator;
    VkCommandPool commandPool = info.commandPool;
    VkQueue queue = info.queue;

    // Initialize slot array
    uint32_t totalSlots = config.getTotalCacheSlots();
    slots.resize(totalSlots);

    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    for (uint32_t i = 0; i < totalSlots; ++i) {
        slots[i].occupied = false;
        slots[i].lastUsedFrame = 0;
    }

    // Create the cache texture
    if (!createCacheTexture(device, allocator, commandPool, queue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache texture");
        return false;
    }

    // Create sampler
    if (!createSampler(device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache sampler");
        return false;
    }

    // Create per-frame staging buffers to avoid race conditions with in-flight frames
    // BC1: 8 bytes per 4x4 block = 0.5 bytes per pixel
    // RGBA8: 4 bytes per pixel
    uint32_t blockWidth = (config.tileSizePixels + 3) / 4;
    uint32_t blockHeight = (config.tileSizePixels + 3) / 4;
    VkDeviceSize stagingSize = useCompression_
        ? (blockWidth * blockHeight * 8)  // BC1: 8 bytes per block
        : (config.tileSizePixels * config.tileSizePixels * 4);  // RGBA8

    stagingBuffers_.resize(framesInFlight_);
    stagingMapped_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        if (!VmaBufferFactory::createStagingBuffer(allocator, stagingSize, stagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT staging buffer %u", i);
            return false;
        }
        stagingMapped_[i] = stagingBuffers_[i].map();
    }

    SDL_Log("VirtualTextureCache initialized: %u slots (%ux%u tiles), %upx cache, %u staging buffers, format: %s",
            totalSlots, slotsPerAxis, slotsPerAxis, config.cacheSizePixels, framesInFlight_,
            useCompression_ ? "BC1" : "RGBA8");

    return true;
}

bool VirtualTextureCache::createCacheTexture(VkDevice device, VmaAllocator allocator,
                                              VkCommandPool commandPool, VkQueue queue) {
    VkFormat cacheFormat = useCompression_ ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_R8G8B8A8_SRGB;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = cacheFormat;
    imageInfo.extent.width = config.cacheSizePixels;
    imageInfo.extent.height = config.cacheSizePixels;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &cacheImage,
                       &cacheAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cacheImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = cacheFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &cacheImageView) != VK_SUCCESS) {
        return false;
    }

    // Transition to shader read layout
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    vk::CommandBuffer vkCmd(cmd);
    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cacheImage)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    return true;
}

bool VirtualTextureCache::createSampler(VkDevice device) {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTextureCache::createSampler requires raiiDevice");
        return false;
    }

    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        cacheSampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cache sampler: %s", e.what());
        return false;
    }
    return true;
}

CacheSlot* VirtualTextureCache::allocateSlot(TileId id, uint32_t currentFrame) {
    uint32_t packed = id.pack();

    // Check if already in cache
    auto it = tileToSlot.find(packed);
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
        return &slots[it->second];
    }

    // Find an empty slot first
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].occupied) {
            slots[i].occupied = true;
            slots[i].tileId = id;
            slots[i].lastUsedFrame = currentFrame;
            tileToSlot[packed] = i;
            return &slots[i];
        }
    }

    // No empty slots, evict LRU
    size_t lruIndex = findLRUSlot();
    if (lruIndex < slots.size()) {
        // Remove old mapping
        uint32_t oldPacked = slots[lruIndex].tileId.pack();
        tileToSlot.erase(oldPacked);

        // Set new tile
        slots[lruIndex].tileId = id;
        slots[lruIndex].lastUsedFrame = currentFrame;
        tileToSlot[packed] = lruIndex;
        return &slots[lruIndex];
    }

    return nullptr;
}

void VirtualTextureCache::markUsed(TileId id, uint32_t currentFrame) {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
    }
}

bool VirtualTextureCache::hasTile(TileId id) const {
    return tileToSlot.find(id.pack()) != tileToSlot.end();
}

const CacheSlot* VirtualTextureCache::getSlot(TileId id) const {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        return &slots[it->second];
    }
    return nullptr;
}

size_t VirtualTextureCache::findLRUSlot() const {
    size_t lruIndex = 0;
    uint32_t oldestFrame = UINT32_MAX;

    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].occupied && slots[i].lastUsedFrame < oldestFrame) {
            oldestFrame = slots[i].lastUsedFrame;
            lruIndex = i;
        }
    }

    return lruIndex;
}

void VirtualTextureCache::recordTileUpload(TileId id, const void* pixelData,
                                            uint32_t width, uint32_t height,
                                            TileFormat format, VkCommandBuffer cmd, uint32_t frameIndex) {
    // Find the slot for this tile
    auto it = tileToSlot.find(id.pack());
    if (it == tileToSlot.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot upload tile not in cache");
        return;
    }

    // Check format compatibility
    bool tileIsCompressed = (format != TileFormat::RGBA8);
    if (tileIsCompressed != useCompression_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Tile format mismatch: tile is %s, cache is %s",
            tileIsCompressed ? "compressed" : "RGBA8",
            useCompression_ ? "BC1" : "RGBA8");
        return;
    }

    // Select the staging buffer for this frame to avoid race conditions
    uint32_t bufferIndex = frameIndex % framesInFlight_;
    if (bufferIndex >= stagingBuffers_.size() || !stagingMapped_[bufferIndex]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid staging buffer index %u", bufferIndex);
        return;
    }

    size_t slotIndex = it->second;
    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    uint32_t slotX = slotIndex % slotsPerAxis;
    uint32_t slotY = slotIndex / slotsPerAxis;

    // Calculate data size based on format
    VkDeviceSize dataSize;
    if (useCompression_) {
        // BC1: 8 bytes per 4x4 block
        uint32_t blockWidth = (width + 3) / 4;
        uint32_t blockHeight = (height + 3) / 4;
        dataSize = blockWidth * blockHeight * 8;
    } else {
        // RGBA8: 4 bytes per pixel
        dataSize = width * height * 4;
    }

    // Copy to per-frame staging buffer
    std::memcpy(stagingMapped_[bufferIndex], pixelData, dataSize);

    vk::CommandBuffer vkCmd(cmd);

    // Transition to transfer dst
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cacheImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                              vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, barrier);
    }

    // Copy buffer to image region at tile slot position
    {
        auto region = vk::BufferImageCopy{}
            .setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(0)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setImageOffset({static_cast<int32_t>(slotX * config.tileSizePixels),
                            static_cast<int32_t>(slotY * config.tileSizePixels), 0})
            .setImageExtent({width, height, 1});
        vkCmd.copyBufferToImage(stagingBuffers_[bufferIndex].get(), cacheImage,
                                vk::ImageLayout::eTransferDstOptimal, region);
    }

    // Transition back to shader read
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cacheImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);
    }
}

uint32_t VirtualTextureCache::getUsedSlotCount() const {
    uint32_t count = 0;
    for (const auto& slot : slots) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace VirtualTexture
