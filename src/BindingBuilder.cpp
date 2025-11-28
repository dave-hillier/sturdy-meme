#include "BindingBuilder.h"

BindingBuilder::BindingBuilder() {
    descriptorBinding.descriptorCount = 1;
}

BindingBuilder& BindingBuilder::setBinding(uint32_t binding) {
    descriptorBinding.binding = binding;
    return *this;
}

BindingBuilder& BindingBuilder::setDescriptorType(VkDescriptorType type) {
    descriptorBinding.descriptorType = type;
    return *this;
}

BindingBuilder& BindingBuilder::setDescriptorCount(uint32_t count) {
    descriptorBinding.descriptorCount = count;
    return *this;
}

BindingBuilder& BindingBuilder::setStageFlags(VkShaderStageFlags stageFlags) {
    descriptorBinding.stageFlags = stageFlags;
    return *this;
}

BindingBuilder& BindingBuilder::setImmutableSamplers(const VkSampler* immutableSamplers) {
    descriptorBinding.pImmutableSamplers = immutableSamplers;
    return *this;
}

VkDescriptorSetLayoutBinding BindingBuilder::build() const {
    return descriptorBinding;
}

