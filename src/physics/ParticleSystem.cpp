#include "ParticleSystem.h"
#include <SDL3/SDL.h>

bool ParticleSystem::init(const InitInfo& info, const Hooks& hooks, uint32_t bufferSets) {
    bufferSetCount = bufferSets;
    computeBufferSet = 0;
    renderBufferSet = 0;
    computeDescriptorSets.assign(bufferSetCount, VK_NULL_HANDLE);
    graphicsDescriptorSets.assign(bufferSetCount, VK_NULL_HANDLE);
    if (!lifecycle.init(info, hooks)) {
        return false;
    }
    // Allocate standard descriptor sets after hooks complete
    // (hooks can't call createStandardDescriptorSets since ParticleSystem isn't assigned yet in owner)
    return createStandardDescriptorSets();
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
    SDL_Log("ParticleSystem::createStandardDescriptorSets - bufferSetCount=%u, pool=%p",
            bufferSetCount, (void*)getDescriptorPool());
    // Allocate descriptor sets for both buffer sets using managed pool
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        VkDescriptorSetLayout computeLayout = getComputePipelineHandles().descriptorSetLayout;
        SDL_Log("  Allocating set %u compute, layout=%p", set, (void*)computeLayout);
        // Compute descriptor set
        VkDescriptorSet computeSet = getDescriptorPool()->allocateSingle(computeLayout);
        if (computeSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate particle system compute descriptor set (set %u)", set);
            return false;
        }
        setComputeDescriptorSet(set, computeSet);
        SDL_Log("  Allocated set %u compute OK, allocating graphics...", set);

        // Graphics descriptor set
        VkDescriptorSet graphicsSet = getDescriptorPool()->allocateSingle(
            getGraphicsPipelineHandles().descriptorSetLayout);
        if (graphicsSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate particle system graphics descriptor set (set %u)", set);
            return false;
        }
        setGraphicsDescriptorSet(set, graphicsSet);
        SDL_Log("  Allocated set %u graphics OK", set);
    }

    SDL_Log("ParticleSystem::createStandardDescriptorSets - done");
    return true;
}

