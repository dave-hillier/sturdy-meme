#include "TreeFilterStage.h"
#include "TreeSpatialIndex.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL_log.h>

bool TreeFilterStage::createPipeline(const vk::raii::Device& raiiDevice, VkDevice device,
                                      const std::string& resourcePath) {
    DescriptorManager::LayoutBuilder builder(device);
    builder.addBinding(Bindings::TREE_FILTER_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_VISIBLE_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_CELL_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_SORTED_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create descriptor set layout");
        return false;
    }
    descriptorSetLayout.emplace(raiiDevice, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(raiiDevice)
        .addDescriptorSetLayout(**descriptorSetLayout)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create pipeline layout");
        return false;
    }
    pipelineLayout = std::move(layoutOpt);

    if (!ComputePipelineBuilder(raiiDevice)
            .setShader(resourcePath + "/shaders/tree_filter.comp.spv")
            .setPipelineLayout(**pipelineLayout)
            .buildInto(pipeline)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create pipeline");
        return false;
    }

    SDL_Log("TreeFilterStage: Created tree filter compute pipeline");
    return true;
}

bool TreeFilterStage::createBuffers(VkDevice device, VmaAllocator allocator,
                                     DescriptorManager::Pool* descriptorPool,
                                     uint32_t maxFramesInFlight, uint32_t maxTrees,
                                     const TreeSpatialIndex& spatialIndex,
                                     const BufferUtils::FrameIndexedBuffers& treeDataBuffers,
                                     const BufferUtils::FrameIndexedBuffers& visibleCellBuffers) {
    if (!descriptorSetLayout) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Descriptor set layout is null");
        return false;
    }

    maxVisibleTrees = maxTrees;
    visibleTreeBufferSize = sizeof(uint32_t) + maxTrees * sizeof(VisibleTreeData);

    if (!visibleTreeBuffers.resize(
            allocator, maxFramesInFlight, visibleTreeBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create visible tree buffers");
        return false;
    }

    vk::DeviceSize indirectDispatchSize = 3 * sizeof(uint32_t);
    if (!leafCullIndirectDispatchBuffers.resize(
            allocator, maxFramesInFlight, indirectDispatchSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create indirect dispatch buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(uniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(TreeFilterParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(paramsBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to create params buffers");
        return false;
    }

    descriptorSets = descriptorPool->allocate(**descriptorSetLayout, maxFramesInFlight);
    if (descriptorSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeFilterStage: Failed to allocate descriptor sets");
        return false;
    }

    for (uint32_t f = 0; f < maxFramesInFlight; ++f) {
        DescriptorManager::SetWriter writer(device, descriptorSets[f]);
        writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_VISIBLE_CELLS, visibleCellBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex.getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex.getSortedTreeBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_VISIBLE_TREES, visibleTreeBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_INDIRECT, leafCullIndirectDispatchBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_CULLING, uniformBuffers.buffers[f], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_PARAMS, paramsBuffers.buffers[f], 0, sizeof(TreeFilterParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .update();
    }

    SDL_Log("TreeFilterStage: Created buffers (max %u trees, %.2f KB visible tree buffer x %u frames)",
            maxTrees, visibleTreeBufferSize / 1024.0f, maxFramesInFlight);
    return true;
}

void TreeFilterStage::updateSpatialIndexDescriptors(VkDevice device, uint32_t maxFramesInFlight,
                                                      const TreeSpatialIndex& spatialIndex) {
    for (uint32_t f = 0; f < maxFramesInFlight; ++f) {
        DescriptorManager::SetWriter writer(device, descriptorSets[f]);
        writer.writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex.getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex.getSortedTreeBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .update();
    }
}

void TreeFilterStage::updateTreeDataDescriptors(VkDevice device, uint32_t maxFramesInFlight,
                                                  const BufferUtils::FrameIndexedBuffers& treeDataBuffers) {
    for (uint32_t f = 0; f < maxFramesInFlight; ++f) {
        DescriptorManager::SetWriter writer(device, descriptorSets[f]);
        writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .update();
    }
}

void TreeFilterStage::destroy(VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, paramsBuffers);
}
