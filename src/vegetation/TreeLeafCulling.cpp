#include "TreeLeafCulling.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ShaderLoader.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/ComputeShaderCommon.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <numeric>

std::unique_ptr<TreeLeafCulling> TreeLeafCulling::create(const InitInfo& info) {
    auto culling = std::make_unique<TreeLeafCulling>(ConstructToken{});
    if (!culling->init(info)) {
        return nullptr;
    }
    return culling;
}

TreeLeafCulling::~TreeLeafCulling() {
    // FrameIndexedBuffers clean up automatically via their destroy() method:
    // - cullOutputBuffers_, cullIndirectBuffers_, treeDataBuffers_, treeRenderDataBuffers_
    // - visibleCellBuffers_, cellCullIndirectBuffers_, visibleTreeBuffers_, leafCullIndirectDispatchBuffers_

    // Use helper for per-frame buffer sets
    BufferUtils::destroyBuffers(allocator_, cellCullUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, cellCullParamsBuffers_);
    BufferUtils::destroyBuffers(allocator_, treeFilterUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, treeFilterParamsBuffers_);
    BufferUtils::destroyBuffers(allocator_, leafCullP3UniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, leafCullP3ParamsBuffers_);
}

bool TreeLeafCulling::init(const InitInfo& info) {
    raiiDevice_ = info.raiiDevice;
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling requires raiiDevice");
        return false;
    }
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxFramesInFlight_ = info.maxFramesInFlight;
    terrainSize_ = info.terrainSize;

    if (!createCellCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell culling pipeline not available, using direct rendering");
        return true; // Graceful degradation
    }

    if (!createTreeFilterPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter pipeline not available");
    }

    if (!createTwoPhaseLeafCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull pipeline not available");
    }

    SDL_Log("TreeLeafCulling initialized successfully");
    return true;
}

bool TreeLeafCulling::createSharedOutputBuffers(uint32_t numTrees) {
    numTreesForIndirect_ = numTrees;

    // Use a fixed budget for visible leaf output rather than sizing for all possible instances.
    constexpr uint32_t MAX_VISIBLE_LEAVES_PER_TYPE = 200000;
    maxLeavesPerType_ = MAX_VISIBLE_LEAVES_PER_TYPE;

    cullOutputBufferSize_ = NUM_LEAF_TYPES * maxLeavesPerType_ * sizeof(WorldLeafInstanceGPU);

    if (!cullOutputBuffers_.resize(
            allocator_, maxFramesInFlight_, cullOutputBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull output buffers");
        return false;
    }

    vk::DeviceSize indirectBufferSize = NUM_LEAF_TYPES * sizeof(VkDrawIndexedIndirectCommand);
    if (!cullIndirectBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull indirect buffers");
        return false;
    }

    // Triple-buffered tree data buffers to prevent race conditions.
    treeDataBufferSize_ = numTrees * sizeof(TreeCullData);
    if (!treeDataBuffers_.resize(
            allocator_, maxFramesInFlight_, treeDataBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree cull data buffers");
        return false;
    }

    treeRenderDataBufferSize_ = numTrees * sizeof(TreeRenderDataGPU);
    if (!treeRenderDataBuffers_.resize(
            allocator_, maxFramesInFlight_, treeRenderDataBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree render data buffers");
        return false;
    }

    SDL_Log("TreeLeafCulling: Created shared output buffers (%u trees, %.2f MB output)",
            numTrees,
            static_cast<float>(cullOutputBufferSize_ * maxFramesInFlight_) / (1024.0f * 1024.0f));
    return true;
}

bool TreeLeafCulling::createCellCullPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_CELL_CULL_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_VISIBLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull descriptor set layout");
        return false;
    }
    cellCullDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**cellCullDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull pipeline layout");
        return false;
    }
    cellCullPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_cell_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**cellCullPipelineLayout_);

    cellCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created cell culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createCellCullBuffers() {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create cell cull buffers without valid spatial index");
        return false;
    }

    // Guard against null layout (shouldn't happen, but defensive check)
    if (!cellCullDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell cull descriptor set layout is null");
        return false;
    }

    uint32_t numCells = spatialIndex_->getCellCount();
    visibleCellBufferSize_ = (numCells + 1) * sizeof(uint32_t);

    // Triple-buffered visible cell buffer to prevent race conditions between frames
    if (!visibleCellBuffers_.resize(
            allocator_, maxFramesInFlight_, visibleCellBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create visible cell buffers");
        return false;
    }

    // Triple-buffered indirect buffer to prevent race conditions between frames
    // Includes bucket counts/offsets for distance-sorted processing
    // Layout: dispatchX, dispatchY, dispatchZ, totalVisibleTrees, bucketCounts[8], bucketOffsets[8]
    constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
    vk::DeviceSize indirectBufferSize = (4 + NUM_DISTANCE_BUCKETS * 2) * sizeof(uint32_t);  // 20 uints = 80 bytes

    if (!cellCullIndirectBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull indirect buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(cellCullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(CellCullParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(cellCullParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull params buffers");
        return false;
    }

    cellCullDescriptorSets_ = descriptorPool_->allocate(**cellCullDescriptorSetLayout_, maxFramesInFlight_);
    if (cellCullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate cell cull descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings using SetWriter
    // Each frame's descriptor set uses that frame's buffers for proper triple-buffering
    // Spatial index buffers are also triple-buffered to prevent race conditions
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        DescriptorManager::SetWriter writer(device_, cellCullDescriptorSets_[f]);
        writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_VISIBLE, visibleCellBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_INDIRECT, cellCullIndirectBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_CULLING, cellCullUniformBuffers_.buffers[f], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .writeBuffer(Bindings::TREE_CELL_CULL_PARAMS, cellCullParamsBuffers_.buffers[f], 0, sizeof(CellCullParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .update();
    }

    SDL_Log("TreeLeafCulling: Created cell culling buffers (%u cells, %.2f KB visible buffer x %u frames)",
            numCells, visibleCellBufferSize_ / 1024.0f, maxFramesInFlight_);
    return true;
}

bool TreeLeafCulling::createTreeFilterPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter descriptor set layout");
        return false;
    }
    treeFilterDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**treeFilterDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter pipeline layout");
        return false;
    }
    treeFilterPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_filter.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**treeFilterPipelineLayout_);

    treeFilterPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created tree filter compute pipeline");
    return true;
}

bool TreeLeafCulling::createTreeFilterBuffers(uint32_t maxTrees) {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create tree filter buffers without valid spatial index");
        return false;
    }

    // Guard against null layout
    if (!treeFilterDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter descriptor set layout is null");
        return false;
    }

    maxVisibleTrees_ = maxTrees;
    visibleTreeBufferSize_ = sizeof(uint32_t) + maxTrees * sizeof(VisibleTreeData);

    // Triple-buffered visible tree buffer to prevent race conditions between frames
    if (!visibleTreeBuffers_.resize(
            allocator_, maxFramesInFlight_, visibleTreeBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create visible tree buffers");
        return false;
    }

    // Triple-buffered indirect dispatch buffer to prevent race conditions between frames
    vk::DeviceSize indirectDispatchSize = 3 * sizeof(uint32_t);
    if (!leafCullIndirectDispatchBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectDispatchSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull indirect dispatch buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(treeFilterUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(TreeFilterParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(treeFilterParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter params buffers");
        return false;
    }

    treeFilterDescriptorSets_ = descriptorPool_->allocate(**treeFilterDescriptorSetLayout_, maxFramesInFlight_);
    if (treeFilterDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate tree filter descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings using SetWriter
    // Each frame's descriptor set uses that frame's buffers for proper triple-buffering
    // Spatial index buffers are also triple-buffered to prevent race conditions
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        DescriptorManager::SetWriter writer(device_, treeFilterDescriptorSets_[f]);
        writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_VISIBLE_CELLS, visibleCellBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex_->getSortedTreeBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_VISIBLE_TREES, visibleTreeBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_INDIRECT, leafCullIndirectDispatchBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_CULLING, treeFilterUniformBuffers_.buffers[f], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_PARAMS, treeFilterParamsBuffers_.buffers[f], 0, sizeof(TreeFilterParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .update();
    }

    SDL_Log("TreeLeafCulling: Created tree filter buffers (max %u trees, %.2f KB visible tree buffer x %u frames)",
            maxTrees, visibleTreeBufferSize_ / 1024.0f, maxFramesInFlight_);
    return true;
}

bool TreeLeafCulling::createTwoPhaseLeafCullPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::LEAF_CULL_P3_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull descriptor set layout");
        return false;
    }
    twoPhaseLeafCullDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**twoPhaseLeafCullDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull pipeline layout");
        return false;
    }
    twoPhaseLeafCullPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_leaf_cull_phase3.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**twoPhaseLeafCullPipelineLayout_);

    twoPhaseLeafCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created two-phase leaf culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createTwoPhaseLeafCullDescriptorSets() {
    // Guard against null layout
    if (!twoPhaseLeafCullDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull descriptor set layout is null");
        return false;
    }

    // Create uniform buffer for phase 3 CullingUniforms
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(leafCullP3UniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull P3 uniform buffers");
        return false;
    }

    // Create params buffer for phase 3
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(LeafCullP3Params))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(leafCullP3ParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull P3 params buffers");
        return false;
    }

    twoPhaseLeafCullDescriptorSets_ = descriptorPool_->allocate(**twoPhaseLeafCullDescriptorSetLayout_, maxFramesInFlight_);
    if (twoPhaseLeafCullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate two-phase leaf cull descriptor sets");
        return false;
    }

    SDL_Log("TreeLeafCulling: Allocated %u two-phase leaf cull descriptor sets", maxFramesInFlight_);
    return true;
}

void TreeLeafCulling::updateSpatialIndex(const TreeSystem& treeSystem) {
    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    if (leafRenderables.empty()) {
        spatialIndex_.reset();
        return;
    }

    if (!spatialIndex_) {
        TreeSpatialIndex::InitInfo indexInfo{};
        indexInfo.device = device_;
        indexInfo.allocator = allocator_;
        indexInfo.cellSize = 64.0f;
        indexInfo.worldSize = terrainSize_;
        indexInfo.maxFramesInFlight = maxFramesInFlight_;

        spatialIndex_ = TreeSpatialIndex::create(indexInfo);
        if (!spatialIndex_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create spatial index");
            return;
        }
    }

    // Build transforms and scales from leafRenderables
    // CRITICAL: The spatial index must use the SAME filtering as recordCulling()
    // to ensure originalTreeIndex matches the index into the TreeCullData buffer.
    // Trees with invalid leafInstanceIndex or zero instanceCount are filtered out
    // in both places to maintain index consistency.
    std::vector<glm::mat4> transforms;
    std::vector<float> scales;
    transforms.reserve(leafRenderables.size());
    scales.reserve(leafRenderables.size());
    for (const auto& renderable : leafRenderables) {
        // Apply same filtering as recordCulling() to ensure index consistency
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                transforms.push_back(renderable.transform);
                // Estimate scale from transform (use Y-axis length as approximation)
                float scale = glm::length(glm::vec3(renderable.transform[1]));
                scales.push_back(scale);
            }
        }
    }

    spatialIndex_->rebuild(transforms, scales);

    if (!spatialIndex_->uploadToGPU()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to upload spatial index to GPU");
        return;
    }

    if (visibleCellBuffers_.empty() && cellCullPipeline_) {
        createCellCullBuffers();
    } else if (!cellCullDescriptorSets_.empty()) {
        // Spatial index buffers were recreated - update descriptor sets with new buffer handles
        // This is necessary because uploadToGPU() destroys and recreates all buffers
        for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
            DescriptorManager::SetWriter writer(device_, cellCullDescriptorSets_[f]);
            writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                  .update();
        }
    }

    uint32_t requiredTreeCapacity = static_cast<uint32_t>(leafRenderables.size());
    bool needsTreeFilterBuffers = visibleTreeBuffers_.empty() ||
                                  requiredTreeCapacity > maxVisibleTrees_;

    // Only create tree filter buffers if tree data buffers are already initialized
    // (they get created lazily during first recordCulling call)
    if (needsTreeFilterBuffers && treeFilterPipeline_ &&
        !visibleCellBuffers_.empty() && !treeDataBuffers_.empty()) {
        // Need to wait for GPU before destroying old buffers
        if (!visibleTreeBuffers_.empty()) {
            vkDeviceWaitIdle(device_);
            SDL_Log("TreeLeafCulling: Resizing visible tree buffer from %u to %u trees",
                    maxVisibleTrees_, requiredTreeCapacity);
        }
        createTreeFilterBuffers(requiredTreeCapacity);
    } else if (!treeFilterDescriptorSets_.empty()) {
        // Spatial index buffers were recreated - update descriptor sets with new buffer handles
        for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
            DescriptorManager::SetWriter writer(device_, treeFilterDescriptorSets_[f]);
            writer.writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                  .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex_->getSortedTreeBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                  .update();
        }
    }

    if (twoPhaseLeafCullDescriptorSets_.empty() && twoPhaseLeafCullPipeline_ &&
        !visibleTreeBuffers_.empty()) {
        createTwoPhaseLeafCullDescriptorSets();
    }

    SDL_Log("TreeLeafCulling: Updated spatial index (%zu trees, %u non-empty cells)",
            leafRenderables.size(), spatialIndex_->getNonEmptyCellCount());
}

void TreeLeafCulling::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                     const TreeSystem& treeSystem,
                                     const TreeLODSystem* lodSystem,
                                     const glm::vec3& cameraPos,
                                     const glm::vec4* frustumPlanes) {
    if (!isEnabled()) return;

    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    if (leafRenderables.empty() || leafDrawInfo.empty()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Build per-tree data for batched culling
    std::vector<TreeCullData> treeDataList;
    std::vector<TreeRenderDataGPU> treeRenderDataList;
    treeDataList.reserve(leafRenderables.size());
    treeRenderDataList.reserve(leafRenderables.size());

    uint32_t numTrees = 0;
    uint32_t totalLeafInstances = 0;

    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                float lodBlendFactor = 0.0f;
                if (lodSystem) {
                    // Use leafInstanceIndex (== tree instance index) for LOD lookup
                    // This correctly maps to treeInstances_ even if some trees have no leaves
                    lodBlendFactor = lodSystem->getBlendFactor(static_cast<uint32_t>(renderable.leafInstanceIndex));
                }

                uint32_t leafTypeIdx = LEAF_TYPE_OAK;
                if (renderable.leafType == "ash") leafTypeIdx = LEAF_TYPE_ASH;
                else if (renderable.leafType == "aspen") leafTypeIdx = LEAF_TYPE_ASPEN;
                else if (renderable.leafType == "pine") leafTypeIdx = LEAF_TYPE_PINE;

                static bool loggedOnce = false;
                if (!loggedOnce && numTrees < 10) {
                    SDL_Log("TreeLeafCulling: Tree %u: leafType='%s' -> leafTypeIdx=%u, firstInst=%u, count=%u",
                            numTrees, renderable.leafType.c_str(), leafTypeIdx,
                            drawInfo.firstInstance, drawInfo.instanceCount);
                    if (numTrees == 9) loggedOnce = true;
                }

                TreeCullData treeData{};
                treeData.treeModel = renderable.transform;
                treeData.inputFirstInstance = drawInfo.firstInstance;
                treeData.inputInstanceCount = drawInfo.instanceCount;
                treeData.treeIndex = numTrees;
                treeData.leafTypeIndex = leafTypeIdx;
                treeData.lodBlendFactor = lodBlendFactor;
                treeDataList.push_back(treeData);

                TreeRenderDataGPU renderData{};
                renderData.model = renderable.transform;
                renderData.tintAndParams = glm::vec4(renderable.leafTint, renderable.autumnHueShift);
                float windOffset = glm::fract(renderable.transform[3][0] * 0.1f + renderable.transform[3][2] * 0.1f) * 6.28318f;
                renderData.windOffsetAndLOD = glm::vec4(windOffset, lodBlendFactor, 0.0f, 0.0f);
                treeRenderDataList.push_back(renderData);

                totalLeafInstances += drawInfo.instanceCount;
                numTrees++;
            }
        }
    }
    if (numTrees == 0 || totalLeafInstances == 0) return;

    // CRITICAL: Sort tree data by inputFirstInstance for binary search in shader.
    // The shader's binary search assumes trees are sorted by their leaf instance range.
    // If trees were added in non-sequential order, the search fails and defaults to
    // tree 0 (usually oak), causing all leaves to render as oak.
    std::vector<size_t> sortIndices(treeDataList.size());
    std::iota(sortIndices.begin(), sortIndices.end(), 0);
    std::sort(sortIndices.begin(), sortIndices.end(), [&](size_t a, size_t b) {
        return treeDataList[a].inputFirstInstance < treeDataList[b].inputFirstInstance;
    });

    // Reorder both lists according to sorted indices
    std::vector<TreeCullData> sortedTreeData(treeDataList.size());
    std::vector<TreeRenderDataGPU> sortedRenderData(treeRenderDataList.size());
    for (size_t i = 0; i < sortIndices.size(); ++i) {
        sortedTreeData[i] = treeDataList[sortIndices[i]];
        sortedTreeData[i].treeIndex = static_cast<uint32_t>(i);  // Update treeIndex to match new position
        sortedRenderData[i] = treeRenderDataList[sortIndices[i]];
    }

    treeDataList = std::move(sortedTreeData);
    treeRenderDataList = std::move(sortedRenderData);

    // Lazy initialization of shared output buffers
    if (cullOutputBuffers_.empty()) {
        if (!createSharedOutputBuffers(numTrees)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create shared output buffers");
            return;
        }
    }

    // CRITICAL: Check if tree count exceeds buffer capacity and resize if needed.
    if (numTrees > numTreesForIndirect_) {
        SDL_Log("TreeLeafCulling: Tree count increased from %u to %u, resizing buffers",
                numTreesForIndirect_, numTrees);
        vkDeviceWaitIdle(device_);

        treeDataBufferSize_ = numTrees * sizeof(TreeCullData);
        if (!treeDataBuffers_.resize(
                allocator_, maxFramesInFlight_, treeDataBufferSize_,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to resize tree data buffers");
            return;
        }

        treeRenderDataBufferSize_ = numTrees * sizeof(TreeRenderDataGPU);
        if (!treeRenderDataBuffers_.resize(
                allocator_, maxFramesInFlight_, treeRenderDataBufferSize_,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to resize tree render data buffers");
            return;
        }

        numTreesForIndirect_ = numTrees;

        // Update tree filter descriptor sets if they exist
        if (!treeFilterDescriptorSets_.empty()) {
            for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
                DescriptorManager::SetWriter writer(device_, treeFilterDescriptorSets_[f]);
                writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .update();
            }
        }
    }

    // Reset all 4 indirect draw commands (one per leaf type: oak, ash, aspen, pine)
    constexpr uint32_t NUM_LEAF_TYPES = 4;

    VkDrawIndexedIndirectCommand indirectReset[NUM_LEAF_TYPES] = {};
    for (uint32_t i = 0; i < NUM_LEAF_TYPES; ++i) {
        indirectReset[i].indexCount = 6;       // Quad: 6 indices
        indirectReset[i].instanceCount = 0;    // Will be set by compute shader
        indirectReset[i].firstIndex = 0;
        indirectReset[i].vertexOffset = 0;
        indirectReset[i].firstInstance = i * maxLeavesPerType_;  // Base offset for each leaf type
    }

    vkCmd.updateBuffer(cullIndirectBuffers_.getVk(frameIndex),
                       0, sizeof(indirectReset), &indirectReset);

    // Upload per-tree data to frame-specific buffers (triple-buffered to avoid race conditions)
    vkCmd.updateBuffer(treeDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeCullData), treeDataList.data());
    vkCmd.updateBuffer(treeRenderDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeRenderDataGPU), treeRenderDataList.data());

    // Barrier for tree data buffer updates
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eUniformRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, barrier, {}, {});

    if (!isSpatialIndexEnabled() || !cellCullPipeline_) return;
    if (!treeFilterPipeline_ || visibleTreeBuffers_.empty() || treeFilterDescriptorSets_.empty()) return;
    if (!twoPhaseLeafCullPipeline_ || twoPhaseLeafCullDescriptorSets_.empty()) return;

    // --- Phase 1: Cell Culling ---
    CullingUniforms cellCulling{};
    cellCulling.cameraPosition = glm::vec4(cameraPos, 1.0f);
    for (int i = 0; i < 6; ++i) {
        cellCulling.frustumPlanes[i] = frustumPlanes[i];
    }
    cellCulling.maxDrawDistance = 250.0f;
    cellCulling.lodTransitionStart = params_.lodTransitionStart;
    cellCulling.lodTransitionEnd = params_.lodTransitionEnd;
    cellCulling.maxLodDropRate = params_.maxLodDropRate;

    CellCullParams cellParams{};
    cellParams.numCells = spatialIndex_->getCellCount();
    cellParams.treesPerWorkgroup = 64;

    // Reset cell cull output buffers
    vkCmd.fillBuffer(visibleCellBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

    constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
    uint32_t cellIndirectReset[4 + NUM_DISTANCE_BUCKETS * 2] = {0, 1, 1, 0};
    vkCmd.updateBuffer(cellCullIndirectBuffers_.getVk(frameIndex), 0, sizeof(cellIndirectReset), cellIndirectReset);

    // Reset tree filter and phase 3 buffers
    vkCmd.fillBuffer(visibleTreeBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

    uint32_t leafDispatchReset[3] = {0, 1, 1};
    vkCmd.updateBuffer(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0, sizeof(leafDispatchReset), leafDispatchReset);

    // Upload cell cull uniforms
    vkCmd.updateBuffer(cellCullUniformBuffers_.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &cellCulling);
    vkCmd.updateBuffer(cellCullParamsBuffers_.buffers[frameIndex], 0,
                       sizeof(CellCullParams), &cellParams);

    // Upload tree filter uniforms (batched to reduce pipeline bubbles)
    CullingUniforms filterCulling{};
    filterCulling.cameraPosition = glm::vec4(cameraPos, 1.0f);
    for (int i = 0; i < 6; ++i) {
        filterCulling.frustumPlanes[i] = frustumPlanes[i];
    }
    filterCulling.maxDrawDistance = params_.maxDrawDistance;
    filterCulling.lodTransitionStart = params_.lodTransitionStart;
    filterCulling.lodTransitionEnd = params_.lodTransitionEnd;
    filterCulling.maxLodDropRate = params_.maxLodDropRate;

    TreeFilterParams filterParams{};
    filterParams.maxTreesPerCell = 64;
    filterParams.maxVisibleTrees = maxVisibleTrees_;

    vkCmd.updateBuffer(treeFilterUniformBuffers_.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &filterCulling);
    vkCmd.updateBuffer(treeFilterParamsBuffers_.buffers[frameIndex], 0,
                       sizeof(TreeFilterParams), &filterParams);

    // Barrier for all buffer updates
    auto cellUniformBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eUniformRead | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, cellUniformBarrier, {}, {});

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **cellCullPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **cellCullPipelineLayout_,
                             0, vk::DescriptorSet(cellCullDescriptorSets_[frameIndex]), {});

    uint32_t cellWorkgroups = ComputeConstants::getDispatchCount1D(cellParams.numCells);
    vkCmd.dispatch(cellWorkgroups, 1, 1);

    // --- Phase 2: Tree Filtering ---
    auto cellBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, cellBarrier, {}, {});

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **treeFilterPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **treeFilterPipelineLayout_,
                             0, vk::DescriptorSet(treeFilterDescriptorSets_[frameIndex]), {});

    vkCmd.dispatchIndirect(cellCullIndirectBuffers_.getVk(frameIndex), 0);

    auto treeFilterBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, treeFilterBarrier, {}, {});

    // --- Phase 3: Leaf Culling ---
    CullingUniforms leafCulling{};
    leafCulling.cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        leafCulling.frustumPlanes[i] = frustumPlanes[i];
    }
    leafCulling.maxDrawDistance = params_.maxDrawDistance;
    leafCulling.lodTransitionStart = params_.lodTransitionStart;
    leafCulling.lodTransitionEnd = params_.lodTransitionEnd;
    leafCulling.maxLodDropRate = params_.maxLodDropRate;

    LeafCullP3Params p3Params{};
    p3Params.maxLeavesPerType = maxLeavesPerType_;

    vkCmd.updateBuffer(leafCullP3UniformBuffers_.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &leafCulling);
    vkCmd.updateBuffer(leafCullP3ParamsBuffers_.buffers[frameIndex], 0,
                       sizeof(LeafCullP3Params), &p3Params);

    auto p3UniformBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eUniformRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, p3UniformBarrier, {}, {});

    DescriptorManager::SetWriter writer(device_, twoPhaseLeafCullDescriptorSets_[frameIndex]);
    writer.writeBuffer(Bindings::LEAF_CULL_P3_VISIBLE_TREES, visibleTreeBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_ALL_TREES, treeDataBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_OUTPUT, cullOutputBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INDIRECT, cullIndirectBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_CULLING, leafCullP3UniformBuffers_.buffers[frameIndex], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_PARAMS, leafCullP3ParamsBuffers_.buffers[frameIndex], 0, sizeof(LeafCullP3Params), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **twoPhaseLeafCullPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **twoPhaseLeafCullPipelineLayout_,
                             0, vk::DescriptorSet(twoPhaseLeafCullDescriptorSets_[frameIndex]), {});

    vkCmd.dispatchIndirect(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0);

    barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
           .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                          {}, barrier, {}, {});
}
