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

struct LeafCullPhase3Stage {
    // Pipeline
    std::optional<vk::raii::Pipeline> pipeline;
    std::optional<vk::raii::PipelineLayout> pipelineLayout;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout;
    std::vector<VkDescriptorSet> descriptorSets;

    // Uniform/params buffers
    BufferUtils::PerFrameBufferSet uniformBuffers;
    BufferUtils::PerFrameBufferSet paramsBuffers;

    bool createPipeline(const vk::raii::Device& raiiDevice, VkDevice device,
                        const std::string& resourcePath);

    bool createDescriptorSets(VkDevice device, VmaAllocator allocator,
                              DescriptorManager::Pool* descriptorPool,
                              uint32_t maxFramesInFlight);

    void destroy(VmaAllocator allocator);

    bool isReady() const { return pipeline.has_value() && !descriptorSets.empty(); }
};
