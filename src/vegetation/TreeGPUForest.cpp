#include "TreeGPUForest.h"
#include "TreeImpostorAtlas.h"
#include "core/DescriptorManager.h"
#include "core/ShaderLoader.h"
#include "core/VulkanBarriers.h"
#include "core/PipelineBuilder.h"
#include "shaders/bindings.h"
#include <SDL3/SDL.h>
#include <random>
#include <algorithm>

std::unique_ptr<TreeGPUForest> TreeGPUForest::create(const InitInfo& info) {
    auto forest = std::unique_ptr<TreeGPUForest>(new TreeGPUForest());
    if (!forest->init(info)) {
        return nullptr;
    }
    return forest;
}

TreeGPUForest::~TreeGPUForest() {
    cleanup();
}

bool TreeGPUForest::init(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxTreeCount_ = info.maxTreeCount;
    maxFullDetailTrees_ = info.maxFullDetailTrees;
    maxImpostorTrees_ = info.maxImpostorTrees;

    // Initialize default archetype bounds
    for (auto& bounds : archetypeBounds_) {
        bounds = glm::vec4(10.0f, 15.0f, 0.0f, 0.0f);  // Default: 10m radius, 15m height
    }

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create buffers");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create pipeline");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create descriptor sets");
        return false;
    }

    initialized_ = true;
    SDL_Log("TreeGPUForest: Initialized for up to %u trees", maxTreeCount_);
    return true;
}

bool TreeGPUForest::createBuffers() {
    uint32_t maxClusters = 1000;

    // Source buffer (static tree data) - GPU only
    BufferUtils::SingleBufferBuilder sourceBuilder;
    if (!sourceBuilder.setAllocator(allocator_)
             .setSize(maxTreeCount_ * sizeof(TreeSourceGPU))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .setAllocationFlags(0)
             .build(sourceBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree source buffer");
        return false;
    }

    // Cluster buffer - GPU only
    BufferUtils::SingleBufferBuilder clusterBuilder;
    if (!clusterBuilder.setAllocator(allocator_)
             .setSize(maxClusters * sizeof(ClusterDataGPU))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .setAllocationFlags(0)
             .build(clusterBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cluster buffer");
        return false;
    }

    // Cluster visibility buffer - CPU writable, GPU readable (mapped)
    BufferUtils::SingleBufferBuilder clusterVisBuilder;
    if (!clusterVisBuilder.setAllocator(allocator_)
             .setSize(maxClusters * sizeof(uint32_t))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
             .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
             .build(clusterVisBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cluster visibility buffer");
        return false;
    }
    clusterVisMapped_ = static_cast<uint32_t*>(clusterVisBuffer_.mappedPointer);

    // Tree-to-cluster mapping buffer - GPU only
    BufferUtils::SingleBufferBuilder treeClusterBuilder;
    if (!treeClusterBuilder.setAllocator(allocator_)
             .setSize(maxTreeCount_ * sizeof(uint32_t))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .setAllocationFlags(0)
             .build(treeClusterMapBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree-cluster map buffer");
        return false;
    }

    // Full detail output buffer - GPU only
    BufferUtils::SingleBufferBuilder fullDetailBuilder;
    if (!fullDetailBuilder.setAllocator(allocator_)
             .setSize(maxFullDetailTrees_ * sizeof(TreeFullDetailGPU))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .setAllocationFlags(0)
             .build(fullDetailBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create full detail buffer");
        return false;
    }

    // Triple-buffered impostor output buffers
    BufferUtils::DoubleBufferedBufferBuilder impostorBuilder;
    if (!impostorBuilder.setAllocator(allocator_)
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(maxImpostorTrees_ * sizeof(TreeImpostorGPU))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .build(impostorBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create impostor buffers");
        return false;
    }

    // Triple-buffered indirect draw buffers
    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(allocator_)
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(sizeof(ForestIndirectCommands))
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
             .build(indirectBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect buffers");
        return false;
    }

    // Per-frame uniform buffers (triple-buffered, CPU writable)
    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(allocator_)
             .setFrameCount(BUFFER_SET_COUNT)
             .setSize(sizeof(ForestUniformsGPU))
             .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
             .build(uniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create uniform buffers");
        return false;
    }

    // Staging buffer for readback
    BufferUtils::SingleBufferBuilder stagingBuilder;
    if (!stagingBuilder.setAllocator(allocator_)
             .setSize(sizeof(ForestIndirectCommands))
             .setUsage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .setMemoryUsage(VMA_MEMORY_USAGE_GPU_TO_CPU)
             .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
             .build(stagingBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer");
        return false;
    }

    return true;
}

bool TreeGPUForest::createPipeline() {
    // Build descriptor set layout using PipelineBuilder
    PipelineBuilder layoutBuilder(device_);
    layoutBuilder.addDescriptorBinding(Bindings::TREE_FOREST_SOURCE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_CLUSTERS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_CLUSTER_VIS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_FULL_DETAIL, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_IMPOSTORS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(Bindings::TREE_FOREST_TREE_CLUSTER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    VkDescriptorSetLayout rawDescSetLayout = VK_NULL_HANDLE;
    if (!layoutBuilder.buildDescriptorSetLayout(rawDescSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create descriptor set layout");
        return false;
    }
    // Adopt raw handle into RAII wrapper
    descriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, rawDescSetLayout);

    // Build pipeline with PipelineBuilder
    PipelineBuilder pipelineBuilder(device_);
    pipelineBuilder.addShaderStage(resourcePath_ + "/shaders/tree_forest_cull.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 4);  // frameIndex + padding

    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (!pipelineBuilder.buildPipelineLayout({descriptorSetLayout_.get()}, rawPipelineLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create pipeline layout");
        return false;
    }
    cullPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, rawPipelineLayout);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!pipelineBuilder.buildComputePipeline(cullPipelineLayout_.get(), rawPipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create compute pipeline");
        return false;
    }
    cullPipeline_ = ManagedPipeline::fromRaw(device_, rawPipeline);

    return true;
}

bool TreeGPUForest::createDescriptorSets() {
    // Batch allocate descriptor sets - one per buffer set for triple-buffering
    descriptorSets_ = descriptorPool_->allocate(descriptorSetLayout_.get(), BUFFER_SET_COUNT);
    if (descriptorSets_.size() != BUFFER_SET_COUNT) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to allocate descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < BUFFER_SET_COUNT; ++i) {
        // Write descriptors - use non-fluent pattern to avoid copy semantics bug
        DescriptorManager::SetWriter writer(device_, descriptorSets_[i]);

        writer.writeBuffer(Bindings::TREE_FOREST_SOURCE, sourceBuffer_.buffer, 0,
                          maxTreeCount_ * sizeof(TreeSourceGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_CLUSTERS, clusterBuffer_.buffer, 0,
                          1000 * sizeof(ClusterDataGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_CLUSTER_VIS, clusterVisBuffer_.buffer, 0,
                          1000 * sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_FULL_DETAIL, fullDetailBuffer_.buffer, 0,
                          maxFullDetailTrees_ * sizeof(TreeFullDetailGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        // Each descriptor set binds to its corresponding buffer set (set 0 -> buffers[0], set 1 -> buffers[1], etc.)
        // This matches GrassSystem convention - no per-frame descriptor updates needed
        writer.writeBuffer(Bindings::TREE_FOREST_IMPOSTORS, impostorBuffers_.buffers[i], 0,
                          maxImpostorTrees_ * sizeof(TreeImpostorGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_INDIRECT, indirectBuffers_.buffers[i], 0,
                          sizeof(ForestIndirectCommands), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        // Use per-frame uniform buffer (frame i uses uniform buffer i)
        writer.writeBuffer(Bindings::TREE_FOREST_UNIFORMS, uniformBuffers_.buffers[i], 0,
                          sizeof(ForestUniformsGPU));
        writer.writeBuffer(Bindings::TREE_FOREST_TREE_CLUSTER, treeClusterMapBuffer_.buffer, 0,
                          maxTreeCount_ * sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        writer.update();
    }

    return true;
}

void TreeGPUForest::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    // RAII wrappers (cullPipeline_, cullPipelineLayout_, descriptorSetLayout_)
    // are automatically cleaned up when the object is destroyed

    // Destroy single buffers
    BufferUtils::destroyBuffer(allocator_, sourceBuffer_);
    BufferUtils::destroyBuffer(allocator_, clusterBuffer_);
    BufferUtils::destroyBuffer(allocator_, clusterVisBuffer_);
    clusterVisMapped_ = nullptr;
    BufferUtils::destroyBuffer(allocator_, treeClusterMapBuffer_);
    BufferUtils::destroyBuffer(allocator_, fullDetailBuffer_);

    // Destroy triple-buffered output buffers
    BufferUtils::destroyBuffers(allocator_, impostorBuffers_);
    BufferUtils::destroyBuffers(allocator_, indirectBuffers_);

    // Destroy per-frame uniform buffers
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);

    // Destroy staging buffer
    BufferUtils::destroyBuffer(allocator_, stagingBuffer_);
}

void TreeGPUForest::generateProceduralForest(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                              uint32_t treeCount, uint32_t seed) {
    // Call the overload with no height function (trees at y=0)
    generateProceduralForest(worldMin, worldMax, treeCount, nullptr, seed);
}

void TreeGPUForest::generateProceduralForest(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                              uint32_t treeCount, const HeightFunction& getHeight, uint32_t seed) {
    // Poisson disc sampling with minimum spacing
    constexpr float minSpacing = 8.0f;  // Minimum distance between trees
    constexpr float minHeight = 22.0f;  // Don't place trees below this height
    constexpr int maxAttempts = 30;     // Attempts per active sample

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distUnit(0.0f, 1.0f);
    std::uniform_real_distribution<float> distRot(0.0f, 6.28318f);
    std::uniform_real_distribution<float> distScale(0.8f, 1.2f);
    std::uniform_int_distribution<uint32_t> distArchetype(0, 3);

    float worldWidth = worldMax.x - worldMin.x;
    float worldDepth = worldMax.z - worldMin.z;

    // Grid for spatial lookup (cell size = minSpacing / sqrt(2))
    float cellSize = minSpacing / 1.414f;
    int gridWidth = static_cast<int>(std::ceil(worldWidth / cellSize));
    int gridDepth = static_cast<int>(std::ceil(worldDepth / cellSize));

    // Grid stores index into trees vector, -1 = empty
    std::vector<int> grid(gridWidth * gridDepth, -1);
    std::vector<TreeSourceGPU> trees;
    trees.reserve(treeCount);

    // Active list for Poisson disc sampling
    std::vector<size_t> active;

    // Helper to get grid index
    auto toGrid = [&](float x, float z) -> std::pair<int, int> {
        int gx = static_cast<int>((x - worldMin.x) / cellSize);
        int gz = static_cast<int>((z - worldMin.z) / cellSize);
        return {std::clamp(gx, 0, gridWidth - 1), std::clamp(gz, 0, gridDepth - 1)};
    };

    // Check if position is valid (no nearby trees within minSpacing)
    auto isValidPosition = [&](float x, float z) -> bool {
        auto [gx, gz] = toGrid(x, z);

        // Check 5x5 neighborhood
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = gx + dx;
                int nz = gz + dz;
                if (nx < 0 || nx >= gridWidth || nz < 0 || nz >= gridDepth) continue;

                int idx = grid[nz * gridWidth + nx];
                if (idx >= 0) {
                    const auto& other = trees[idx];
                    float ox = other.positionScale.x;
                    float oz = other.positionScale.z;
                    float dist2 = (x - ox) * (x - ox) + (z - oz) * (z - oz);
                    if (dist2 < minSpacing * minSpacing) {
                        return false;
                    }
                }
            }
        }
        return true;
    };

    // Add a tree at position
    auto addTree = [&](float x, float z) {
        float y = getHeight ? getHeight(x, z) : 0.0f;

        // Skip if below minimum height (water/beach level)
        if (y < minHeight) return;

        TreeSourceGPU tree;
        tree.positionScale = glm::vec4(x, y, z, distScale(rng));
        tree.rotationArchetype = glm::vec4(
            distRot(rng),
            static_cast<float>(distArchetype(rng)),
            distUnit(rng),  // seed
            0.0f
        );

        auto [gx, gz] = toGrid(x, z);
        grid[gz * gridWidth + gx] = static_cast<int>(trees.size());
        trees.push_back(tree);
        active.push_back(trees.size() - 1);
    };

    // Find valid starting points near the world center (where player spawns)
    // Search outward from center to find a point above minimum height
    bool foundStart = false;
    float searchRadius = 100.0f;  // Start searching close to center
    float maxSearchRadius = std::min(worldWidth, worldDepth) * 0.5f;

    while (!foundStart && searchRadius < maxSearchRadius) {
        for (int startAttempt = 0; startAttempt < 100 && !foundStart; ++startAttempt) {
            // Random angle and distance from center
            float angle = distUnit(rng) * 6.28318f;
            float dist = distUnit(rng) * searchRadius;
            float startX = std::cos(angle) * dist;
            float startZ = std::sin(angle) * dist;

            // Clamp to world bounds
            startX = std::clamp(startX, worldMin.x, worldMax.x);
            startZ = std::clamp(startZ, worldMin.z, worldMax.z);

            float startY = getHeight ? getHeight(startX, startZ) : 0.0f;

            if (startY >= minHeight) {
                TreeSourceGPU tree;
                tree.positionScale = glm::vec4(startX, startY, startZ, distScale(rng));
                tree.rotationArchetype = glm::vec4(
                    distRot(rng),
                    static_cast<float>(distArchetype(rng)),
                    distUnit(rng),
                    0.0f
                );

                auto [gx, gz] = toGrid(startX, startZ);
                grid[gz * gridWidth + gx] = static_cast<int>(trees.size());
                trees.push_back(tree);
                active.push_back(trees.size() - 1);
                foundStart = true;
                SDL_Log("TreeGPUForest: Starting point at (%.1f, %.1f, %.1f) at search radius %.1f",
                        startX, startY, startZ, searchRadius);
            }
        }
        searchRadius *= 2.0f;  // Expand search radius
    }

    if (!foundStart) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TreeGPUForest: Could not find valid starting point above %.1f", minHeight);
        return;
    }

    // Poisson disc sampling main loop
    while (!active.empty() && trees.size() < treeCount) {
        // Pick random active sample
        size_t activeIdx = static_cast<size_t>(distUnit(rng) * active.size());
        size_t sampleIdx = active[activeIdx];
        const auto& sample = trees[sampleIdx];
        float sx = sample.positionScale.x;
        float sz = sample.positionScale.z;

        bool foundValid = false;
        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            // Generate random point in annulus [minSpacing, 2*minSpacing]
            float angle = distUnit(rng) * 6.28318f;
            float radius = minSpacing + distUnit(rng) * minSpacing;
            float nx = sx + std::cos(angle) * radius;
            float nz = sz + std::sin(angle) * radius;

            // Check bounds
            if (nx < worldMin.x || nx > worldMax.x || nz < worldMin.z || nz > worldMax.z) {
                continue;
            }

            // Check spacing
            if (isValidPosition(nx, nz)) {
                addTree(nx, nz);
                foundValid = true;
                break;
            }
        }

        // Remove from active if no valid point found
        if (!foundValid) {
            active[activeIdx] = active.back();
            active.pop_back();
        }
    }

    if (trees.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: No valid tree positions found (all below min height %.1f)", minHeight);
        return;
    }

    // Log tree distribution bounds for debugging
    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());
    for (const auto& tree : trees) {
        minPos = glm::min(minPos, glm::vec3(tree.positionScale));
        maxPos = glm::max(maxPos, glm::vec3(tree.positionScale));
    }
    SDL_Log("TreeGPUForest: Tree bounds: (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
            minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);

    uploadTreeData(trees);
    SDL_Log("TreeGPUForest: Generated %zu procedural trees (Poisson disc, minHeight=%.1f, minSpacing=%.1f)",
            trees.size(), minHeight, minSpacing);
}

void TreeGPUForest::uploadTreeData(const std::vector<TreeSourceGPU>& trees) {
    if (trees.empty() || trees.size() > maxTreeCount_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TreeGPUForest: Invalid tree count %zu (max %u)",
                     trees.size(), maxTreeCount_);
        return;
    }

    currentTreeCount_ = static_cast<uint32_t>(trees.size());
    VkDeviceSize dataSize = trees.size() * sizeof(TreeSourceGPU);

    // Create staging buffer
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = dataSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo allocInfo;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo,
                        &stagingBuf, &stagingAlloc, &allocInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create staging buffer");
        return;
    }

    // Copy data to staging
    memcpy(allocInfo.pMappedData, trees.data(), dataSize);

    // Allocate command buffer for transfer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to allocate command buffer");
        vmaDestroyBuffer(allocator_, stagingBuf, stagingAlloc);
        return;
    }

    // Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    // Copy staging buffer to source buffer
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = dataSize;
    vkCmdCopyBuffer(cmd, stagingBuf, sourceBuffer_.buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    // Cleanup
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuf, stagingAlloc);

    SDL_Log("TreeGPUForest: Uploaded %u trees (%.1f MB) to GPU",
            currentTreeCount_, dataSize / (1024.0f * 1024.0f));
}

void TreeGPUForest::setArchetypeBounds(uint32_t archetype, const glm::vec3& halfExtents, float baseOffset) {
    if (archetype < 4) {
        archetypeBounds_[archetype] = glm::vec4(halfExtents, baseOffset);
    }
}

void TreeGPUForest::buildClusterGrid(float cellSize) {
    if (currentTreeCount_ == 0) return;

    // For now, create a single cluster containing all trees
    // This ensures all trees are visible and the shader doesn't read garbage data
    // A proper implementation would create a spatial grid

    clusterCount_ = 1;
    clusterInfos_.resize(1);
    clusterInfos_[0].center = glm::vec3(0.0f, 0.0f, 0.0f);  // World center
    clusterInfos_[0].radius = 20000.0f;  // Large enough to contain all trees
    clusterInfos_[0].treeCount = currentTreeCount_;
    clusterInfos_[0].firstTreeIndex = 0;

    // Create tree-to-cluster mapping (all trees in cluster 0)
    std::vector<uint32_t> treeClusterMap(currentTreeCount_, 0);

    // Upload cluster data to GPU
    std::vector<ClusterDataGPU> clusterData(1);
    clusterData[0].centerRadius = glm::vec4(0.0f, 0.0f, 0.0f, 20000.0f);
    clusterData[0].minBounds = glm::vec4(-10000.0f, 0.0f, -10000.0f, static_cast<float>(currentTreeCount_));
    clusterData[0].maxBounds = glm::vec4(10000.0f, 500.0f, 10000.0f, 0.0f);

    // Upload via staging buffers
    VkDeviceSize clusterSize = sizeof(ClusterDataGPU);
    VkDeviceSize mapSize = currentTreeCount_ * sizeof(uint32_t);
    VkDeviceSize totalSize = clusterSize + mapSize;

    // Create staging buffer
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = totalSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo allocInfo;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo,
                        &stagingBuf, &stagingAlloc, &allocInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to create staging buffer for clusters");
        return;
    }

    // Copy data to staging
    uint8_t* data = static_cast<uint8_t*>(allocInfo.pMappedData);
    memcpy(data, clusterData.data(), clusterSize);
    memcpy(data + clusterSize, treeClusterMap.data(), mapSize);

    // Allocate command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    // Copy cluster data
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = clusterSize;
    vkCmdCopyBuffer(cmd, stagingBuf, clusterBuffer_.buffer, 1, &copyRegion);

    // Copy tree-cluster mapping
    copyRegion.srcOffset = clusterSize;
    copyRegion.size = mapSize;
    vkCmdCopyBuffer(cmd, stagingBuf, treeClusterMapBuffer_.buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuf, stagingAlloc);

    // Initialize cluster visibility (all visible)
    if (clusterVisMapped_) {
        clusterVisMapped_[0] = 1;  // visible, not forcing impostor
    }

    SDL_Log("TreeGPUForest: Built cluster grid with %u clusters (cell size %.1f)", clusterCount_, cellSize);
}

void TreeGPUForest::updateClusterVisibility(const glm::vec3& cameraPos,
                                             const std::array<glm::vec4, 6>& frustumPlanes,
                                             float clusterCullDistance,
                                             float clusterImpostorDistance) {
    if (!clusterVisMapped_ || clusterCount_ == 0) return;

    // Update cluster visibility on CPU, write to mapped buffer
    for (uint32_t i = 0; i < clusterCount_; ++i) {
        const auto& cluster = clusterInfos_[i];

        float distance = glm::length(cluster.center - cameraPos);
        bool visible = true;
        bool forceImpostor = false;

        // Distance culling
        if (distance > clusterCullDistance + cluster.radius) {
            visible = false;
        }

        // Frustum culling
        if (visible) {
            for (const auto& plane : frustumPlanes) {
                float d = glm::dot(glm::vec3(plane), cluster.center) + plane.w;
                if (d < -cluster.radius) {
                    visible = false;
                    break;
                }
            }
        }

        // Force impostor for distant clusters
        if (visible && distance > clusterImpostorDistance) {
            forceImpostor = true;
        }

        // Pack visibility: bit 0 = visible, bit 1 = forceImpostor
        clusterVisMapped_[i] = (visible ? 1u : 0u) | (forceImpostor ? 2u : 0u);
    }
}

void TreeGPUForest::recordCullingCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                                          const glm::vec3& cameraPos,
                                          const std::array<glm::vec4, 6>& frustumPlanes,
                                          const TreeLODSettings& settings) {
    if (!initialized_ || currentTreeCount_ == 0) return;

    // Update uniforms to the per-frame uniform buffer (uses writeBufferSet_ to match descriptor set)
    ForestUniformsGPU* uniforms = static_cast<ForestUniformsGPU*>(uniformBuffers_.mappedPointers[writeBufferSet_]);
    uniforms->cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        uniforms->frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms->fullDetailDistance = settings.fullDetailDistance;
    uniforms->impostorStartDistance = settings.fullDetailDistance;
    uniforms->impostorEndDistance = settings.fullDetailDistance + settings.blendRange;
    uniforms->cullDistance = settings.impostorDistance;
    uniforms->fullDetailBudget = settings.fullDetailBudget;
    uniforms->totalTreeCount = currentTreeCount_;
    uniforms->clusterCount = clusterCount_;
    uniforms->clusterImpostorDist = settings.clusterImpostorDistance;

    for (int i = 0; i < 4; ++i) {
        uniforms->archetypeBounds[i] = archetypeBounds_[i];
    }

    // Use write buffer set for compute output (triple-buffering)
    VkBuffer writeIndirectBuffer = indirectBuffers_.buffers[writeBufferSet_];

    // Clear indirect buffer using vkCmdFillBuffer (more reliable than vkCmdUpdateBuffer)
    // This zeros out the entire buffer including all instanceCount fields
    Barriers::clearBufferForComputeReadWrite(cmd, writeIndirectBuffer, 0, sizeof(ForestIndirectCommands));

    // Now set the non-zero fields via vkCmdUpdateBuffer
    // impostorCmd.indexCount = 6 (6 indices for billboard quad)
    // Offset: fullDetailCmd is 20 bytes, impostorCmd.indexCount is at offset 20
    uint32_t impostorIndexCount = 6;
    vkCmdUpdateBuffer(cmd, writeIndirectBuffer,
                      offsetof(ForestIndirectCommands, impostorCmd) + offsetof(VkDrawIndexedIndirectCommand, indexCount),
                      sizeof(uint32_t), &impostorIndexCount);

    // Barrier after the update
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    // Bind pipeline and descriptor set
    // Use writeBufferSet_ to select descriptor set (matches GrassSystem convention)
    // Each descriptor set is permanently bound to its buffer set at init time
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_.get(),
                            0, 1, &descriptorSets_[writeBufferSet_], 0, nullptr);

    // Push constants
    uint32_t pushData[4] = {frameIndex, 0, 0, 0};
    vkCmdPushConstants(cmd, cullPipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushData), pushData);

    // Dispatch: 256 threads per workgroup
    uint32_t workgroupCount = (currentTreeCount_ + 255) / 256;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Barrier: compute write -> indirect draw + vertex input
    Barriers::computeToIndirectDraw(cmd);
}

uint32_t TreeGPUForest::readFullDetailCount() {
    // This would need GPU sync - expensive!
    // For debugging only
    return 0;
}

uint32_t TreeGPUForest::readImpostorCount() {
    // This would need GPU sync - expensive!
    // For debugging only
    return 0;
}
