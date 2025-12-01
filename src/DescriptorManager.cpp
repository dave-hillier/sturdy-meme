#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <stdexcept>

// ============================================================================
// LayoutBuilder Implementation
// ============================================================================

DescriptorManager::LayoutBuilder::LayoutBuilder(VkDevice device)
    : device(device) {}

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addUniformBuffer(
    VkShaderStageFlags stages, uint32_t count) {
    return addBinding(nextBinding++, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stages, count);
}

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addStorageBuffer(
    VkShaderStageFlags stages, uint32_t count) {
    return addBinding(nextBinding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages, count);
}

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addCombinedImageSampler(
    VkShaderStageFlags stages, uint32_t count) {
    return addBinding(nextBinding++, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stages, count);
}

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addStorageImage(
    VkShaderStageFlags stages, uint32_t count) {
    return addBinding(nextBinding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stages, count);
}

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addBinding(
    uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages, uint32_t count) {

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stages;
    layoutBinding.pImmutableSamplers = nullptr;

    bindings.push_back(layoutBinding);

    // Update nextBinding if manually specified binding is higher
    if (binding >= nextBinding) {
        nextBinding = binding + 1;
    }

    return *this;
}

VkDescriptorSetLayout DescriptorManager::LayoutBuilder::build() {
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        SDL_Log("DescriptorManager: Failed to create descriptor set layout");
        return VK_NULL_HANDLE;
    }

    return layout;
}

// ============================================================================
// SetWriter Implementation
// ============================================================================

DescriptorManager::SetWriter::SetWriter(VkDevice device, VkDescriptorSet set)
    : device(device), set(set) {
    // Reserve space to avoid reallocation invalidating pointers
    bufferInfos.reserve(16);
    imageInfos.reserve(16);
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeBuffer(
    uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
    VkDescriptorType type) {
    return writeBufferArray(binding, 0, buffer, offset, range, type);
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeBufferArray(
    uint32_t binding, uint32_t arrayElement, VkBuffer buffer,
    VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type) {

    bufferInfos.push_back({buffer, offset, range});

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfos.back();

    writes.push_back(write);
    return *this;
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeImage(
    uint32_t binding, VkImageView view, VkSampler sampler,
    VkImageLayout layout, VkDescriptorType type) {
    return writeImageArray(binding, 0, view, sampler, layout, type);
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeImageArray(
    uint32_t binding, uint32_t arrayElement, VkImageView view, VkSampler sampler,
    VkImageLayout layout, VkDescriptorType type) {

    imageInfos.push_back({sampler, view, layout});

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfos.back();

    writes.push_back(write);
    return *this;
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeStorageImage(
    uint32_t binding, VkImageView view, VkImageLayout layout) {

    imageInfos.push_back({VK_NULL_HANDLE, view, layout});

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfos.back();

    writes.push_back(write);
    return *this;
}

void DescriptorManager::SetWriter::update() {
    if (!writes.empty()) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

// ============================================================================
// Pool Implementation
// ============================================================================

DescriptorManager::Pool::Pool(VkDevice device, uint32_t initialSetsPerPool)
    : device(device), setsPerPool(initialSetsPerPool) {
    // Create initial pool
    pools.push_back(createPool());
}

DescriptorManager::Pool::~Pool() {
    destroy();
}

DescriptorManager::Pool::Pool(Pool&& other) noexcept
    : device(other.device)
    , pools(std::move(other.pools))
    , setsPerPool(other.setsPerPool)
    , currentPoolIndex(other.currentPoolIndex)
    , totalAllocatedSets(other.totalAllocatedSets)
    , poolSizes(other.poolSizes) {
    other.device = VK_NULL_HANDLE;
}

DescriptorManager::Pool& DescriptorManager::Pool::operator=(Pool&& other) noexcept {
    if (this != &other) {
        destroy();
        device = other.device;
        pools = std::move(other.pools);
        setsPerPool = other.setsPerPool;
        currentPoolIndex = other.currentPoolIndex;
        totalAllocatedSets = other.totalAllocatedSets;
        poolSizes = other.poolSizes;
        other.device = VK_NULL_HANDLE;
    }
    return *this;
}

VkDescriptorPool DescriptorManager::Pool::createPool() {
    std::vector<VkDescriptorPoolSize> sizes;

    if (poolSizes.uniformBuffers > 0) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         poolSizes.uniformBuffers * setsPerPool});
    }
    if (poolSizes.storageBuffers > 0) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                         poolSizes.storageBuffers * setsPerPool});
    }
    if (poolSizes.combinedImageSamplers > 0) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         poolSizes.combinedImageSamplers * setsPerPool});
    }
    if (poolSizes.storageImages > 0) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                         poolSizes.storageImages * setsPerPool});
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    poolInfo.maxSets = setsPerPool;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        SDL_Log("DescriptorManager: Failed to create descriptor pool");
        return VK_NULL_HANDLE;
    }

    SDL_Log("DescriptorManager: Created new descriptor pool (total: %zu)",
            pools.size() + 1);
    return pool;
}

bool DescriptorManager::Pool::tryAllocate(VkDescriptorPool pool,
                                          VkDescriptorSetLayout layout,
                                          uint32_t count,
                                          std::vector<VkDescriptorSet>& outSets) {
    std::vector<VkDescriptorSetLayout> layouts(count, layout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    outSets.resize(count);
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, outSets.data());

    return result == VK_SUCCESS;
}

std::vector<VkDescriptorSet> DescriptorManager::Pool::allocate(
    VkDescriptorSetLayout layout, uint32_t count) {

    std::vector<VkDescriptorSet> sets;

    // Try current pool first
    if (tryAllocate(pools[currentPoolIndex], layout, count, sets)) {
        totalAllocatedSets += count;
        return sets;
    }

    // Try remaining pools
    for (uint32_t i = 0; i < pools.size(); ++i) {
        if (i != currentPoolIndex && tryAllocate(pools[i], layout, count, sets)) {
            currentPoolIndex = i;
            totalAllocatedSets += count;
            return sets;
        }
    }

    // All pools exhausted - create new one
    VkDescriptorPool newPool = createPool();
    if (newPool == VK_NULL_HANDLE) {
        SDL_Log("DescriptorManager: Failed to create new pool for allocation");
        return {};
    }

    pools.push_back(newPool);
    currentPoolIndex = static_cast<uint32_t>(pools.size() - 1);

    if (tryAllocate(newPool, layout, count, sets)) {
        totalAllocatedSets += count;
        return sets;
    }

    SDL_Log("DescriptorManager: Failed to allocate from new pool");
    return {};
}

VkDescriptorSet DescriptorManager::Pool::allocateSingle(VkDescriptorSetLayout layout) {
    auto sets = allocate(layout, 1);
    return sets.empty() ? VK_NULL_HANDLE : sets[0];
}

void DescriptorManager::Pool::reset() {
    for (auto pool : pools) {
        vkResetDescriptorPool(device, pool, 0);
    }
    currentPoolIndex = 0;
    totalAllocatedSets = 0;
}

void DescriptorManager::Pool::destroy() {
    if (device != VK_NULL_HANDLE) {
        for (auto pool : pools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, pool, nullptr);
            }
        }
    }
    pools.clear();
    currentPoolIndex = 0;
    totalAllocatedSets = 0;
}

// ============================================================================
// Static Helpers
// ============================================================================

VkPipelineLayout DescriptorManager::createPipelineLayout(
    VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    const std::vector<VkPushConstantRange>& pushConstants) {

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    layoutInfo.pPushConstantRanges = pushConstants.empty() ? nullptr : pushConstants.data();

    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        SDL_Log("DescriptorManager: Failed to create pipeline layout");
        return VK_NULL_HANDLE;
    }

    return layout;
}

VkPipelineLayout DescriptorManager::createPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    const std::vector<VkPushConstantRange>& pushConstants) {
    return createPipelineLayout(device, std::vector<VkDescriptorSetLayout>{setLayout}, pushConstants);
}
