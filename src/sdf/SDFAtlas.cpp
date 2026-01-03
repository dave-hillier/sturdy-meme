#include "SDFAtlas.h"
#include "CommandBufferUtils.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <cstring>

std::unique_ptr<SDFAtlas> SDFAtlas::create(const InitInfo& info) {
    auto atlas = std::unique_ptr<SDFAtlas>(new SDFAtlas());
    if (!atlas->initInternal(info)) {
        return nullptr;
    }
    return atlas;
}

std::unique_ptr<SDFAtlas> SDFAtlas::create(const InitContext& ctx, const SDFConfig& config) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.commandPool = ctx.commandPool;
    info.transferQueue = ctx.graphicsQueue;
    info.sdfPath = ctx.resourcePath + "/sdf";
    info.config = config;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

SDFAtlas::~SDFAtlas() {
    cleanup();
}

bool SDFAtlas::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    transferQueue_ = info.transferQueue;
    sdfPath_ = info.sdfPath;
    config_ = info.config;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDFAtlas requires raiiDevice");
        return false;
    }

    if (!createAtlasTexture()) return false;
    if (!createBuffers()) return false;

    SDL_Log("SDFAtlas initialized: %u³ resolution, max %u entries (~%zuMB)",
            config_.resolution, config_.maxAtlasEntries,
            config_.estimateMemoryMB(config_.maxAtlasEntries));

    return true;
}

void SDFAtlas::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    sampler_.reset();

    if (atlasView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, atlasView_, nullptr);
        atlasView_ = VK_NULL_HANDLE;
    }
    if (atlasImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, atlasImage_, atlasAllocation_);
        atlasImage_ = VK_NULL_HANDLE;
    }

    if (entryBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, entryBuffer_, entryAllocation_);
        entryBuffer_ = VK_NULL_HANDLE;
    }

    if (instanceBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, instanceBuffer_, instanceAllocation_);
        instanceBuffer_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}

bool SDFAtlas::createAtlasTexture() {
    uint32_t res = config_.resolution;
    uint32_t layers = config_.maxAtlasEntries;

    // Create 3D texture array (2D array with depth = resolution)
    // Using VK_IMAGE_TYPE_3D doesn't support array layers, so we use
    // a texture array where each layer is a 2D slice of the 3D SDF
    // Actually, for proper 3D SDF we need VK_IMAGE_TYPE_3D
    // We'll use a 3D texture and store multiple SDFs by offsetting in Z

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e3D)
        .setFormat(vk::Format::eR16Sfloat)
        .setExtent(vk::Extent3D{res, res, res * layers})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &atlasImage_, &atlasAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF atlas image");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(atlasImage_)
        .setViewType(vk::ImageViewType::e3D)
        .setFormat(vk::Format::eR16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                          nullptr, &atlasView_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF atlas view");
        return false;
    }

    // Create sampler with trilinear filtering
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxLod(0.0f);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF sampler: %s", e.what());
        return false;
    }

    // Transition to shader read layout
    {
        CommandScope cmdScope(device_, commandPool_, transferQueue_);
        if (!cmdScope.begin()) return false;

        Barriers::transitionImage(cmdScope.get(), atlasImage_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, VK_ACCESS_SHADER_READ_BIT);

        if (!cmdScope.end()) return false;
    }

    return true;
}

bool SDFAtlas::createBuffers() {
    // Entry metadata buffer
    VkBufferCreateInfo entryBufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    entryBufferInfo.size = sizeof(SDFEntry) * config_.maxAtlasEntries;
    entryBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator_, &entryBufferInfo, &allocInfo,
                        &entryBuffer_, &entryAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF entry buffer");
        return false;
    }

    // Instance buffer
    VkBufferCreateInfo instanceBufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    instanceBufferInfo.size = sizeof(SDFInstance) * maxInstances_;
    instanceBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vmaCreateBuffer(allocator_, &instanceBufferInfo, &allocInfo,
                        &instanceBuffer_, &instanceAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SDF instance buffer");
        return false;
    }

    entries_.reserve(config_.maxAtlasEntries);

    return true;
}

int SDFAtlas::loadSDF(const std::string& meshName) {
    // Check if already loaded
    auto it = meshToEntry_.find(meshName);
    if (it != meshToEntry_.end()) {
        return it->second;
    }

    // Check capacity
    if (nextLayerIndex_ >= config_.maxAtlasEntries) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDF atlas full, cannot load: %s", meshName.c_str());
        return -1;
    }

    // Load SDF file
    std::string filePath = sdfPath_ + "/" + meshName + ".sdf";
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDF file not found: %s", filePath.c_str());
        return -1;
    }

    size_t fileSize = file.tellg();
    size_t expectedSize = config_.resolution * config_.resolution * config_.resolution * sizeof(uint16_t);
    if (fileSize != expectedSize) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDF file size mismatch for %s: got %zu, expected %zu",
                     meshName.c_str(), fileSize, expectedSize);
        return -1;
    }

    file.seekg(0);
    std::vector<uint16_t> sdfData(config_.resolution * config_.resolution * config_.resolution);
    file.read(reinterpret_cast<char*>(sdfData.data()), fileSize);
    file.close();

    // Upload to GPU
    if (!uploadSDFData(nextLayerIndex_, sdfData.data(), fileSize)) {
        return -1;
    }

    // Create entry
    SDFEntry entry{};
    entry.boundsMin = glm::vec4(-1.0f, -1.0f, -1.0f, 0.0f);  // Will be set by mesh bounds
    entry.boundsMax = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    entry.invScale = glm::vec4(0.5f, 0.5f, 0.5f, static_cast<float>(nextLayerIndex_));
    entry.worldToLocal = glm::mat4(1.0f);

    int entryIndex = static_cast<int>(entries_.size());
    entries_.push_back(entry);

    // Update entry buffer
    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, entryAllocation_, &mapped) == VK_SUCCESS) {
        memcpy(static_cast<SDFEntry*>(mapped) + entryIndex, &entry, sizeof(SDFEntry));
        vmaUnmapMemory(allocator_, entryAllocation_);
    }

    meshToEntry_[meshName] = entryIndex;
    nextLayerIndex_++;

    SDL_Log("Loaded SDF: %s (entry %d)", meshName.c_str(), entryIndex);
    return entryIndex;
}

int SDFAtlas::getEntryIndex(const std::string& meshName) const {
    auto it = meshToEntry_.find(meshName);
    return (it != meshToEntry_.end()) ? it->second : -1;
}

void SDFAtlas::updateInstances(const std::vector<SDFInstance>& instances) {
    instanceCount_ = static_cast<uint32_t>(std::min(instances.size(), static_cast<size_t>(maxInstances_)));

    if (instanceCount_ == 0) return;

    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, instanceAllocation_, &mapped) == VK_SUCCESS) {
        memcpy(mapped, instances.data(), instanceCount_ * sizeof(SDFInstance));
        vmaUnmapMemory(allocator_, instanceAllocation_);
    }
}

bool SDFAtlas::uploadSDFData(uint32_t layerIndex, const void* data, size_t dataSize) {
    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingInfo.size = dataSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Copy data to staging
    void* mapped = nullptr;
    vmaMapMemory(allocator_, stagingAllocation, &mapped);
    memcpy(mapped, data, dataSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Copy to atlas texture
    {
        CommandScope cmdScope(device_, commandPool_, transferQueue_);
        if (!cmdScope.begin()) {
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            return false;
        }

        // Transition to transfer dst
        Barriers::transitionImage(cmdScope.get(), atlasImage_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        // Copy buffer to image layer
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, static_cast<int32_t>(layerIndex * config_.resolution)};
        region.imageExtent = {config_.resolution, config_.resolution, config_.resolution};

        vkCmdCopyBufferToImage(cmdScope.get(), stagingBuffer, atlasImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition back to shader read
        Barriers::transitionImage(cmdScope.get(), atlasImage_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        if (!cmdScope.end()) {
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            return false;
        }
    }

    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
    return true;
}
