#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <optional>

#include "TreeCullingTypes.h"
#include "DescriptorManager.h"
#include "PerFrameBuffer.h"
#include "FrameIndexedBuffers.h"

class TreeSpatialIndex;

struct TreeFilterStage {
    // Pipeline
    std::optional<vk::raii::Pipeline> pipeline;
    std::optional<vk::raii::PipelineLayout> pipelineLayout;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout;
    std::vector<VkDescriptorSet> descriptorSets;

    // Intermediate buffers
    BufferUtils::FrameIndexedBuffers visibleTreeBuffers;
    VkDeviceSize visibleTreeBufferSize = 0;
    uint32_t maxVisibleTrees = 0;

    BufferUtils::FrameIndexedBuffers leafCullIndirectDispatchBuffers;

    // Uniform/params buffers
    BufferUtils::PerFrameBufferSet uniformBuffers;
    BufferUtils::PerFrameBufferSet paramsBuffers;

    bool createPipeline(const vk::raii::Device& raiiDevice, VkDevice device,
                        const std::string& resourcePath);

    bool createBuffers(VkDevice device, VmaAllocator allocator,
                       DescriptorManager::Pool* descriptorPool,
                       uint32_t maxFramesInFlight, uint32_t maxTrees,
                       const TreeSpatialIndex& spatialIndex,
                       const BufferUtils::FrameIndexedBuffers& treeDataBuffers,
                       const BufferUtils::FrameIndexedBuffers& visibleCellBuffers);

    void updateSpatialIndexDescriptors(VkDevice device, uint32_t maxFramesInFlight,
                                        const TreeSpatialIndex& spatialIndex);

    void updateTreeDataDescriptors(VkDevice device, uint32_t maxFramesInFlight,
                                    const BufferUtils::FrameIndexedBuffers& treeDataBuffers);

    void destroy(VmaAllocator allocator);

    bool isReady() const { return pipeline.has_value() && !descriptorSets.empty(); }
};
