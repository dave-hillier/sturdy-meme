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

    // Impostor output buffer
    bufferInfo.size = maxImpostorTrees_ * sizeof(TreeImpostorGPU);

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &impostorBuffer_, &impostorAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Indirect draw buffer
    bufferInfo.size = sizeof(ForestIndirectCommands);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &indirectBuffer_, &indirectAllocation_, nullptr) != VK_SUCCESS) {
        return false;
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
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, "shaders/tree_forest_cull.comp.spv");
    if (!shaderModuleOpt.has_value()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPUForest: Failed to load cull shader");
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
        writer.writeBuffer(Bindings::TREE_FOREST_IMPOSTORS, impostorBuffer_, 0,
                          maxImpostorTrees_ * sizeof(TreeImpostorGPU), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(Bindings::TREE_FOREST_INDIRECT, indirectBuffer_, 0,
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
    destroyBuffer(impostorBuffer_, impostorAllocation_);
    destroyBuffer(indirectBuffer_, indirectAllocation_);
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
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(worldMin.x, worldMax.x);
    std::uniform_real_distribution<float> distZ(worldMin.z, worldMax.z);
    std::uniform_real_distribution<float> distRot(0.0f, 6.28318f);
    std::uniform_real_distribution<float> distScale(0.8f, 1.2f);
    std::uniform_int_distribution<uint32_t> distArchetype(0, 3);
    std::uniform_real_distribution<float> distSeed(0.0f, 1.0f);

    std::vector<TreeSourceGPU> trees(treeCount);

    for (uint32_t i = 0; i < treeCount; ++i) {
        float x = distX(rng);
        float z = distZ(rng);
        float y = getHeight ? getHeight(x, z) : 0.0f;

        trees[i].positionScale = glm::vec4(x, y, z, distScale(rng));
        trees[i].rotationArchetype = glm::vec4(
            distRot(rng),
            static_cast<float>(distArchetype(rng)),
            distSeed(rng),
            0.0f
        );
    }

    uploadTreeData(trees);
    SDL_Log("TreeGPUForest: Generated %u procedural trees with terrain sampling", treeCount);
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

    // Clear indirect commands
    ForestIndirectCommands clearCmds{};
    clearCmds.fullDetailCmd.indexCount = 0;  // Will be set per-archetype
    clearCmds.fullDetailCmd.instanceCount = 0;
    clearCmds.fullDetailCmd.firstIndex = 0;
    clearCmds.fullDetailCmd.vertexOffset = 0;
    clearCmds.fullDetailCmd.firstInstance = 0;

    // Impostor uses indexed draw with billboard quad (6 indices for 2 triangles)
    clearCmds.impostorCmd.indexCount = 6;
    clearCmds.impostorCmd.instanceCount = 0;  // Compute shader fills this via atomicAdd
    clearCmds.impostorCmd.firstIndex = 0;
    clearCmds.impostorCmd.vertexOffset = 0;
    clearCmds.impostorCmd.firstInstance = 0;

    vkCmdUpdateBuffer(cmd, indirectBuffer_, 0, sizeof(ForestIndirectCommands), &clearCmds);

    // Barrier before compute
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);

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

    // Barrier before draw
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);
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
