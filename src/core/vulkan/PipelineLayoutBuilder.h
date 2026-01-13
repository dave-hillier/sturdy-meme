#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <optional>
#include <SDL3/SDL_log.h>

/**
 * PipelineLayoutBuilder - Fluent builder for creating Vulkan pipeline layouts
 *
 * Simplifies the common pattern of creating pipeline layouts with descriptor
 * set layouts and push constant ranges.
 *
 * Example usage:
 *   auto layout = PipelineLayoutBuilder(device)
 *       .addDescriptorSetLayout(**myDescSetLayout)
 *       .addPushConstantRange<MyPushConstants>(vk::ShaderStageFlagBits::eFragment)
 *       .build();
 *
 *   // Or for compute pipelines:
 *   auto layout = PipelineLayoutBuilder(device)
 *       .addDescriptorSetLayout(**computeDescSetLayout)
 *       .addPushConstantRange<ComputePushConstants>(vk::ShaderStageFlagBits::eCompute)
 *       .build();
 */
class PipelineLayoutBuilder {
public:
    explicit PipelineLayoutBuilder(const vk::raii::Device& device)
        : device_(&device) {}

    // Add a descriptor set layout at the next set index
    PipelineLayoutBuilder& addDescriptorSetLayout(vk::DescriptorSetLayout layout) {
        setLayouts_.push_back(layout);
        return *this;
    }

    // Add multiple descriptor set layouts
    PipelineLayoutBuilder& addDescriptorSetLayouts(
            const std::vector<vk::DescriptorSetLayout>& layouts) {
        setLayouts_.insert(setLayouts_.end(), layouts.begin(), layouts.end());
        return *this;
    }

    // Add a push constant range with explicit size
    PipelineLayoutBuilder& addPushConstantRange(
            vk::ShaderStageFlags stages,
            uint32_t size,
            uint32_t offset = 0) {
        pushConstantRanges_.push_back(
            vk::PushConstantRange{}
                .setStageFlags(stages)
                .setOffset(offset)
                .setSize(size)
        );
        return *this;
    }

    // Add a push constant range using sizeof(T)
    template<typename T>
    PipelineLayoutBuilder& addPushConstantRange(
            vk::ShaderStageFlags stages,
            uint32_t offset = 0) {
        return addPushConstantRange(stages, sizeof(T), offset);
    }

    // Build the pipeline layout
    std::optional<vk::raii::PipelineLayout> build() const {
        auto layoutInfo = vk::PipelineLayoutCreateInfo{};

        if (!setLayouts_.empty()) {
            layoutInfo
                .setSetLayoutCount(static_cast<uint32_t>(setLayouts_.size()))
                .setPSetLayouts(setLayouts_.data());
        }

        if (!pushConstantRanges_.empty()) {
            layoutInfo
                .setPushConstantRangeCount(static_cast<uint32_t>(pushConstantRanges_.size()))
                .setPPushConstantRanges(pushConstantRanges_.data());
        }

        try {
            return vk::raii::PipelineLayout(*device_, layoutInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "PipelineLayoutBuilder: Failed to create pipeline layout: %s", e.what());
            return std::nullopt;
        }
    }

    // Build into an optional member (for placement in class members)
    bool buildInto(std::optional<vk::raii::PipelineLayout>& outLayout) const {
        auto result = build();
        if (result) {
            outLayout = std::move(result);
            return true;
        }
        return false;
    }

    // Reset the builder for reuse
    PipelineLayoutBuilder& reset() {
        setLayouts_.clear();
        pushConstantRanges_.clear();
        return *this;
    }

private:
    const vk::raii::Device* device_;
    std::vector<vk::DescriptorSetLayout> setLayouts_;
    std::vector<vk::PushConstantRange> pushConstantRanges_;
};

// ComputePipelineBuilder has been moved to src/core/pipeline/ComputePipelineBuilder.h
// with additional features: shader loading from path, specialization constants, etc.
