#include "VirtualTexturePageTable.h"
#include "VulkanBarriers.h"
#include "CommandBufferUtils.h"
#include "VulkanResourceFactory.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

namespace VirtualTexture {

std::unique_ptr<VirtualTexturePageTable> VirtualTexturePageTable::create(const InitInfo& info) {
    std::unique_ptr<VirtualTexturePageTable> pageTable(new VirtualTexturePageTable());
    if (!pageTable->initInternal(info)) {
        return nullptr;
    }
    return pageTable;
}

VirtualTexturePageTable::~VirtualTexturePageTable() {
    cleanup();
}

bool VirtualTexturePageTable::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTexturePageTable::initInternal requires raiiDevice");
        return false;
    }

    raiiDevice_ = info.raiiDevice;
    config = info.config;
    device_ = info.device;
    allocator_ = info.allocator;
    framesInFlight_ = info.framesInFlight;

    VkDevice device = info.device;
    VmaAllocator allocator = info.allocator;
    VkCommandPool commandPool = info.commandPool;
    VkQueue queue = info.queue;

    // Calculate total entries and offsets for each mip level
    size_t totalEntries = 0;
    mipOffsets.resize(config.maxMipLevels);
    mipSizes.resize(config.maxMipLevels);
    mipDirty.resize(config.maxMipLevels, false);

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        mipOffsets[mip] = totalEntries;
        uint32_t tilesAtMip = config.getTilesAtMip(mip);
        mipSizes[mip] = tilesAtMip * tilesAtMip;
        totalEntries += mipSizes[mip];
    }

    // Initialize CPU data
    cpuData.resize(totalEntries);
    for (auto& entry : cpuData) {
        entry.valid = 0;
        entry.cacheX = 0;
        entry.cacheY = 0;
    }

    // Create page table textures
    if (!createPageTableTextures(device, allocator, commandPool, queue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table textures");
        return false;
    }

    // Create sampler
    if (!createSampler(device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table sampler");
        return false;
    }

    // Create per-frame staging buffers (sized for largest mip level)
    uint32_t maxMipSize = mipSizes[0];
    VkDeviceSize stagingSize = maxMipSize * sizeof(uint32_t); // RGBA8 packed
    stagingBuffers_.resize(framesInFlight_);
    stagingMapped_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        if (!VulkanResourceFactory::createStagingBuffer(allocator, stagingSize, stagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table staging buffer %u", i);
            return false;
        }
        stagingMapped_[i] = stagingBuffers_[i].map();
    }

    SDL_Log("VirtualTexturePageTable initialized: %u mip levels, %zu total entries, %u staging buffers",
            config.maxMipLevels, totalEntries, framesInFlight_);

    return true;
}

void VirtualTexturePageTable::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;  // Not initialized
    VkDevice device = device_;
    VmaAllocator allocator = allocator_;
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

    pageTableSampler_.reset();

    if (combinedImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, combinedImageView, nullptr);
        combinedImageView = VK_NULL_HANDLE;
    }

    for (auto view : pageTableViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    pageTableViews.clear();

    for (size_t i = 0; i < pageTableImages.size(); ++i) {
        if (pageTableImages[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, pageTableImages[i], pageTableAllocations[i]);
        }
    }
    pageTableImages.clear();
    pageTableAllocations.clear();

    cpuData.clear();
    mipOffsets.clear();
    mipSizes.clear();
    mipDirty.clear();
}

bool VirtualTexturePageTable::createPageTableTextures(VkDevice device, VmaAllocator allocator,
                                                       VkCommandPool commandPool, VkQueue queue) {
    pageTableImages.resize(config.maxMipLevels);
    pageTableAllocations.resize(config.maxMipLevels);
    pageTableViews.resize(config.maxMipLevels);

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        uint32_t tilesAtMip = config.getTilesAtMip(mip);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UINT;  // RGBA8 for page table
        imageInfo.extent.width = tilesAtMip;
        imageInfo.extent.height = tilesAtMip;
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

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &pageTableImages[mip],
                           &pageTableAllocations[mip], nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create page table image for mip %u", mip);
            return false;
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pageTableImages[mip];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UINT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &pageTableViews[mip]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create page table view for mip %u", mip);
            return false;
        }
    }

    // Transition all images to shader read layout
    {
        CommandScope cmdScope(device, commandPool, queue);
        if (!cmdScope.begin()) return false;

        {
            Barriers::BarrierBatch batch(cmdScope.get(),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
                batch.imageTransition(pageTableImages[mip],
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    0, VK_ACCESS_SHADER_READ_BIT);
            }
        }

        if (!cmdScope.end()) return false;
    }

    return true;
}

bool VirtualTexturePageTable::createSampler(VkDevice device) {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTexturePageTable::createSampler requires raiiDevice");
        return false;
    }

    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    try {
        pageTableSampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create page table sampler: %s", e.what());
        return false;
    }
    return true;
}

size_t VirtualTexturePageTable::getEntryIndex(TileId id) const {
    if (id.mipLevel >= config.maxMipLevels) return SIZE_MAX;

    uint32_t tilesAtMip = config.getTilesAtMip(id.mipLevel);
    if (id.x >= tilesAtMip || id.y >= tilesAtMip) return SIZE_MAX;

    return mipOffsets[id.mipLevel] + id.y * tilesAtMip + id.x;
}

void VirtualTexturePageTable::setEntry(TileId id, uint16_t cacheX, uint16_t cacheY) {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return;

    cpuData[index].cacheX = cacheX;
    cpuData[index].cacheY = cacheY;
    cpuData[index].valid = 1;

    mipDirty[id.mipLevel] = true;
    dirty = true;
}

void VirtualTexturePageTable::clearEntry(TileId id) {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return;

    cpuData[index].valid = 0;
    cpuData[index].cacheX = 0;
    cpuData[index].cacheY = 0;

    mipDirty[id.mipLevel] = true;
    dirty = true;
}

PageTableEntry VirtualTexturePageTable::getEntry(TileId id) const {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return PageTableEntry{};
    return cpuData[index];
}

VkImageView VirtualTexturePageTable::getImageView(uint32_t mipLevel) const {
    if (mipLevel >= pageTableViews.size()) return VK_NULL_HANDLE;
    return pageTableViews[mipLevel];
}

void VirtualTexturePageTable::recordUpload(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!dirty) return;

    // Select the staging buffer for this frame to avoid race conditions
    uint32_t bufferIndex = frameIndex % framesInFlight_;
    if (bufferIndex >= stagingBuffers_.size() || !stagingMapped_[bufferIndex]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid page table staging buffer index %u", bufferIndex);
        return;
    }

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        if (!mipDirty[mip]) continue;

        uint32_t tilesAtMip = config.getTilesAtMip(mip);
        size_t numEntries = mipSizes[mip];

        // Pack entries into per-frame staging buffer
        uint32_t* packed = static_cast<uint32_t*>(stagingMapped_[bufferIndex]);
        for (size_t i = 0; i < numEntries; ++i) {
            packed[i] = cpuData[mipOffsets[mip] + i].packRGBA8();
        }

        // Transition to transfer dst
        Barriers::transitionImage(cmd, pageTableImages[mip],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        // Copy buffer to page table image
        Barriers::copyBufferToImageRegion(cmd, stagingBuffers_[bufferIndex].get(), pageTableImages[mip],
                                          0, 0, tilesAtMip, tilesAtMip);

        // Transition back to shader read
        Barriers::imageTransferToSampling(cmd, pageTableImages[mip]);

        mipDirty[mip] = false;
    }

    dirty = false;
}

} // namespace VirtualTexture
