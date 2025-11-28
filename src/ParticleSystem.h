#pragma once

#include "SystemLifecycleHelper.h"
#include <vulkan/vulkan.h>
#include <vector>

// Utility helper for particle-style systems that share lifecycle, pipeline, and double-buffer management.
// Prefer composition: systems can embed this helper to centralize common logic while keeping effect-specific code separate.
class ParticleSystem {
public:
    using InitInfo = SystemLifecycleHelper::InitInfo;
    using Hooks = SystemLifecycleHelper::Hooks;

    bool init(const InitInfo& info, const Hooks& hooks, uint32_t bufferSets = 2);
    void destroy(VkDevice device, VmaAllocator allocator);

    void advanceBufferSet();

    uint32_t getComputeBufferSet() const { return computeBufferSet; }
    uint32_t getRenderBufferSet() const { return renderBufferSet; }
    uint32_t getBufferSetCount() const { return bufferSetCount; }

    void setComputeDescriptorSet(uint32_t index, VkDescriptorSet set);
    void setGraphicsDescriptorSet(uint32_t index, VkDescriptorSet set);

    VkDescriptorSet getComputeDescriptorSet(uint32_t index) const { return computeDescriptorSets[index]; }
    VkDescriptorSet getGraphicsDescriptorSet(uint32_t index) const { return graphicsDescriptorSets[index]; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return lifecycle.getComputePipeline(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return lifecycle.getGraphicsPipeline(); }

    VkDevice getDevice() const { return lifecycle.getDevice(); }
    VmaAllocator getAllocator() const { return lifecycle.getAllocator(); }
    VkRenderPass getRenderPass() const { return lifecycle.getRenderPass(); }
    VkDescriptorPool getDescriptorPool() const { return lifecycle.getDescriptorPool(); }
    const VkExtent2D& getExtent() const { return lifecycle.getExtent(); }
    const std::string& getShaderPath() const { return lifecycle.getShaderPath(); }
    uint32_t getFramesInFlight() const { return lifecycle.getFramesInFlight(); }

private:
    SystemLifecycleHelper lifecycle;
    uint32_t bufferSetCount = 0;
    uint32_t computeBufferSet = 0;
    uint32_t renderBufferSet = 0;
    std::vector<VkDescriptorSet> computeDescriptorSets;
    std::vector<VkDescriptorSet> graphicsDescriptorSets;
};

