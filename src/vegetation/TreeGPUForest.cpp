#include "TreeGPUForest.h"
#include "TreeImpostorAtlas.h"
#include "core/DescriptorManager.h"
#include "core/ShaderLoader.h"
#include "core/VulkanBarriers.h"
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
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};

    // Source buffer (static tree data) - GPU only
    bufferInfo.size = maxTreeCount_ * sizeof(TreeSourceGPU);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &sourceBuffer_, &sourceAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Cluster buffer - GPU only
    // Estimate max clusters: 1km² with 50m cells = 400 clusters
    uint32_t maxClusters = 1000;
    bufferInfo.size = maxClusters * sizeof(ClusterDataGPU);
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &clusterBuffer_, &clusterAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Cluster visibility buffer - CPU writable, GPU readable
    bufferInfo.size = maxClusters * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo visAllocInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &clusterVisBuffer_, &clusterVisAllocation_, &visAllocInfo) != VK_SUCCESS) {
        return false;
    }
    clusterVisMapped_ = static_cast<uint32_t*>(visAllocInfo.pMappedData);

    // Tree-to-cluster mapping buffer - GPU only
    bufferInfo.size = maxTreeCount_ * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.flags = 0;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &treeClusterMapBuffer_, &treeClusterMapAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Full detail output buffer
    bufferInfo.size = maxFullDetailTrees_ * sizeof(TreeFullDetailGPU);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &fullDetailBuffer_, &fullDetailAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Double-buffered impostor output buffers (compute writes to one, graphics reads from other)
    bufferInfo.size = maxImpostorTrees_ * sizeof(TreeImpostorGPU);

    for (uint32_t i = 0; i < BUFFER_SET_COUNT; ++i) {
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                            &impostorBuffers_[i], &impostorAllocations_[i], nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // Double-buffered indirect draw buffers
    bufferInfo.size = sizeof(ForestIndirectCommands);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    for (uint32_t i = 0; i < BUFFER_SET_COUNT; ++i) {
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                            &indirectBuffers_[i], &indirectAllocations_[i], nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // Uniform buffer - CPU writable
    bufferInfo.size = sizeof(ForestUniformsGPU);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo uniformAllocInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &uniformBuffer_, &uniformAllocation_, &uniformAllocInfo) != VK_SUCCESS) {
        return false;
    }
    uniformsMapped_ = static_cast<ForestUniformsGPU*>(uniformAllocInfo.pMappedData);

    // Staging buffer for readback
    bufferInfo.size = sizeof(ForestIndirectCommands);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &stagingBuffer_, &stagingAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TreeGPUForest::createPipeline() {
    // Load compute shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_forest_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to load cull shader from %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule shaderModule = shaderModuleOpt.value();

    // Descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};

    // Binding 0: Tree source buffer
    bindings[0].binding = Bindings::TREE_FOREST_SOURCE;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Cluster buffer
    bindings[1].binding = Bindings::TREE_FOREST_CLUSTERS;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Cluster visibility
    bindings[2].binding = Bindings::TREE_FOREST_CLUSTER_VIS;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Full detail output
    bindings[3].binding = Bindings::TREE_FOREST_FULL_DETAIL;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Impostor output
    bindings[4].binding = Bindings::TREE_FOREST_IMPOSTORS;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: Indirect commands
    bindings[5].binding = Bindings::TREE_FOREST_INDIRECT;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: Uniforms
    bindings[6].binding = Bindings::TREE_FOREST_UNIFORMS;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 7: Tree-to-cluster mapping
    bindings[7].binding = Bindings::TREE_FOREST_TREE_CLUSTER;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        return false;
    }

    // Push constant for frame index
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 4;  // frameIndex + padding

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &cullPipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, shaderModule, nullptr);
        return false;
    }

    // Compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = cullPipelineLayout_;

    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cullPipeline_);
    vkDestroyShaderModule(device_, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TreeGPUForest::createDescriptorSets() {
    // Allocate descriptor sets
    for (uint32_t i = 0; i < 2; ++i) {
        descriptorSets_[i] = descriptorPool_->allocateSingle(descriptorSetLayout_);
        if (descriptorSets_[i] == VK_NULL_HANDLE) {
            return false;
        }

        // Write descriptors
        DescriptorManager::SetWriter writer(device_, descriptorSets_[i]);

        writer.writeBuffer(Bindings::TREE_FOREST_SOURCE, sourceBuffer_, 0,
                          maxTreeCount_ * sizeof(TreeSourceGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_CLUSTERS, clusterBuffer_, 0,
                          1000 * sizeof(ClusterDataGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_CLUSTER_VIS, clusterVisBuffer_, 0,
                          1000 * sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_FULL_DETAIL, fullDetailBuffer_, 0,
                          maxFullDetailTrees_ * sizeof(TreeFullDetailGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        // Initial binding uses writeBufferSet_; will be updated dynamically in recordCullingCompute
        writer.writeBuffer(Bindings::TREE_FOREST_IMPOSTORS, impostorBuffers_[writeBufferSet_], 0,
                          maxImpostorTrees_ * sizeof(TreeImpostorGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_INDIRECT, indirectBuffers_[writeBufferSet_], 0,
                          sizeof(ForestIndirectCommands), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_UNIFORMS, uniformBuffer_, 0,
                          sizeof(ForestUniformsGPU));
        writer.writeBuffer(Bindings::TREE_FOREST_TREE_CLUSTER, treeClusterMapBuffer_, 0,
                          maxTreeCount_ * sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        writer.update();
    }

    return true;
}

void TreeGPUForest::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDestroyPipeline(device_, cullPipeline_, nullptr);
    vkDestroyPipelineLayout(device_, cullPipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);

    auto destroyBuffer = [this](VkBuffer& buf, VmaAllocation& alloc) {
        if (buf != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, buf, alloc);
            buf = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
        }
    };

    destroyBuffer(sourceBuffer_, sourceAllocation_);
    destroyBuffer(clusterBuffer_, clusterAllocation_);
    destroyBuffer(clusterVisBuffer_, clusterVisAllocation_);
    destroyBuffer(treeClusterMapBuffer_, treeClusterMapAllocation_);
    destroyBuffer(fullDetailBuffer_, fullDetailAllocation_);

    // Destroy double-buffered impostor and indirect buffers
    for (uint32_t i = 0; i < BUFFER_SET_COUNT; ++i) {
        destroyBuffer(impostorBuffers_[i], impostorAllocations_[i]);
        destroyBuffer(indirectBuffers_[i], indirectAllocations_[i]);
    }

    destroyBuffer(uniformBuffer_, uniformAllocation_);
    destroyBuffer(stagingBuffer_, stagingAllocation_);
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
    vkCmdCopyBuffer(cmd, stagingBuf, sourceBuffer_, 1, &copyRegion);

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
    vkCmdCopyBuffer(cmd, stagingBuf, clusterBuffer_, 1, &copyRegion);

    // Copy tree-cluster mapping
    copyRegion.srcOffset = clusterSize;
    copyRegion.size = mapSize;
    vkCmdCopyBuffer(cmd, stagingBuf, treeClusterMapBuffer_, 1, &copyRegion);

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

    // Update uniforms
    uniformsMapped_->cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        uniformsMapped_->frustumPlanes[i] = frustumPlanes[i];
    }
    uniformsMapped_->fullDetailDistance = settings.fullDetailDistance;
    uniformsMapped_->impostorStartDistance = settings.fullDetailDistance;
    uniformsMapped_->impostorEndDistance = settings.fullDetailDistance + settings.blendRange;
    uniformsMapped_->cullDistance = settings.impostorDistance;
    uniformsMapped_->fullDetailBudget = settings.fullDetailBudget;
    uniformsMapped_->totalTreeCount = currentTreeCount_;
    uniformsMapped_->clusterCount = clusterCount_;
    uniformsMapped_->clusterImpostorDist = settings.clusterImpostorDistance;

    for (int i = 0; i < 4; ++i) {
        uniformsMapped_->archetypeBounds[i] = archetypeBounds_[i];
    }

    // Use write buffer set for compute output (double-buffering)
    VkBuffer writeIndirectBuffer = indirectBuffers_[writeBufferSet_];
    VkBuffer writeImpostorBuffer = impostorBuffers_[writeBufferSet_];

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

    // Update descriptor set to use current write buffer set
    // (double-buffering: compute writes to writeBufferSet_, graphics reads from readBufferSet_)
    {
        DescriptorManager::SetWriter writer(device_, descriptorSets_[frameIndex % 2]);
        writer.writeBuffer(Bindings::TREE_FOREST_IMPOSTORS, writeImpostorBuffer, 0,
                          maxImpostorTrees_ * sizeof(TreeImpostorGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_INDIRECT, writeIndirectBuffer, 0,
                          sizeof(ForestIndirectCommands), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.update();
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_,
                            0, 1, &descriptorSets_[frameIndex % 2], 0, nullptr);

    // Push constants
    uint32_t pushData[4] = {frameIndex, 0, 0, 0};
    vkCmdPushConstants(cmd, cullPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
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
