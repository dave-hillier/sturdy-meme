#pragma once

#include <vulkan/vulkan.h>

// DEPRECATED: Use direct VkDescriptorSetLayoutBinding initialization instead.
// Example:
//   VkDescriptorSetLayoutBinding binding{};
//   binding.binding = 0;
//   binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//   binding.descriptorCount = 1;
//   binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
//
// This class will be removed in a future version.
class [[deprecated("Use direct VkDescriptorSetLayoutBinding initialization")]] BindingBuilder {
public:
    BindingBuilder();

    BindingBuilder& setBinding(uint32_t binding);
    BindingBuilder& setDescriptorType(VkDescriptorType type);
    BindingBuilder& setDescriptorCount(uint32_t count);
    BindingBuilder& setStageFlags(VkShaderStageFlags stageFlags);
    BindingBuilder& setImmutableSamplers(const VkSampler* immutableSamplers);

    VkDescriptorSetLayoutBinding build() const;

private:
    VkDescriptorSetLayoutBinding descriptorBinding{};
};

