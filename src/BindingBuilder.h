#pragma once

#include <vulkan/vulkan.h>

class BindingBuilder {
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

