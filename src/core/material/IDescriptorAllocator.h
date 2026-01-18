#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Interface for allocating descriptor sets
// Allows systems to depend on allocation capability without coupling
// to a specific implementation (e.g., auto-growing pool)
class IDescriptorAllocator {
public:
    virtual ~IDescriptorAllocator() = default;

    // Allocate multiple descriptor sets with the given layout
    virtual std::vector<VkDescriptorSet> allocate(VkDescriptorSetLayout layout, uint32_t count) = 0;

    // Allocate a single descriptor set with the given layout
    virtual VkDescriptorSet allocateSingle(VkDescriptorSetLayout layout) = 0;

    // Reset all allocated sets (returns them to the pool)
    virtual void reset() = 0;
};
