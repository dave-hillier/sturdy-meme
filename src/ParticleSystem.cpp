#include "ParticleSystem.h"
#include <SDL3/SDL.h>

bool ParticleSystem::init(const InitInfo& info, const Hooks& hooks, uint32_t bufferSets) {
    bufferSetCount = bufferSets;
    computeBufferSet = 0;
    renderBufferSet = 0;
    computeDescriptorSets.assign(bufferSetCount, VK_NULL_HANDLE);
    graphicsDescriptorSets.assign(bufferSetCount, VK_NULL_HANDLE);
    return lifecycle.init(info, hooks);
}

void ParticleSystem::destroy(VkDevice device, VmaAllocator allocator) {
    lifecycle.destroy(device, allocator);
    computeDescriptorSets.clear();
    graphicsDescriptorSets.clear();
}

void ParticleSystem::advanceBufferSet() {
    renderBufferSet = computeBufferSet;
    computeBufferSet = (computeBufferSet + 1) % bufferSetCount;
}

void ParticleSystem::setComputeDescriptorSet(uint32_t index, VkDescriptorSet set) {
    if (index >= computeDescriptorSets.size()) return;
    computeDescriptorSets[index] = set;
}

void ParticleSystem::setGraphicsDescriptorSet(uint32_t index, VkDescriptorSet set) {
    if (index >= graphicsDescriptorSets.size()) return;
    graphicsDescriptorSets[index] = set;
}

bool ParticleSystem::createStandardDescriptorSets() {
    // Allocate descriptor sets for both buffer sets using managed pool
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Compute descriptor set
        VkDescriptorSet computeSet = getDescriptorPool()->allocateSingle(
            getComputePipelineHandles().descriptorSetLayout);
        if (computeSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate particle system compute descriptor set (set %u)", set);
            return false;
        }
        setComputeDescriptorSet(set, computeSet);

        // Graphics descriptor set
        VkDescriptorSet graphicsSet = getDescriptorPool()->allocateSingle(
            getGraphicsPipelineHandles().descriptorSetLayout);
        if (graphicsSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate particle system graphics descriptor set (set %u)", set);
            return false;
        }
        setGraphicsDescriptorSet(set, graphicsSet);
    }

    return true;
}

