#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <stdexcept>

// ============================================================================
// DescriptorLayoutBuilder Implementation
// ============================================================================

DescriptorManager::DescriptorLayoutBuilder::DescriptorLayoutBuilder(VkDevice device)
    : device(device) {}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addUniformBuffer(
    VkShaderStageFlags stages, uint32_t count, std::optional<uint32_t> binding) const {
    return addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addDynamicUniformBuffer(
    VkShaderStageFlags stages, uint32_t count, std::optional<uint32_t> binding) const {
    return addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addStorageBuffer(
    VkShaderStageFlags stages, uint32_t count, std::optional<uint32_t> binding) const {
    return addBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addCombinedImageSampler(
    VkShaderStageFlags stages, uint32_t count, std::optional<uint32_t> binding) const {
    return addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addStorageImage(
    VkShaderStageFlags stages, uint32_t count, std::optional<uint32_t> binding) const {
    return addBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addBinding(
    uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages, uint32_t count) const {
    return addBinding(type, stages, count, binding);
}

DescriptorManager::DescriptorLayoutBuilder DescriptorManager::DescriptorLayoutBuilder::addBinding(
    VkDescriptorType type, VkShaderStageFlags stages, uint32_t count,
    std::optional<uint32_t> binding) const {

    DescriptorLayoutBuilder next = *this;
    uint32_t resolvedBinding = binding.value_or(next.nextBinding);

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = resolvedBinding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stages;
    layoutBinding.pImmutableSamplers = nullptr;

    next.bindings.push_back(layoutBinding);

    if (resolvedBinding >= next.nextBinding) {
        next.nextBinding = resolvedBinding + 1;
    }

    return next;
}

VkDescriptorSetLayout DescriptorManager::DescriptorLayoutBuilder::build() const {
    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindingCount(static_cast<uint32_t>(bindings.size()))
        .setPBindings(reinterpret_cast<const vk::DescriptorSetLayoutBinding*>(bindings.data()));

    vk::Device vkDevice(device);
    vk::DescriptorSetLayout layout = vkDevice.createDescriptorSetLayout(layoutInfo);
    return layout;
}

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
    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindingCount(static_cast<uint32_t>(bindings.size()))
        .setPBindings(reinterpret_cast<const vk::DescriptorSetLayoutBinding*>(bindings.data()));

    vk::Device vkDevice(device);
    vk::DescriptorSetLayout layout = vkDevice.createDescriptorSetLayout(layoutInfo);
    return layout;
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

    writes.push_back(*reinterpret_cast<const VkWriteDescriptorSet*>(&write));
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

    writes.push_back(*reinterpret_cast<const VkWriteDescriptorSet*>(&write));
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

    writes.push_back(*reinterpret_cast<const VkWriteDescriptorSet*>(&write));
    return *this;
}

void DescriptorManager::SetWriter::update() {
    if (!writes.empty()) {
        vk::Device vkDevice(device);
        vkDevice.updateDescriptorSets(
            static_cast<uint32_t>(writes.size()),
            reinterpret_cast<const vk::WriteDescriptorSet*>(writes.data()),
            0, nullptr);
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
    std::vector<VkDescriptorPoolSize> sizes;

    if (poolSizes.uniformBuffers > 0) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        size.descriptorCount = poolSizes.uniformBuffers * setsPerPool;
        sizes.push_back(size);
    }
    if (poolSizes.storageBuffers > 0) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        size.descriptorCount = poolSizes.storageBuffers * setsPerPool;
        sizes.push_back(size);
    }
    if (poolSizes.combinedImageSamplers > 0) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = poolSizes.combinedImageSamplers * setsPerPool;
        sizes.push_back(size);
    }
    if (poolSizes.storageImages > 0) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        size.descriptorCount = poolSizes.storageImages * setsPerPool;
        sizes.push_back(size);
    }

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setPoolSizeCount(static_cast<uint32_t>(sizes.size()))
        .setPPoolSizes(reinterpret_cast<const vk::DescriptorPoolSize*>(sizes.data()))
        .setMaxSets(setsPerPool);

    vk::Device vkDevice(device);
    vk::DescriptorPool pool = vkDevice.createDescriptorPool(poolInfo);

    SDL_Log("DescriptorManager: Created new descriptor pool (total: %zu)",
            pools.size() + 1);
    return pool;
}

bool DescriptorManager::Pool::tryAllocate(VkDescriptorPool pool,
                                          VkDescriptorSetLayout layout,
                                          uint32_t count,
                                          std::vector<VkDescriptorSet>& outSets) {
    std::vector<VkDescriptorSetLayout> layouts(count, layout);

    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(pool)
        .setDescriptorSetCount(count)
        .setPSetLayouts(reinterpret_cast<const vk::DescriptorSetLayout*>(layouts.data()));

    vk::Device vkDevice(device);
    outSets.resize(count);

    try {
        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        for (size_t i = 0; i < sets.size(); ++i) {
            outSets[i] = sets[i];
        }
        return true;
    } catch (const vk::OutOfPoolMemoryError&) {
        return false;
    } catch (const vk::FragmentedPoolError&) {
        return false;
    }
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
        .setSetLayoutCount(static_cast<uint32_t>(setLayouts.size()))
        .setPSetLayouts(reinterpret_cast<const vk::DescriptorSetLayout*>(setLayouts.data()))
        .setPushConstantRangeCount(static_cast<uint32_t>(pushConstants.size()))
        .setPPushConstantRanges(reinterpret_cast<const vk::PushConstantRange*>(pushConstants.data()));

    vk::Device vkDevice(device);
    vk::PipelineLayout layout = vkDevice.createPipelineLayout(layoutInfo);
    return layout;
}

VkPipelineLayout DescriptorManager::createPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    const std::vector<VkPushConstantRange>& pushConstants) {
    return createPipelineLayout(device, std::vector<VkDescriptorSetLayout>{setLayout}, pushConstants);
}
