#include "DescriptorManager.h"
#include "VulkanRAII.h"
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

DescriptorManager::LayoutBuilder& DescriptorManager::LayoutBuilder::addDynamicUniformBuffer(
    VkShaderStageFlags stages, uint32_t count) {
    return addBinding(nextBinding++, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, stages, count);
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

    auto layoutBinding = vk::DescriptorSetLayoutBinding{}
        .setBinding(binding)
        .setDescriptorType(static_cast<vk::DescriptorType>(type))
        .setDescriptorCount(count)
        .setStageFlags(static_cast<vk::ShaderStageFlags>(stages));

    bindings.push_back(layoutBinding);

    // Update nextBinding if manually specified binding is higher
    if (binding >= nextBinding) {
        nextBinding = binding + 1;
    }

    return *this;
}

VkDescriptorSetLayout DescriptorManager::LayoutBuilder::build() {
    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    vk::Device vkDevice(device);
    auto result = vkDevice.createDescriptorSetLayout(layoutInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_Log("DescriptorManager: Failed to create descriptor set layout");
        return VK_NULL_HANDLE;
    }

    return result.value;
}

bool DescriptorManager::LayoutBuilder::buildManaged(ManagedDescriptorSetLayout& outLayout) {
    VkDescriptorSetLayout rawLayout = build();
    if (rawLayout == VK_NULL_HANDLE) {
        return false;
    }
    outLayout = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);
    return true;
}

// ============================================================================
// SetWriter Implementation
// ============================================================================

DescriptorManager::SetWriter::SetWriter(VkDevice device, VkDescriptorSet set)
    : device(device), set(set) {
    // Reserve space to avoid reallocation invalidating pointers
    // Increased to 32 to handle larger descriptor sets with tile cache bindings
    bufferInfos.reserve(32);
    imageInfos.reserve(32);
    writes.reserve(32);  // Also reserve writes vector
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

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(set)
        .setDstBinding(binding)
        .setDstArrayElement(arrayElement)
        .setDescriptorType(static_cast<vk::DescriptorType>(type))
        .setDescriptorCount(1)
        .setPBufferInfo(reinterpret_cast<const vk::DescriptorBufferInfo*>(&bufferInfos.back()));

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

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(set)
        .setDstBinding(binding)
        .setDstArrayElement(arrayElement)
        .setDescriptorType(static_cast<vk::DescriptorType>(type))
        .setDescriptorCount(1)
        .setPImageInfo(reinterpret_cast<const vk::DescriptorImageInfo*>(&imageInfos.back()));

    writes.push_back(write);
    return *this;
}

DescriptorManager::SetWriter& DescriptorManager::SetWriter::writeStorageImage(
    uint32_t binding, VkImageView view, VkImageLayout layout) {

    imageInfos.push_back({VK_NULL_HANDLE, view, layout});

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(set)
        .setDstBinding(binding)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eStorageImage)
        .setDescriptorCount(1)
        .setPImageInfo(reinterpret_cast<const vk::DescriptorImageInfo*>(&imageInfos.back()));

    writes.push_back(write);
    return *this;
}

void DescriptorManager::SetWriter::update() {
    if (!writes.empty()) {
        vk::Device vkDevice(device);
        vkDevice.updateDescriptorSets(writes, nullptr);
    }
}

// ============================================================================
// Pool Implementation
// ============================================================================

DescriptorManager::Pool::Pool(VkDevice device, uint32_t initialSetsPerPool)
    : device(device), setsPerPool(initialSetsPerPool), poolSizes(DescriptorPoolSizes::standard()) {
    // Create initial pool
    pools.push_back(createPool());
}

DescriptorManager::Pool::Pool(VkDevice device, uint32_t initialSetsPerPool, const DescriptorPoolSizes& sizes)
    : device(device), setsPerPool(initialSetsPerPool), poolSizes(sizes) {
    // Create initial pool with custom sizes
    SDL_Log("DescriptorManager: Creating pool with custom sizes (UBO=%u, SSBO=%u, samplers=%u, storage=%u)",
            sizes.uniformBuffers, sizes.storageBuffers, sizes.combinedImageSamplers, sizes.storageImages);
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
    std::vector<vk::DescriptorPoolSize> sizes;

    if (poolSizes.uniformBuffers > 0) {
        sizes.push_back(vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(poolSizes.uniformBuffers * setsPerPool));
    }
    if (poolSizes.storageBuffers > 0) {
        sizes.push_back(vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(poolSizes.storageBuffers * setsPerPool));
    }
    if (poolSizes.combinedImageSamplers > 0) {
        sizes.push_back(vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(poolSizes.combinedImageSamplers * setsPerPool));
    }
    if (poolSizes.storageImages > 0) {
        sizes.push_back(vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(poolSizes.storageImages * setsPerPool));
    }

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setPoolSizes(sizes)
        .setMaxSets(setsPerPool);

    vk::Device vkDevice(device);
    auto result = vkDevice.createDescriptorPool(poolInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_Log("DescriptorManager: Failed to create descriptor pool");
        return VK_NULL_HANDLE;
    }

    SDL_Log("DescriptorManager: Created new descriptor pool (total: %zu)",
            pools.size() + 1);
    return result.value;
}

bool DescriptorManager::Pool::tryAllocate(VkDescriptorPool pool,
                                          VkDescriptorSetLayout layout,
                                          uint32_t count,
                                          std::vector<VkDescriptorSet>& outSets) {
    std::vector<VkDescriptorSetLayout> layouts(count, layout);

    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(pool)
        .setSetLayouts(layouts);

    vk::Device vkDevice(device);
    outSets.resize(count);
    auto result = vkDevice.allocateDescriptorSets(allocInfo);

    if (result.result == vk::Result::eSuccess) {
        std::copy(result.value.begin(), result.value.end(), outSets.begin());
        return true;
    }
    return false;
}

std::vector<VkDescriptorSet> DescriptorManager::Pool::allocate(
    VkDescriptorSetLayout layout, uint32_t count) {

    std::vector<VkDescriptorSet> sets;

    SDL_Log("DescriptorManager::allocate - pools.size()=%zu, currentPoolIndex=%u, device=%p",
            pools.size(), currentPoolIndex, (void*)device);

    // Try current pool first
    if (pools.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DescriptorManager::allocate - pools is empty!");
        return {};
    }
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
    vk::Device vkDevice(device);
    for (auto pool : pools) {
        vkDevice.resetDescriptorPool(pool);
    }
    currentPoolIndex = 0;
    totalAllocatedSets = 0;
}

void DescriptorManager::Pool::destroy() {
    if (device != VK_NULL_HANDLE) {
        vk::Device vkDevice(device);
        for (auto pool : pools) {
            if (pool != VK_NULL_HANDLE) {
                vkDevice.destroyDescriptorPool(pool);
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

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayouts)
        .setPushConstantRanges(pushConstants);

    vk::Device vkDevice(device);
    auto result = vkDevice.createPipelineLayout(layoutInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_Log("DescriptorManager: Failed to create pipeline layout");
        return VK_NULL_HANDLE;
    }

    return result.value;
}

VkPipelineLayout DescriptorManager::createPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    const std::vector<VkPushConstantRange>& pushConstants) {
    return createPipelineLayout(device, std::vector<VkDescriptorSetLayout>{setLayout}, pushConstants);
}

bool DescriptorManager::createManagedPipelineLayout(
    VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    ManagedPipelineLayout& outLayout,
    const std::vector<VkPushConstantRange>& pushConstants) {

    VkPipelineLayout rawLayout = createPipelineLayout(device, setLayouts, pushConstants);
    if (rawLayout == VK_NULL_HANDLE) {
        return false;
    }
    outLayout = ManagedPipelineLayout::fromRaw(device, rawLayout);
    return true;
}

bool DescriptorManager::createManagedPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    ManagedPipelineLayout& outLayout,
    const std::vector<VkPushConstantRange>& pushConstants) {
    return createManagedPipelineLayout(device,
        std::vector<VkDescriptorSetLayout>{setLayout},
        outLayout,
        pushConstants);
}
