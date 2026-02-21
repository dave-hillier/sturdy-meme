#include "CellCullStage.h"
#include "TreeSpatialIndex.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL_log.h>

bool CellCullStage::createPipeline(const vk::raii::Device& raiiDevice, VkDevice device,
                                    const std::string& resourcePath) {
    DescriptorManager::LayoutBuilder builder(device);
    builder.addBinding(Bindings::TREE_CELL_CULL_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_VISIBLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create descriptor set layout");
        return false;
    }
    descriptorSetLayout.emplace(raiiDevice, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(raiiDevice)
        .addDescriptorSetLayout(**descriptorSetLayout)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create pipeline layout");
        return false;
    }
    pipelineLayout = std::move(layoutOpt);

    if (!ComputePipelineBuilder(raiiDevice)
            .setShader(resourcePath + "/shaders/tree_cell_cull.comp.spv")
            .setPipelineLayout(**pipelineLayout)
            .buildInto(pipeline)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create pipeline");
        return false;
    }

    SDL_Log("CellCullStage: Created cell culling compute pipeline");
    return true;
}

bool CellCullStage::createBuffers(VkDevice device, VmaAllocator allocator,
                                   DescriptorManager::Pool* descriptorPool,
                                   uint32_t maxFramesInFlight,
                                   const TreeSpatialIndex& spatialIndex) {
    if (!descriptorSetLayout) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Descriptor set layout is null");
        return false;
    }

    uint32_t numCells = spatialIndex.getCellCount();
    visibleCellBufferSize = (numCells + 1) * sizeof(uint32_t);

    if (!visibleCellBuffers.resize(
            allocator, maxFramesInFlight, visibleCellBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create visible cell buffers");
        return false;
    }

    constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
    vk::DeviceSize indirectBufferSize = (4 + NUM_DISTANCE_BUCKETS * 2) * sizeof(uint32_t);

    if (!indirectBuffers.resize(
            allocator, maxFramesInFlight, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create indirect buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(uniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(maxFramesInFlight)
            .setSize(sizeof(CellCullParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(paramsBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to create params buffers");
        return false;
    }

    descriptorSets = descriptorPool->allocate(**descriptorSetLayout, maxFramesInFlight);
    if (descriptorSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CellCullStage: Failed to allocate descriptor sets");
        return false;
    }

    for (uint32_t f = 0; f < maxFramesInFlight; ++f) {
        DescriptorManager::SetWriter writer(device, descriptorSets[f]);
        writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex.getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_VISIBLE, visibleCellBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_INDIRECT, indirectBuffers.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_CULLING, uniformBuffers.buffers[f], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_PARAMS, paramsBuffers.buffers[f], 0, sizeof(CellCullParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .update();
    }

    SDL_Log("CellCullStage: Created buffers (%u cells, %.2f KB visible buffer x %u frames)",
            numCells, visibleCellBufferSize / 1024.0f, maxFramesInFlight);
    return true;
}

void CellCullStage::updateSpatialIndexDescriptors(VkDevice device, uint32_t maxFramesInFlight,
                                                    const TreeSpatialIndex& spatialIndex) {
    for (uint32_t f = 0; f < maxFramesInFlight; ++f) {
        DescriptorManager::SetWriter writer(device, descriptorSets[f]);
        writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex.getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .update();
    }
}

void CellCullStage::destroy(VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, paramsBuffers);
}
