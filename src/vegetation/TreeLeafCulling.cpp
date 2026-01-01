#include "TreeLeafCulling.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ShaderLoader.h"
#include "Bindings.h"
#include "UBOs.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

std::unique_ptr<TreeLeafCulling> TreeLeafCulling::create(const InitInfo& info) {
    auto culling = std::unique_ptr<TreeLeafCulling>(new TreeLeafCulling());
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
    BufferUtils::destroyBuffers(allocator_, cullUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, leafCullParamsBuffers_);
    BufferUtils::destroyBuffers(allocator_, cellCullUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, cellCullParamsBuffers_);
    BufferUtils::destroyBuffers(allocator_, treeFilterUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, treeFilterParamsBuffers_);
    BufferUtils::destroyBuffers(allocator_, leafCullP3ParamsBuffers_);
}

bool TreeLeafCulling::init(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxFramesInFlight_ = info.maxFramesInFlight;
    terrainSize_ = info.terrainSize;

    if (!createLeafCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Culling pipeline not available, using direct rendering");
        return true; // Graceful degradation
    }

    if (!createCellCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell culling pipeline not available");
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

bool TreeLeafCulling::createLeafCullPipeline() {
    // Use LayoutBuilder to reduce boilerplate
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_LEAF_CULL_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    if (!builder.buildManaged(cullDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull descriptor set layout");
        return false;
    }

    if (!DescriptorManager::createManagedPipelineLayout(device_, cullDescriptorSetLayout_.get(), cullPipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull pipeline layout");
        return false;
    }

    std::string shaderPath = resourcePath_ + "/shaders/tree_leaf_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(cullPipelineLayout_.get());

    vk::Device vkDevice(device_);
    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(computeShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull compute pipeline");
        return false;
    }
    cullPipeline_ = ManagedPipeline::fromRaw(device_, result.value);

    SDL_Log("TreeLeafCulling: Created leaf culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createLeafCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees) {
    numTreesForIndirect_ = numTrees;

    // Use a fixed budget for visible leaf output rather than sizing for all possible instances.
    // The GPU culling pass outputs only visible leaves, so we size for expected maximum visibility:
    // - ~50-100 trees visible at once (close enough for full leaf detail)
    // - ~2000-3000 leaves per tree on average
    // - Total: ~100k-300k visible leaf instances per type at peak
    //
    // Using 100k per type = 400k total * 48 bytes * 3 frames = ~57.6MB
    // This is a reasonable GPU memory budget for leaf rendering.
    // The tiered distance budget in tree_leaf_cull_phase3.comp ensures nearby trees
    // always have leaves even when total budget is exhausted.
    constexpr uint32_t MAX_VISIBLE_LEAVES_PER_TYPE = 100000;
    maxLeavesPerType_ = MAX_VISIBLE_LEAVES_PER_TYPE;

    // Only log if input was much larger (avoid spam for reasonable inputs)
    if (maxLeafInstances > MAX_VISIBLE_LEAVES_PER_TYPE * 4) {
        SDL_Log("TreeLeafCulling: Using fixed output budget of %u leaves/type (input was %u total)",
                MAX_VISIBLE_LEAVES_PER_TYPE, maxLeafInstances);
    }

    cullOutputBufferSize_ = NUM_LEAF_TYPES * maxLeavesPerType_ * sizeof(WorldLeafInstanceGPU);

    // Use FrameIndexedBuffers for type-safe per-frame buffer management
    // This prevents the common desync bug where a separate counter gets out of sync with frameIndex
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

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(CullingUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(cullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(LeafCullParams))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .build(leafCullParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull params buffers");
        return false;
    }

    // Triple-buffered tree data buffers to prevent race conditions.
    // These are updated every frame via vkCmdUpdateBuffer, so they must be
    // triple-buffered to avoid GPU reading from a buffer that another frame is writing to.
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

    cullDescriptorSets_ = descriptorPool_->allocate(cullDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (cullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate cull descriptor sets");
        return false;
    }

    SDL_Log("TreeLeafCulling: Created leaf culling buffers (max %u instances, %u trees, %.2f MB output)",
            maxLeafInstances, numTrees,
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

    if (!builder.buildManaged(cellCullDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull descriptor set layout");
        return false;
    }

    if (!DescriptorManager::createManagedPipelineLayout(device_, cellCullDescriptorSetLayout_.get(), cellCullPipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull pipeline layout");
        return false;
    }

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
        .setLayout(cellCullPipelineLayout_.get());

    vk::Device vkDevice(device_);
    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(computeShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull compute pipeline");
        return false;
    }
    cellCullPipeline_ = ManagedPipeline::fromRaw(device_, result.value);

    SDL_Log("TreeLeafCulling: Created cell culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createCellCullBuffers() {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create cell cull buffers without valid spatial index");
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

    cellCullDescriptorSets_ = descriptorPool_->allocate(cellCullDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (cellCullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate cell cull descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings using SetWriter
    // Each frame's descriptor set uses that frame's buffers for proper triple-buffering
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        DescriptorManager::SetWriter writer(device_, cellCullDescriptorSets_[f]);
        writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex_->getCellBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
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

    if (!builder.buildManaged(treeFilterDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter descriptor set layout");
        return false;
    }

    if (!DescriptorManager::createManagedPipelineLayout(device_, treeFilterDescriptorSetLayout_.get(), treeFilterPipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter pipeline layout");
        return false;
    }

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
        .setLayout(treeFilterPipelineLayout_.get());

    vk::Device vkDevice(device_);
    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(computeShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter compute pipeline");
        return false;
    }
    treeFilterPipeline_ = ManagedPipeline::fromRaw(device_, result.value);

    SDL_Log("TreeLeafCulling: Created tree filter compute pipeline");
    return true;
}

bool TreeLeafCulling::createTreeFilterBuffers(uint32_t maxTrees) {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create tree filter buffers without valid spatial index");
        return false;
    }

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

    treeFilterDescriptorSets_ = descriptorPool_->allocate(treeFilterDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (treeFilterDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate tree filter descriptor sets");
        return false;
    }

    // Update descriptor sets with buffer bindings using SetWriter
    // Each frame's descriptor set uses that frame's buffers for proper triple-buffering
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        DescriptorManager::SetWriter writer(device_, treeFilterDescriptorSets_[f]);
        writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_VISIBLE_CELLS, visibleCellBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex_->getCellBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex_->getSortedTreeBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
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

    if (!builder.buildManaged(twoPhaseLeafCullDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull descriptor set layout");
        return false;
    }

    if (!DescriptorManager::createManagedPipelineLayout(device_, twoPhaseLeafCullDescriptorSetLayout_.get(), twoPhaseLeafCullPipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull pipeline layout");
        return false;
    }

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
        .setLayout(twoPhaseLeafCullPipelineLayout_.get());

    vk::Device vkDevice(device_);
    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(computeShaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull compute pipeline");
        return false;
    }
    twoPhaseLeafCullPipeline_ = ManagedPipeline::fromRaw(device_, result.value);

    SDL_Log("TreeLeafCulling: Created two-phase leaf culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createTwoPhaseLeafCullDescriptorSets() {
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

    twoPhaseLeafCullDescriptorSets_ = descriptorPool_->allocate(twoPhaseLeafCullDescriptorSetLayout_.get(), maxFramesInFlight_);
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

    if (visibleCellBuffers_.empty() && cellCullPipeline_.get() != VK_NULL_HANDLE) {
        createCellCullBuffers();
    }

    if (visibleTreeBuffers_.empty() && treeFilterPipeline_.get() != VK_NULL_HANDLE &&
        !visibleCellBuffers_.empty()) {
        createTreeFilterBuffers(static_cast<uint32_t>(leafRenderables.size()));
    }

    if (twoPhaseLeafCullDescriptorSets_.empty() && twoPhaseLeafCullPipeline_.get() != VK_NULL_HANDLE &&
        !visibleTreeBuffers_.empty()) {
        createTwoPhaseLeafCullDescriptorSets();
    }

    SDL_Log("TreeLeafCulling: Updated spatial index (%zu trees, %u non-empty cells)",
            leafRenderables.size(), spatialIndex_->getNonEmptyCellCount());
}

void TreeLeafCulling::updateCullDescriptorSets(const TreeSystem& treeSystem) {
    // Each frame's descriptor set points to its corresponding buffer set
    // This ensures proper triple-buffering: frame N uses buffer N
    // Using FrameIndexedBuffers::getVk(f) for type-safe access
    for (uint32_t f = 0; f < maxFramesInFlight_; ++f) {
        DescriptorManager::SetWriter writer(device_, cullDescriptorSets_[f]);
        writer.writeBuffer(Bindings::TREE_LEAF_CULL_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_LEAF_CULL_OUTPUT, cullOutputBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_LEAF_CULL_INDIRECT, cullIndirectBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_LEAF_CULL_CULLING, cullUniformBuffers_.buffers[f], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .writeBuffer(Bindings::TREE_LEAF_CULL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
              .writeBuffer(Bindings::TREE_LEAF_CULL_PARAMS, leafCullParamsBuffers_.buffers[f], 0, sizeof(LeafCullParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
              .update();
    }
    descriptorSetsInitialized_ = true;
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

    // Lazy initialization of cull buffers
    if (cullOutputBuffers_.empty()) {
        if (!createLeafCullBuffers(totalLeafInstances, numTrees)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull buffers");
            return;
        }
        updateCullDescriptorSets(treeSystem);
    }

    // Reset all 4 indirect draw commands (one per leaf type: oak, ash, aspen, pine)
    // Also includes tierCounts for phase 3 distance-based budget allocation
    constexpr uint32_t NUM_LEAF_TYPES = 4;
    constexpr uint32_t NUM_DISTANCE_TIERS = 3;

    // Structure matches shader's IndirectBuffer: drawCmds[4] followed by tierCounts[12]
    struct {
        VkDrawIndexedIndirectCommand drawCmds[NUM_LEAF_TYPES];
        uint32_t tierCounts[NUM_LEAF_TYPES * NUM_DISTANCE_TIERS];
    } indirectReset = {};

    for (uint32_t i = 0; i < NUM_LEAF_TYPES; ++i) {
        indirectReset.drawCmds[i].indexCount = 6;       // Quad: 6 indices
        indirectReset.drawCmds[i].instanceCount = 0;    // Will be set by compute shader
        indirectReset.drawCmds[i].firstIndex = 0;
        indirectReset.drawCmds[i].vertexOffset = 0;
        indirectReset.drawCmds[i].firstInstance = i * maxLeavesPerType_;  // Base offset for each leaf type
    }
    // tierCounts is already zero-initialized

    vkCmd.updateBuffer(cullIndirectBuffers_.getVk(frameIndex),
                       0, sizeof(indirectReset), &indirectReset);

    // Upload per-tree data to frame-specific buffers (triple-buffered to avoid race conditions)
    vkCmd.updateBuffer(treeDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeCullData), treeDataList.data());
    vkCmd.updateBuffer(treeRenderDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeRenderDataGPU), treeRenderDataList.data());

    // Upload global uniforms - split into CullingUniforms and LeafCullParams
    CullingUniforms culling{};
    culling.cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        culling.frustumPlanes[i] = frustumPlanes[i];
    }
    culling.maxDrawDistance = params_.maxDrawDistance;
    culling.lodTransitionStart = params_.lodTransitionStart;
    culling.lodTransitionEnd = params_.lodTransitionEnd;
    culling.maxLodDropRate = params_.maxLodDropRate;

    LeafCullParams leafParams{};
    leafParams.numTrees = numTrees;
    leafParams.totalLeafInstances = totalLeafInstances;
    leafParams.maxLeavesPerType = maxLeavesPerType_;

    vkCmd.updateBuffer(cullUniformBuffers_.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &culling);
    vkCmd.updateBuffer(leafCullParamsBuffers_.buffers[frameIndex], 0,
                       sizeof(LeafCullParams), &leafParams);

    // Barrier for buffer updates
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Descriptor sets are pre-configured during initialization to use buffer[frameIndex]
    // (see updateCullDescriptorSets), so no per-frame update needed here.

    // Cell Culling (if spatial index available)
    if (isSpatialIndexEnabled() && cellCullPipeline_.get() != VK_NULL_HANDLE) {
        // Split cell cull uniforms: CullingUniforms + CellCullParams
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

        // Check if two-phase culling will be used so we can batch uniform updates
        bool useTwoPhase = twoPhaseEnabled_ && treeFilterPipeline_.get() != VK_NULL_HANDLE &&
                           !visibleTreeBuffers_.empty() && !treeFilterDescriptorSets_.empty();

        // Reset cell cull output buffers on CPU side BEFORE dispatch
        // This is critical - shader-side initialization with barrier() only works within
        // a workgroup, not across workgroups. Other workgroups may atomicAdd before
        // workgroup 0 resets the counters, causing race conditions and flickering.
        // Using frame-indexed buffers to prevent race conditions between in-flight frames.

        // Reset visible cell buffer: first uint is visibleCellCount
        vkCmd.fillBuffer(visibleCellBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

        // Reset cell cull indirect buffer: dispatchX/Y/Z, totalVisibleTrees, bucketCounts[8], bucketOffsets[8]
        // Structure: { dispatchX=0, dispatchY=1, dispatchZ=1, totalVisibleTrees=0, bucketCounts[8]=0, bucketOffsets[8]=0 }
        constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
        uint32_t cellIndirectReset[4 + NUM_DISTANCE_BUCKETS * 2] = {0, 1, 1, 0}; // dispatchX=0, Y=1, Z=1, totalTrees=0
        // bucketCounts and bucketOffsets are already 0-initialized
        vkCmd.updateBuffer(cellCullIndirectBuffers_.getVk(frameIndex), 0, sizeof(cellIndirectReset), cellIndirectReset);

        // If two-phase culling, also reset visible tree buffer and leaf cull indirect dispatch
        if (useTwoPhase) {
            // Reset visible tree buffer: first uint is visibleTreeCount
            vkCmd.fillBuffer(visibleTreeBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

            // Reset leaf cull indirect dispatch: { dispatchX=0, dispatchY=1, dispatchZ=1 }
            uint32_t leafDispatchReset[3] = {0, 1, 1};
            vkCmd.updateBuffer(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0, sizeof(leafDispatchReset), leafDispatchReset);
        }

        // Use updateBuffer to avoid HOST→COMPUTE stall (keeps update on GPU timeline)
        vkCmd.updateBuffer(cellCullUniformBuffers_.buffers[frameIndex], 0,
                           sizeof(CullingUniforms), &cellCulling);
        vkCmd.updateBuffer(cellCullParamsBuffers_.buffers[frameIndex], 0,
                           sizeof(CellCullParams), &cellParams);

        // If two-phase culling is enabled, update tree filter uniforms now too
        // This allows us to combine barriers later (reducing pipeline bubbles)
        if (useTwoPhase) {
            // Split tree filter uniforms: CullingUniforms + TreeFilterParams
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

            vkCmd.updateBuffer(treeFilterUniformBuffers_.buffers[frameIndex], 0,
                               sizeof(CullingUniforms), &filterCulling);
            vkCmd.updateBuffer(treeFilterParamsBuffers_.buffers[frameIndex], 0,
                               sizeof(TreeFilterParams), &filterParams);
        }

        VkMemoryBarrier cellUniformBarrier{};
        cellUniformBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        cellUniformBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cellUniformBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &cellUniformBarrier, 0, nullptr, 0, nullptr);

        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, cellCullPipeline_.get());
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cellCullPipelineLayout_.get(),
                                 0, vk::DescriptorSet(cellCullDescriptorSets_[frameIndex]), {});

        uint32_t cellWorkgroups = (cellParams.numCells + 255) / 256;
        vkCmd.dispatch(cellWorkgroups, 1, 1);

        // Tree Filtering (Two-Phase Culling)
        // Uniforms were already updated above, so we only need COMPUTE→COMPUTE barrier
        if (useTwoPhase) {
            // Single barrier: wait for cell cull shader writes before tree filter reads
            VkMemoryBarrier cellBarrier{};
            cellBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            cellBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            cellBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &cellBarrier, 0, nullptr, 0, nullptr);

            vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, treeFilterPipeline_.get());
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, treeFilterPipelineLayout_.get(),
                                     0, vk::DescriptorSet(treeFilterDescriptorSets_[frameIndex]), {});

            vkCmd.dispatchIndirect(cellCullIndirectBuffers_.getVk(frameIndex), 0);

            VkMemoryBarrier treeFilterBarrier{};
            treeFilterBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            treeFilterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            treeFilterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &treeFilterBarrier, 0, nullptr, 0, nullptr);

            // Two-phase leaf culling
            if (twoPhaseLeafCullPipeline_.get() != VK_NULL_HANDLE && !twoPhaseLeafCullDescriptorSets_.empty()) {
                // Upload phase 3 specific params
                LeafCullP3Params p3Params{};
                p3Params.maxLeavesPerType = maxLeavesPerType_;

                vkCmd.updateBuffer(leafCullP3ParamsBuffers_.buffers[frameIndex], 0,
                                   sizeof(LeafCullP3Params), &p3Params);

                VkMemoryBarrier p3UniformBarrier{};
                p3UniformBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                p3UniformBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                p3UniformBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &p3UniformBarrier, 0, nullptr, 0, nullptr);

                DescriptorManager::SetWriter writer(device_, twoPhaseLeafCullDescriptorSets_[frameIndex]);
                writer.writeBuffer(Bindings::LEAF_CULL_P3_VISIBLE_TREES, visibleTreeBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_ALL_TREES, treeDataBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_OUTPUT, cullOutputBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_INDIRECT, cullIndirectBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_CULLING, cullUniformBuffers_.buffers[frameIndex], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                      .writeBuffer(Bindings::LEAF_CULL_P3_PARAMS, leafCullP3ParamsBuffers_.buffers[frameIndex], 0, sizeof(LeafCullP3Params), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                      .update();

                vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, twoPhaseLeafCullPipeline_.get());
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, twoPhaseLeafCullPipelineLayout_.get(),
                                         0, vk::DescriptorSet(twoPhaseLeafCullDescriptorSets_[frameIndex]), {});

                vkCmd.dispatchIndirect(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0);

                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
                return;
            }
        }
    }

    // Fallback: Single-phase leaf culling
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, cullPipeline_.get());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cullPipelineLayout_.get(),
                             0, vk::DescriptorSet(cullDescriptorSets_[frameIndex]), {});

    uint32_t workgroupCount = (totalLeafInstances + 255) / 256;
    vkCmd.dispatch(workgroupCount, 1, 1);

    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}
