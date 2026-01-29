#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <SDL3/SDL_log.h>

/**
 * WriteBuilder - Immutable builder for a single vk::WriteDescriptorSet
 *
 * Each setter returns a new copy, allowing stereotypes to be reused.
 *
 * Example:
 *   auto write = WriteBuilder::uniformBuffer(0, bufferInfo);
 *   auto write2 = WriteBuilder::combinedImageSampler(1, imageInfo);
 */
class WriteBuilder {
public:
    constexpr WriteBuilder() = default;

    // ========================================================================
    // Setters (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] WriteBuilder binding(uint32_t b) const {
        WriteBuilder copy = *this;
        copy.binding_ = b;
        return copy;
    }

    [[nodiscard]] WriteBuilder descriptorType(vk::DescriptorType type) const {
        WriteBuilder copy = *this;
        copy.descriptorType_ = type;
        return copy;
    }

    [[nodiscard]] WriteBuilder bufferInfo(const vk::DescriptorBufferInfo& info) const {
        WriteBuilder copy = *this;
        copy.bufferInfo_ = info;
        copy.hasBuffer_ = true;
        copy.hasImage_ = false;
        return copy;
    }

    [[nodiscard]] WriteBuilder imageInfo(const vk::DescriptorImageInfo& info) const {
        WriteBuilder copy = *this;
        copy.imageInfo_ = info;
        copy.hasImage_ = true;
        copy.hasBuffer_ = false;
        return copy;
    }

    [[nodiscard]] WriteBuilder arrayElement(uint32_t element) const {
        WriteBuilder copy = *this;
        copy.arrayElement_ = element;
        return copy;
    }

    [[nodiscard]] WriteBuilder descriptorCount(uint32_t count) const {
        WriteBuilder copy = *this;
        copy.descriptorCount_ = count;
        return copy;
    }

    // ========================================================================
    // Stereotypes - predefined common write configurations
    // ========================================================================

    // Uniform buffer write
    static WriteBuilder uniformBuffer(uint32_t bindingIdx, const vk::DescriptorBufferInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eUniformBuffer)
            .bufferInfo(info);
    }

    // Dynamic uniform buffer write
    static WriteBuilder uniformBufferDynamic(uint32_t bindingIdx, const vk::DescriptorBufferInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .bufferInfo(info);
    }

    // Storage buffer write
    static WriteBuilder storageBuffer(uint32_t bindingIdx, const vk::DescriptorBufferInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eStorageBuffer)
            .bufferInfo(info);
    }

    // Combined image sampler write
    static WriteBuilder combinedImageSampler(uint32_t bindingIdx, const vk::DescriptorImageInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eCombinedImageSampler)
            .imageInfo(info);
    }

    // Storage image write
    static WriteBuilder storageImage(uint32_t bindingIdx, const vk::DescriptorImageInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eStorageImage)
            .imageInfo(info);
    }

    // Sampled image write (for separate sampler pattern)
    static WriteBuilder sampledImage(uint32_t bindingIdx, const vk::DescriptorImageInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eSampledImage)
            .imageInfo(info);
    }

    // Input attachment write
    static WriteBuilder inputAttachment(uint32_t bindingIdx, const vk::DescriptorImageInfo& info) {
        return WriteBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eInputAttachment)
            .imageInfo(info);
    }

    // ========================================================================
    // Build method - creates WriteDescriptorSet for a specific destination set
    // ========================================================================

    [[nodiscard]] vk::WriteDescriptorSet build(vk::DescriptorSet dstSet) const {
        auto write = vk::WriteDescriptorSet{}
            .setDstSet(dstSet)
            .setDstBinding(binding_)
            .setDstArrayElement(arrayElement_)
            .setDescriptorType(descriptorType_)
            .setDescriptorCount(descriptorCount_);

        // Note: Caller must ensure info structs outlive the WriteDescriptorSet
        // The build() method stores pointers to the internal info structs
        if (hasBuffer_) {
            write.setPBufferInfo(&bufferInfo_);
        } else if (hasImage_) {
            write.setPImageInfo(&imageInfo_);
        }

        return write;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] uint32_t getBinding() const { return binding_; }
    [[nodiscard]] vk::DescriptorType getDescriptorType() const { return descriptorType_; }
    [[nodiscard]] bool hasBufferInfo() const { return hasBuffer_; }
    [[nodiscard]] bool hasImageInfo() const { return hasImage_; }
    [[nodiscard]] const vk::DescriptorBufferInfo& getBufferInfo() const { return bufferInfo_; }
    [[nodiscard]] const vk::DescriptorImageInfo& getImageInfo() const { return imageInfo_; }

private:
    uint32_t binding_ = 0;
    uint32_t arrayElement_ = 0;
    uint32_t descriptorCount_ = 1;
    vk::DescriptorType descriptorType_ = vk::DescriptorType::eUniformBuffer;
    vk::DescriptorBufferInfo bufferInfo_;
    vk::DescriptorImageInfo imageInfo_;
    bool hasBuffer_ = false;
    bool hasImage_ = false;
};

/**
 * DescriptorWriter - Collects and applies descriptor set writes
 *
 * Designed to work with WriteBuilder stereotypes. Stores WriteBuilder instances
 * (not raw WriteDescriptorSets) so the info structs remain valid until update.
 *
 * Example usage:
 *   DescriptorWriter writer;
 *   writer.add(WriteBuilder::uniformBuffer(0, uboInfo))
 *         .add(WriteBuilder::combinedImageSampler(1, textureInfo))
 *         .add(WriteBuilder::storageBuffer(2, ssboInfo))
 *         .update(device, descriptorSet);
 *
 *   // Or for batch updates to multiple sets:
 *   writer.clear()
 *         .add(WriteBuilder::uniformBuffer(0, uboInfo))
 *         .updateMultiple(device, {set1, set2, set3});
 */
class DescriptorWriter {
public:
    DescriptorWriter() = default;

    // ========================================================================
    // Builder methods (mutable for convenience in building write lists)
    // ========================================================================

    // Add a write to the pending list
    DescriptorWriter& add(const WriteBuilder& write) {
        writes_.push_back(write);
        return *this;
    }

    // Add multiple writes
    DescriptorWriter& add(std::initializer_list<WriteBuilder> writes) {
        for (const auto& w : writes) {
            writes_.push_back(w);
        }
        return *this;
    }

    // Clear all pending writes
    DescriptorWriter& clear() {
        writes_.clear();
        return *this;
    }

    // ========================================================================
    // Update methods
    // ========================================================================

    // Update a single descriptor set with all pending writes
    void update(const vk::raii::Device& device, vk::DescriptorSet dstSet) const {
        if (writes_.empty()) return;

        // Build WriteDescriptorSets with the destination set
        std::vector<vk::WriteDescriptorSet> vkWrites;
        vkWrites.reserve(writes_.size());

        for (const auto& write : writes_) {
            vkWrites.push_back(write.build(dstSet));
        }

        device.updateDescriptorSets(vkWrites, nullptr);
    }

    // Update a single descriptor set (raw device handle)
    void update(vk::Device device, vk::DescriptorSet dstSet) const {
        if (writes_.empty()) return;

        std::vector<vk::WriteDescriptorSet> vkWrites;
        vkWrites.reserve(writes_.size());

        for (const auto& write : writes_) {
            vkWrites.push_back(write.build(dstSet));
        }

        device.updateDescriptorSets(vkWrites, nullptr);
    }

    // Update multiple descriptor sets with the same writes
    void updateMultiple(const vk::raii::Device& device,
                        const std::vector<vk::DescriptorSet>& dstSets) const {
        for (const auto& dstSet : dstSets) {
            update(device, dstSet);
        }
    }

    // Update descriptor sets at specific frame indices
    template<typename Container>
    void updateFrames(const vk::raii::Device& device,
                      const Container& dstSets,
                      uint32_t startFrame,
                      uint32_t frameCount) const {
        for (uint32_t i = startFrame; i < startFrame + frameCount && i < dstSets.size(); ++i) {
            update(device, dstSets[i]);
        }
    }

    // ========================================================================
    // Convenience: create common write patterns
    // ========================================================================

    // Add UBO write at binding 0 (very common pattern)
    DescriptorWriter& addUBO(const vk::DescriptorBufferInfo& info, uint32_t binding = 0) {
        return add(WriteBuilder::uniformBuffer(binding, info));
    }

    // Add texture write
    DescriptorWriter& addTexture(const vk::DescriptorImageInfo& info, uint32_t binding) {
        return add(WriteBuilder::combinedImageSampler(binding, info));
    }

    // Add storage buffer write
    DescriptorWriter& addSSBO(const vk::DescriptorBufferInfo& info, uint32_t binding) {
        return add(WriteBuilder::storageBuffer(binding, info));
    }

    // Add storage image write
    DescriptorWriter& addStorageImage(const vk::DescriptorImageInfo& info, uint32_t binding) {
        return add(WriteBuilder::storageImage(binding, info));
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] size_t size() const { return writes_.size(); }
    [[nodiscard]] bool empty() const { return writes_.empty(); }
    [[nodiscard]] const std::vector<WriteBuilder>& getWrites() const { return writes_; }

private:
    std::vector<WriteBuilder> writes_;
};

/**
 * Helper function to create common DescriptorImageInfo
 */
inline vk::DescriptorImageInfo makeImageInfo(
        vk::Sampler sampler,
        vk::ImageView view,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) {
    return vk::DescriptorImageInfo{}
        .setSampler(sampler)
        .setImageView(view)
        .setImageLayout(layout);
}

/**
 * Helper function to create common DescriptorBufferInfo
 */
inline vk::DescriptorBufferInfo makeBufferInfo(
        vk::Buffer buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = VK_WHOLE_SIZE) {
    return vk::DescriptorBufferInfo{}
        .setBuffer(buffer)
        .setOffset(offset)
        .setRange(range);
}

/**
 * Helper function for storage image info (no sampler needed)
 */
inline vk::DescriptorImageInfo makeStorageImageInfo(
        vk::ImageView view,
        vk::ImageLayout layout = vk::ImageLayout::eGeneral) {
    return vk::DescriptorImageInfo{}
        .setImageView(view)
        .setImageLayout(layout);
}
