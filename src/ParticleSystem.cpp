#include "ParticleSystem.h"

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

