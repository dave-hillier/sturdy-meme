#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <vector>
#include <SDL3/SDL_log.h>

/**
 * BindingBuilder - Immutable builder for a single descriptor set layout binding
 *
 * Allows creating bindings with a fluent API that can be customized from stereotypes.
 *
 * Example:
 *   // Using stereotypes
 *   auto ubo = BindingBuilder::uniformBuffer(0, vk::ShaderStageFlagBits::eVertex);
 *   auto tex = BindingBuilder::combinedImageSampler(1, vk::ShaderStageFlagBits::eFragment);
 *
 *   // Customizing from stereotype
 *   auto arrayTex = BindingBuilder::combinedImageSampler(2, vk::ShaderStageFlagBits::eFragment)
 *       .descriptorCount(4);  // texture array
 */
class BindingBuilder {
public:
    constexpr BindingBuilder() = default;

    // ========================================================================
    // Setters (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] constexpr BindingBuilder binding(uint32_t idx) const {
        BindingBuilder copy = *this;
        copy.binding_ = idx;
        return copy;
    }

    [[nodiscard]] constexpr BindingBuilder descriptorType(vk::DescriptorType type) const {
        BindingBuilder copy = *this;
        copy.descriptorType_ = type;
        return copy;
    }

    [[nodiscard]] constexpr BindingBuilder descriptorCount(uint32_t count) const {
        BindingBuilder copy = *this;
        copy.descriptorCount_ = count;
        return copy;
    }

    [[nodiscard]] constexpr BindingBuilder stageFlags(vk::ShaderStageFlags flags) const {
        BindingBuilder copy = *this;
        copy.stageFlags_ = flags;
        return copy;
    }

    [[nodiscard]] constexpr BindingBuilder addStage(vk::ShaderStageFlagBits stage) const {
        BindingBuilder copy = *this;
        copy.stageFlags_ |= stage;
        return copy;
    }

    // ========================================================================
    // Stereotypes - predefined common binding configurations
    // ========================================================================

    // Uniform buffer (UBO) binding
    static constexpr BindingBuilder uniformBuffer(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eUniformBuffer)
            .stageFlags(stages);
    }

    // Dynamic uniform buffer binding
    static constexpr BindingBuilder uniformBufferDynamic(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .stageFlags(stages);
    }

    // Storage buffer (SSBO) binding
    static constexpr BindingBuilder storageBuffer(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eStorageBuffer)
            .stageFlags(stages);
    }

    // Dynamic storage buffer binding
    static constexpr BindingBuilder storageBufferDynamic(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eStorageBufferDynamic)
            .stageFlags(stages);
    }

    // Combined image sampler (texture) binding
    static constexpr BindingBuilder combinedImageSampler(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eCombinedImageSampler)
            .stageFlags(stages);
    }

    // Sampled image (separate sampler) binding
    static constexpr BindingBuilder sampledImage(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eSampledImage)
            .stageFlags(stages);
    }

    // Sampler binding (for separate samplers)
    static constexpr BindingBuilder sampler(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eSampler)
            .stageFlags(stages);
    }

    // Storage image binding
    static constexpr BindingBuilder storageImage(uint32_t bindingIdx, vk::ShaderStageFlags stages) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eStorageImage)
            .stageFlags(stages);
    }

    // Input attachment binding (for subpass inputs)
    static constexpr BindingBuilder inputAttachment(uint32_t bindingIdx, vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eFragment) {
        return BindingBuilder()
            .binding(bindingIdx)
            .descriptorType(vk::DescriptorType::eInputAttachment)
            .stageFlags(stages);
    }

    // ========================================================================
    // Common stage flag shortcuts
    // ========================================================================

    // Commonly used combinations
    static constexpr vk::ShaderStageFlags VertexStage = vk::ShaderStageFlagBits::eVertex;
    static constexpr vk::ShaderStageFlags FragmentStage = vk::ShaderStageFlagBits::eFragment;
    static constexpr vk::ShaderStageFlags ComputeStage = vk::ShaderStageFlagBits::eCompute;
    static constexpr vk::ShaderStageFlags VertexFragment = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    static constexpr vk::ShaderStageFlags AllGraphics = vk::ShaderStageFlagBits::eAllGraphics;

    // ========================================================================
    // Conversion to Vulkan struct
    // ========================================================================

    [[nodiscard]] constexpr vk::DescriptorSetLayoutBinding build() const {
        return vk::DescriptorSetLayoutBinding{}
            .setBinding(binding_)
            .setDescriptorType(descriptorType_)
            .setDescriptorCount(descriptorCount_)
            .setStageFlags(stageFlags_);
    }

    // Implicit conversion for convenience
    [[nodiscard]] constexpr operator vk::DescriptorSetLayoutBinding() const {
        return build();
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] constexpr uint32_t getBinding() const { return binding_; }
    [[nodiscard]] constexpr vk::DescriptorType getDescriptorType() const { return descriptorType_; }
    [[nodiscard]] constexpr uint32_t getDescriptorCount() const { return descriptorCount_; }
    [[nodiscard]] constexpr vk::ShaderStageFlags getStageFlags() const { return stageFlags_; }

private:
    uint32_t binding_ = 0;
    vk::DescriptorType descriptorType_ = vk::DescriptorType::eUniformBuffer;
    uint32_t descriptorCount_ = 1;
    vk::ShaderStageFlags stageFlags_ = vk::ShaderStageFlagBits::eAll;
};

/**
 * DescriptorSetLayoutBuilder - Immutable builder for descriptor set layouts
 *
 * This builder uses an immutable pattern where each addBinding() returns a new
 * builder instance. This allows for creating "stereotypes" that can be extended.
 *
 * Example usage:
 *   // Create from scratch
 *   auto layout = DescriptorSetLayoutBuilder()
 *       .addBinding(BindingBuilder::uniformBuffer(0, vk::ShaderStageFlagBits::eVertex))
 *       .addBinding(BindingBuilder::combinedImageSampler(1, vk::ShaderStageFlagBits::eFragment))
 *       .build(device);
 *
 *   // Using a stereotype as base
 *   static const auto baseComputeLayout = DescriptorSetLayoutBuilder()
 *       .addBinding(BindingBuilder::storageBuffer(0, vk::ShaderStageFlagBits::eCompute))
 *       .addBinding(BindingBuilder::storageBuffer(1, vk::ShaderStageFlagBits::eCompute));
 *
 *   // Extend the stereotype
 *   auto layout = baseComputeLayout
 *       .addBinding(BindingBuilder::uniformBuffer(2, vk::ShaderStageFlagBits::eCompute))
 *       .build(device);
 */
class DescriptorSetLayoutBuilder {
public:
    DescriptorSetLayoutBuilder() = default;

    // ========================================================================
    // Binding adders (return new builder - immutable)
    // ========================================================================

    // Add a pre-configured binding
    [[nodiscard]] DescriptorSetLayoutBuilder addBinding(const BindingBuilder& binding) const {
        DescriptorSetLayoutBuilder copy = *this;
        copy.bindings_.push_back(binding.build());
        return copy;
    }

    // Add a raw vk::DescriptorSetLayoutBinding
    [[nodiscard]] DescriptorSetLayoutBuilder addBinding(const vk::DescriptorSetLayoutBinding& binding) const {
        DescriptorSetLayoutBuilder copy = *this;
        copy.bindings_.push_back(binding);
        return copy;
    }

    // Convenience: add uniform buffer at next binding index
    [[nodiscard]] DescriptorSetLayoutBuilder addUniformBuffer(vk::ShaderStageFlags stages) const {
        uint32_t nextBinding = static_cast<uint32_t>(bindings_.size());
        return addBinding(BindingBuilder::uniformBuffer(nextBinding, stages));
    }

    // Convenience: add storage buffer at next binding index
    [[nodiscard]] DescriptorSetLayoutBuilder addStorageBuffer(vk::ShaderStageFlags stages) const {
        uint32_t nextBinding = static_cast<uint32_t>(bindings_.size());
        return addBinding(BindingBuilder::storageBuffer(nextBinding, stages));
    }

    // Convenience: add combined image sampler at next binding index
    [[nodiscard]] DescriptorSetLayoutBuilder addCombinedImageSampler(vk::ShaderStageFlags stages) const {
        uint32_t nextBinding = static_cast<uint32_t>(bindings_.size());
        return addBinding(BindingBuilder::combinedImageSampler(nextBinding, stages));
    }

    // Convenience: add storage image at next binding index
    [[nodiscard]] DescriptorSetLayoutBuilder addStorageImage(vk::ShaderStageFlags stages) const {
        uint32_t nextBinding = static_cast<uint32_t>(bindings_.size());
        return addBinding(BindingBuilder::storageImage(nextBinding, stages));
    }

    // ========================================================================
    // Layout flags
    // ========================================================================

    [[nodiscard]] DescriptorSetLayoutBuilder flags(vk::DescriptorSetLayoutCreateFlags f) const {
        DescriptorSetLayoutBuilder copy = *this;
        copy.flags_ = f;
        return copy;
    }

    // ========================================================================
    // Stereotypes - common layout patterns
    // ========================================================================

    // Single UBO layout - very common for per-frame data
    static DescriptorSetLayoutBuilder singleUniformBuffer(vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eAllGraphics) {
        return DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::uniformBuffer(0, stages));
    }

    // UBO + texture - common for material rendering
    static DescriptorSetLayoutBuilder uniformBufferWithTexture(
            vk::ShaderStageFlags uboStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            vk::ShaderStageFlags texStages = vk::ShaderStageFlagBits::eFragment) {
        return DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::uniformBuffer(0, uboStages))
            .addBinding(BindingBuilder::combinedImageSampler(1, texStages));
    }

    // Compute with input/output storage buffers
    static DescriptorSetLayoutBuilder computeInOutBuffers() {
        return DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::storageBuffer(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageBuffer(1, vk::ShaderStageFlagBits::eCompute));
    }

    // Compute with UBO + input/output storage buffers
    static DescriptorSetLayoutBuilder computeWithUBOAndBuffers() {
        return DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::uniformBuffer(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageBuffer(1, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageBuffer(2, vk::ShaderStageFlagBits::eCompute));
    }

    // Image processing compute layout
    static DescriptorSetLayoutBuilder computeImageProcessing() {
        return DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::combinedImageSampler(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageImage(1, vk::ShaderStageFlagBits::eCompute));
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] std::optional<vk::raii::DescriptorSetLayout> build(const vk::raii::Device& device) const {
        auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
            .setFlags(flags_)
            .setBindings(bindings_);

        try {
            return vk::raii::DescriptorSetLayout(device, layoutInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "DescriptorSetLayoutBuilder: Failed to create layout: %s", e.what());
            return std::nullopt;
        }
    }

    // Build into an optional member
    bool buildInto(const vk::raii::Device& device, std::optional<vk::raii::DescriptorSetLayout>& outLayout) const {
        auto result = build(device);
        if (result) {
            outLayout = std::move(result);
            return true;
        }
        return false;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] const std::vector<vk::DescriptorSetLayoutBinding>& getBindings() const { return bindings_; }
    [[nodiscard]] size_t getBindingCount() const { return bindings_.size(); }

private:
    std::vector<vk::DescriptorSetLayoutBinding> bindings_;
    vk::DescriptorSetLayoutCreateFlags flags_ = {};
};
