#include "TreeLeafCulling.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "Bindings.h"
#include "UBOs.h"
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
    cellCullStage_.destroy(allocator_);
    treeFilterStage_.destroy(allocator_);
    leafCullPhase3Stage_.destroy(allocator_);
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

    if (!cellCullStage_.createPipeline(*raiiDevice_, device_, resourcePath_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell culling pipeline not available, using direct rendering");
        return true; // Graceful degradation
    }

    if (!treeFilterStage_.createPipeline(*raiiDevice_, device_, resourcePath_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter pipeline not available");
    }

    if (!leafCullPhase3Stage_.createPipeline(*raiiDevice_, device_, resourcePath_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Leaf cull phase 3 pipeline not available");
    }

    SDL_Log("TreeLeafCulling initialized successfully");
    return true;
}

bool TreeLeafCulling::createSharedOutputBuffers(uint32_t numTrees) {
    numTreesForIndirect_ = numTrees;

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
    std::vector<glm::mat4> transforms;
    std::vector<float> scales;
    transforms.reserve(leafRenderables.size());
    scales.reserve(leafRenderables.size());
    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                transforms.push_back(renderable.transform);
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

    // Cell cull stage: create buffers or update descriptors
    if (cellCullStage_.visibleCellBuffers.empty() && cellCullStage_.pipeline) {
        cellCullStage_.createBuffers(device_, allocator_, descriptorPool_,
                                     maxFramesInFlight_, *spatialIndex_);
    } else if (!cellCullStage_.descriptorSets.empty()) {
        cellCullStage_.updateSpatialIndexDescriptors(device_, maxFramesInFlight_, *spatialIndex_);
    }

    // Tree filter stage: create buffers or update descriptors
    uint32_t requiredTreeCapacity = static_cast<uint32_t>(leafRenderables.size());
    bool needsTreeFilterBuffers = treeFilterStage_.visibleTreeBuffers.empty() ||
                                  requiredTreeCapacity > treeFilterStage_.maxVisibleTrees;

    if (needsTreeFilterBuffers && treeFilterStage_.pipeline &&
        !cellCullStage_.visibleCellBuffers.empty() && !treeDataBuffers_.empty()) {
        if (!treeFilterStage_.visibleTreeBuffers.empty()) {
            vkDeviceWaitIdle(device_);
            SDL_Log("TreeLeafCulling: Resizing visible tree buffer from %u to %u trees",
                    treeFilterStage_.maxVisibleTrees, requiredTreeCapacity);
        }
        treeFilterStage_.createBuffers(device_, allocator_, descriptorPool_,
                                       maxFramesInFlight_, requiredTreeCapacity,
                                       *spatialIndex_, treeDataBuffers_,
                                       cellCullStage_.visibleCellBuffers);
    } else if (!treeFilterStage_.descriptorSets.empty()) {
        treeFilterStage_.updateSpatialIndexDescriptors(device_, maxFramesInFlight_, *spatialIndex_);
    }

    // Leaf cull phase 3: create descriptor sets if ready
    if (leafCullPhase3Stage_.descriptorSets.empty() && leafCullPhase3Stage_.pipeline &&
        !treeFilterStage_.visibleTreeBuffers.empty()) {
        leafCullPhase3Stage_.createDescriptorSets(device_, allocator_, descriptorPool_,
                                                   maxFramesInFlight_);
    }

    SDL_Log("TreeLeafCulling: Updated spatial index (%zu trees, %u non-empty cells)",
            leafRenderables.size(), spatialIndex_->getNonEmptyCellCount());
}

struct TreeDataPrepResult {
    std::vector<TreeCullData> treeData;
    std::vector<TreeRenderDataGPU> renderData;
    uint32_t numTrees = 0;
    uint32_t totalLeafInstances = 0;
};

static TreeDataPrepResult prepareTreeCullData(const TreeSystem& treeSystem,
                                               const TreeLODSystem* lodSystem) {
    TreeDataPrepResult result;
    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    result.treeData.reserve(leafRenderables.size());
    result.renderData.reserve(leafRenderables.size());

    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                float lodBlendFactor = 0.0f;
                if (lodSystem) {
                    lodBlendFactor = lodSystem->getBlendFactor(static_cast<uint32_t>(renderable.leafInstanceIndex));
                }

                uint32_t leafTypeIdx = LEAF_TYPE_OAK;
                if (renderable.leafType == "ash") leafTypeIdx = LEAF_TYPE_ASH;
                else if (renderable.leafType == "aspen") leafTypeIdx = LEAF_TYPE_ASPEN;
                else if (renderable.leafType == "pine") leafTypeIdx = LEAF_TYPE_PINE;

                static bool loggedOnce = false;
                if (!loggedOnce && result.numTrees < 10) {
                    SDL_Log("TreeLeafCulling: Tree %u: leafType='%s' -> leafTypeIdx=%u, firstInst=%u, count=%u",
                            result.numTrees, renderable.leafType.c_str(), leafTypeIdx,
                            drawInfo.firstInstance, drawInfo.instanceCount);
                    if (result.numTrees == 9) loggedOnce = true;
                }

                TreeCullData treeData{};
                treeData.treeModel = renderable.transform;
                treeData.inputFirstInstance = drawInfo.firstInstance;
                treeData.inputInstanceCount = drawInfo.instanceCount;
                treeData.treeIndex = result.numTrees;
                treeData.leafTypeIndex = leafTypeIdx;
                treeData.lodBlendFactor = lodBlendFactor;
                result.treeData.push_back(treeData);

                TreeRenderDataGPU renderData{};
                renderData.model = renderable.transform;
                renderData.tintAndParams = glm::vec4(renderable.leafTint, renderable.autumnHueShift);
                float windOffset = glm::fract(renderable.transform[3][0] * 0.1f + renderable.transform[3][2] * 0.1f) * 6.28318f;
                renderData.windOffsetAndLOD = glm::vec4(windOffset, lodBlendFactor, 0.0f, 0.0f);
                result.renderData.push_back(renderData);

                result.totalLeafInstances += drawInfo.instanceCount;
                result.numTrees++;
            }
        }
    }

    if (result.numTrees == 0) return result;

    // CRITICAL: Sort tree data by inputFirstInstance for binary search in shader.
    std::vector<size_t> sortIndices(result.treeData.size());
    std::iota(sortIndices.begin(), sortIndices.end(), 0);
    std::sort(sortIndices.begin(), sortIndices.end(), [&](size_t a, size_t b) {
        return result.treeData[a].inputFirstInstance < result.treeData[b].inputFirstInstance;
    });

    std::vector<TreeCullData> sortedTreeData(result.treeData.size());
    std::vector<TreeRenderDataGPU> sortedRenderData(result.renderData.size());
    for (size_t i = 0; i < sortIndices.size(); ++i) {
        sortedTreeData[i] = result.treeData[sortIndices[i]];
        sortedTreeData[i].treeIndex = static_cast<uint32_t>(i);
        sortedRenderData[i] = result.renderData[sortIndices[i]];
    }

    result.treeData = std::move(sortedTreeData);
    result.renderData = std::move(sortedRenderData);
    return result;
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

    auto prep = prepareTreeCullData(treeSystem, lodSystem);
    if (prep.numTrees == 0 || prep.totalLeafInstances == 0) return;

    uint32_t numTrees = prep.numTrees;

    vk::CommandBuffer vkCmd(cmd);

    // Lazy initialization of shared output buffers
    if (cullOutputBuffers_.empty()) {
        if (!createSharedOutputBuffers(numTrees)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create shared output buffers");
            return;
        }

        // Deferred stage initialization: updateSpatialIndex() may have run before
        // shared output buffers existed, so the TreeFilterStage and LeafCullPhase3Stage
        // couldn't be initialized. Now that treeDataBuffers_ exists, create them.
        if (isSpatialIndexEnabled() && treeFilterStage_.pipeline &&
            treeFilterStage_.visibleTreeBuffers.empty() &&
            !cellCullStage_.visibleCellBuffers.empty()) {
            uint32_t requiredTreeCapacity = static_cast<uint32_t>(leafRenderables.size());
            treeFilterStage_.createBuffers(device_, allocator_, descriptorPool_,
                                           maxFramesInFlight_, requiredTreeCapacity,
                                           *spatialIndex_, treeDataBuffers_,
                                           cellCullStage_.visibleCellBuffers);
        }

        if (leafCullPhase3Stage_.descriptorSets.empty() && leafCullPhase3Stage_.pipeline &&
            !treeFilterStage_.visibleTreeBuffers.empty()) {
            leafCullPhase3Stage_.createDescriptorSets(device_, allocator_, descriptorPool_,
                                                       maxFramesInFlight_);
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

        if (!treeFilterStage_.descriptorSets.empty()) {
            treeFilterStage_.updateTreeDataDescriptors(device_, maxFramesInFlight_, treeDataBuffers_);
        }
    }

    // Reset all 4 indirect draw commands (one per leaf type: oak, ash, aspen, pine)
    constexpr uint32_t NUM_LEAF_TYPES_LOCAL = 4;

    VkDrawIndexedIndirectCommand indirectReset[NUM_LEAF_TYPES_LOCAL] = {};
    for (uint32_t i = 0; i < NUM_LEAF_TYPES_LOCAL; ++i) {
        indirectReset[i].indexCount = 6;
        indirectReset[i].instanceCount = 0;
        indirectReset[i].firstIndex = 0;
        indirectReset[i].vertexOffset = 0;
        indirectReset[i].firstInstance = i * maxLeavesPerType_;
    }

    vkCmd.updateBuffer(cullIndirectBuffers_.getVk(frameIndex),
                       0, sizeof(indirectReset), &indirectReset);

    // Upload per-tree data to frame-specific buffers
    vkCmd.updateBuffer(treeDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeCullData), prep.treeData.data());
    vkCmd.updateBuffer(treeRenderDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeRenderDataGPU), prep.renderData.data());

    // Barrier for tree data buffer updates
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eUniformRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, barrier, {}, {});

    if (!isSpatialIndexEnabled() || !cellCullStage_.pipeline) return;
    if (!treeFilterStage_.isReady()) return;
    if (!leafCullPhase3Stage_.isReady()) return;

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
    vkCmd.fillBuffer(cellCullStage_.visibleCellBuffers.getVk(frameIndex), 0, sizeof(uint32_t), 0);

    constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
    uint32_t cellIndirectReset[4 + NUM_DISTANCE_BUCKETS * 2] = {0, 1, 1, 0};
    vkCmd.updateBuffer(cellCullStage_.indirectBuffers.getVk(frameIndex), 0, sizeof(cellIndirectReset), cellIndirectReset);

    // Reset tree filter and phase 3 buffers
    vkCmd.fillBuffer(treeFilterStage_.visibleTreeBuffers.getVk(frameIndex), 0, sizeof(uint32_t), 0);

    uint32_t leafDispatchReset[3] = {0, 1, 1};
    vkCmd.updateBuffer(treeFilterStage_.leafCullIndirectDispatchBuffers.getVk(frameIndex), 0, sizeof(leafDispatchReset), leafDispatchReset);

    // Upload cell cull uniforms
    vkCmd.updateBuffer(cellCullStage_.uniformBuffers.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &cellCulling);
    vkCmd.updateBuffer(cellCullStage_.paramsBuffers.buffers[frameIndex], 0,
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
    filterParams.maxVisibleTrees = treeFilterStage_.maxVisibleTrees;

    vkCmd.updateBuffer(treeFilterStage_.uniformBuffers.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &filterCulling);
    vkCmd.updateBuffer(treeFilterStage_.paramsBuffers.buffers[frameIndex], 0,
                       sizeof(TreeFilterParams), &filterParams);

    // Barrier for all buffer updates
    auto cellUniformBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eUniformRead | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, cellUniformBarrier, {}, {});

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **cellCullStage_.pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **cellCullStage_.pipelineLayout,
                             0, vk::DescriptorSet(cellCullStage_.descriptorSets[frameIndex]), {});

    uint32_t cellWorkgroups = ComputeConstants::getDispatchCount1D(cellParams.numCells);
    vkCmd.dispatch(cellWorkgroups, 1, 1);

    // --- Phase 2: Tree Filtering ---
    auto cellBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, cellBarrier, {}, {});

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **treeFilterStage_.pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **treeFilterStage_.pipelineLayout,
                             0, vk::DescriptorSet(treeFilterStage_.descriptorSets[frameIndex]), {});

    vkCmd.dispatchIndirect(cellCullStage_.indirectBuffers.getVk(frameIndex), 0);

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

    vkCmd.updateBuffer(leafCullPhase3Stage_.uniformBuffers.buffers[frameIndex], 0,
                       sizeof(CullingUniforms), &leafCulling);
    vkCmd.updateBuffer(leafCullPhase3Stage_.paramsBuffers.buffers[frameIndex], 0,
                       sizeof(LeafCullP3Params), &p3Params);

    auto p3UniformBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eUniformRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, p3UniformBarrier, {}, {});

    DescriptorManager::SetWriter writer(device_, leafCullPhase3Stage_.descriptorSets[frameIndex]);
    writer.writeBuffer(Bindings::LEAF_CULL_P3_VISIBLE_TREES, treeFilterStage_.visibleTreeBuffers.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_ALL_TREES, treeDataBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_OUTPUT, cullOutputBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INDIRECT, cullIndirectBuffers_.getVk(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_CULLING, leafCullPhase3Stage_.uniformBuffers.buffers[frameIndex], 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_PARAMS, leafCullPhase3Stage_.paramsBuffers.buffers[frameIndex], 0, sizeof(LeafCullP3Params), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **leafCullPhase3Stage_.pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **leafCullPhase3Stage_.pipelineLayout,
                             0, vk::DescriptorSet(leafCullPhase3Stage_.descriptorSets[frameIndex]), {});

    vkCmd.dispatchIndirect(treeFilterStage_.leafCullIndirectDispatchBuffers.getVk(frameIndex), 0);

    barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
           .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                          {}, barrier, {}, {});
}
