#include "TreeGPULODPipeline.h"
#include "TreeSystem.h"
#include "ShaderLoader.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>

std::unique_ptr<TreeGPULODPipeline> TreeGPULODPipeline::create(const InitInfo& info) {
    auto pipeline = std::unique_ptr<TreeGPULODPipeline>(new TreeGPULODPipeline());
    if (!pipeline->initInternal(info)) {
        return nullptr;
    }
    return pipeline;
}

TreeGPULODPipeline::~TreeGPULODPipeline() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    // Clean up buffers
    if (treeInstanceBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, treeInstanceBuffer_, treeInstanceAllocation_);
    }
    if (distanceKeyBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, distanceKeyBuffer_, distanceKeyAllocation_);
    }
    if (lodStateBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, lodStateBuffer_, lodStateAllocation_);
    }
    if (counterBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, counterBuffer_, counterAllocation_);
    }

    for (size_t i = 0; i < uniformBuffers_.size(); ++i) {
        if (uniformBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, uniformBuffers_[i], uniformAllocations_[i]);
        }
    }
}

bool TreeGPULODPipeline::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    computeQueue_ = info.computeQueue;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxFramesInFlight_ = info.maxFramesInFlight;
    maxTrees_ = info.maxTrees;

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create descriptor set layout");
        return false;
    }

    if (!createDistancePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create distance pipeline");
        return false;
    }

    if (!createSortPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create sort pipeline");
        return false;
    }

    if (!createSelectPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create select pipeline");
        return false;
    }

    if (!allocateDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to allocate descriptor sets");
        return false;
    }

    if (!createBuffers(maxTrees_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create buffers");
        return false;
    }

    pipelinesReady_ = true;
    SDL_Log("TreeGPULODPipeline: Initialized (max %u trees)", maxTrees_);
    return true;
}

bool TreeGPULODPipeline::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Tree instances SSBO
    bindings[0].binding = Bindings::TREE_LOD_INSTANCES;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Distance keys SSBO
    bindings[1].binding = Bindings::TREE_LOD_DISTANCE_KEYS;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // LOD states SSBO
    bindings[2].binding = Bindings::TREE_LOD_STATES;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Uniforms UBO
    bindings[3].binding = Bindings::TREE_LOD_UNIFORMS;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Counters SSBO
    bindings[4].binding = Bindings::TREE_LOD_COUNTERS;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return false;
    }
    descriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(device_, layout);

    return true;
}

bool TreeGPULODPipeline::createDistancePipeline() {
    // Create pipeline layout
    VkDescriptorSetLayout layouts[] = {descriptorSetLayout_.get()};

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = layouts;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    distancePipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, pipelineLayout);

    // Load shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_lod_distance.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to load distance shader: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule shaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = distancePipelineLayout_.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device_, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    distancePipeline_ = ManagedPipeline::fromRaw(device_, pipeline);

    return true;
}

bool TreeGPULODPipeline::createSortPipeline() {
    // Create pipeline layout with push constants
    VkDescriptorSetLayout layouts[] = {descriptorSetLayout_.get()};

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SortPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    sortPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, pipelineLayout);

    // Load shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_lod_sort.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to load sort shader: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule shaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = sortPipelineLayout_.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device_, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    sortPipeline_ = ManagedPipeline::fromRaw(device_, pipeline);

    return true;
}

bool TreeGPULODPipeline::createSelectPipeline() {
    // Create pipeline layout
    VkDescriptorSetLayout layouts[] = {descriptorSetLayout_.get()};

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = layouts;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    selectPipelineLayout_ = ManagedPipelineLayout::fromRaw(device_, pipelineLayout);

    // Load shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_lod_select.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to load select shader: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule shaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = selectPipelineLayout_.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device_, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    selectPipeline_ = ManagedPipeline::fromRaw(device_, pipeline);

    return true;
}

bool TreeGPULODPipeline::allocateDescriptorSets() {
    descriptorSets_ = descriptorPool_->allocate(descriptorSetLayout_.get(), maxFramesInFlight_);
    return !descriptorSets_.empty();
}

bool TreeGPULODPipeline::createBuffers(uint32_t maxTrees) {
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Tree instance buffer
    treeInstanceBufferSize_ = maxTrees * sizeof(TreeLODInstanceGPU);
    VkBufferCreateInfo instanceBufferInfo{};
    instanceBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    instanceBufferInfo.size = treeInstanceBufferSize_;
    instanceBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    instanceBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &instanceBufferInfo, &allocInfo,
                        &treeInstanceBuffer_, &treeInstanceAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Distance key buffer
    distanceKeyBufferSize_ = maxTrees * sizeof(TreeDistanceKey);
    VkBufferCreateInfo distanceKeyBufferInfo{};
    distanceKeyBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    distanceKeyBufferInfo.size = distanceKeyBufferSize_;
    distanceKeyBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    distanceKeyBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &distanceKeyBufferInfo, &allocInfo,
                        &distanceKeyBuffer_, &distanceKeyAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // LOD state buffer
    lodStateBufferSize_ = maxTrees * sizeof(TreeLODStateGPU);
    VkBufferCreateInfo lodStateBufferInfo{};
    lodStateBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    lodStateBufferInfo.size = lodStateBufferSize_;
    lodStateBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    lodStateBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &lodStateBufferInfo, &allocInfo,
                        &lodStateBuffer_, &lodStateAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Counter buffer (needs to be CPU-readable for debugging)
    VmaAllocationCreateInfo counterAllocInfo{};
    counterAllocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBufferCreateInfo counterBufferInfo{};
    counterBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    counterBufferInfo.size = sizeof(TreeDrawCounters);
    counterBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    counterBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vmaCreateBuffer(allocator_, &counterBufferInfo, &counterAllocInfo,
                        &counterBuffer_, &counterAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Per-frame uniform buffers
    VmaAllocationCreateInfo uniformAllocInfo{};
    uniformAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    uniformBuffers_.resize(maxFramesInFlight_);
    uniformAllocations_.resize(maxFramesInFlight_);

    VkBufferCreateInfo uniformBufferInfo{};
    uniformBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uniformBufferInfo.size = sizeof(TreeLODUniformsGPU);
    uniformBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uniformBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        if (vmaCreateBuffer(allocator_, &uniformBufferInfo, &uniformAllocInfo,
                            &uniformBuffers_[i], &uniformAllocations_[i], nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
        std::array<VkWriteDescriptorSet, 5> writes{};

        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = treeInstanceBuffer_;
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = Bindings::TREE_LOD_INSTANCES;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &instanceInfo;

        VkDescriptorBufferInfo distanceKeyInfo{};
        distanceKeyInfo.buffer = distanceKeyBuffer_;
        distanceKeyInfo.offset = 0;
        distanceKeyInfo.range = VK_WHOLE_SIZE;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = Bindings::TREE_LOD_DISTANCE_KEYS;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &distanceKeyInfo;

        VkDescriptorBufferInfo lodStateInfo{};
        lodStateInfo.buffer = lodStateBuffer_;
        lodStateInfo.offset = 0;
        lodStateInfo.range = VK_WHOLE_SIZE;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets_[i];
        writes[2].dstBinding = Bindings::TREE_LOD_STATES;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &lodStateInfo;

        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = uniformBuffers_[i];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(TreeLODUniformsGPU);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSets_[i];
        writes[3].dstBinding = Bindings::TREE_LOD_UNIFORMS;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &uniformInfo;

        VkDescriptorBufferInfo counterInfo{};
        counterInfo.buffer = counterBuffer_;
        counterInfo.offset = 0;
        counterInfo.range = sizeof(TreeDrawCounters);

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSets_[i];
        writes[4].dstBinding = Bindings::TREE_LOD_COUNTERS;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &counterInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

void TreeGPULODPipeline::uploadTreeInstances(
    const std::vector<TreeInstanceData>& trees,
    const std::vector<glm::vec3>& boundingBoxHalfExtents,
    const std::vector<float>& boundingSphereRadii) {

    if (trees.empty()) {
        currentTreeCount_ = 0;
        return;
    }

    currentTreeCount_ = static_cast<uint32_t>(trees.size());

    // Convert to GPU format
    std::vector<TreeLODInstanceGPU> gpuTrees(trees.size());
    for (size_t i = 0; i < trees.size(); ++i) {
        const auto& tree = trees[i];
        gpuTrees[i].positionScale = glm::vec4(tree.position, tree.scale);
        gpuTrees[i].rotationMeshInfo = glm::vec4(
            tree.rotation,
            static_cast<float>(tree.meshIndex),
            0.0f,  // archetypeIndex - will be set based on meshIndex
            0.0f   // flags
        );

        if (i < boundingBoxHalfExtents.size() && i < boundingSphereRadii.size()) {
            gpuTrees[i].boundingInfo = glm::vec4(boundingBoxHalfExtents[i], boundingSphereRadii[i]);
        } else {
            gpuTrees[i].boundingInfo = glm::vec4(5.0f, 10.0f, 5.0f, 15.0f);  // Default bounds
        }
    }

    // Upload via staging buffer
    VkDeviceSize uploadSize = trees.size() * sizeof(TreeLODInstanceGPU);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = uploadSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator_, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeGPULODPipeline: Failed to create staging buffer");
        return;
    }

    // Copy to staging
    void* data;
    vmaMapMemory(allocator_, stagingAllocation, &data);
    memcpy(data, gpuTrees.data(), uploadSize);
    vmaUnmapMemory(allocator_, stagingAllocation);

    // Record and submit copy command
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

    VkBufferCopy copyRegion{};
    copyRegion.size = uploadSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, treeInstanceBuffer_, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(computeQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(computeQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    SDL_Log("TreeGPULODPipeline: Uploaded %zu tree instances", trees.size());
}

uint32_t TreeGPULODPipeline::calculateSortStages(uint32_t n) {
    if (n <= 1) return 0;
    return static_cast<uint32_t>(std::ceil(std::log2(static_cast<double>(n))));
}

void TreeGPULODPipeline::recordBitonicSort(VkCommandBuffer cmd, uint32_t numElements) {
    if (numElements <= 1) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sortPipeline_.get());

    uint32_t numStages = calculateSortStages(numElements);
    uint32_t workgroupCount = (numElements + 255) / 256;

    for (uint32_t stage = 0; stage < numStages; ++stage) {
        for (uint32_t substage = 0; substage <= stage; ++substage) {
            SortPushConstants pushConstants;
            pushConstants.numElements = numElements;
            pushConstants.stage = stage;
            pushConstants.substage = substage;
            pushConstants._pad = 0;

            vkCmdPushConstants(cmd, sortPipelineLayout_.get(),
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(SortPushConstants), &pushConstants);

            vkCmdDispatch(cmd, workgroupCount, 1, 1);

            // Memory barrier between substages
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }
}

void TreeGPULODPipeline::recordLODCompute(VkCommandBuffer cmd,
                                           uint32_t frameIndex,
                                           const glm::vec3& cameraPos,
                                           const TreeLODSettings& settings) {
    if (!pipelinesReady_ || currentTreeCount_ == 0) {
        return;
    }

    // Upload uniforms
    TreeLODUniformsGPU uniforms{};
    uniforms.cameraPosition = glm::vec4(cameraPos, 0.0f);
    // frustumPlanes left at zero for now (future use)
    uniforms.numTrees = currentTreeCount_;
    uniforms.fullDetailBudget = settings.fullDetailBudget;
    uniforms.fullDetailDistance = settings.fullDetailDistance;
    uniforms.maxFullDetailDistance = static_cast<float>(settings.maxFullDetailDistance);
    uniforms.blendRange = settings.blendRange;
    uniforms.hysteresis = settings.hysteresis;

    void* data;
    vmaMapMemory(allocator_, uniformAllocations_[frameIndex], &data);
    memcpy(data, &uniforms, sizeof(TreeLODUniformsGPU));
    vmaUnmapMemory(allocator_, uniformAllocations_[frameIndex]);

    // Reset counters
    TreeDrawCounters resetCounters{0, 0, 0, 0};
    vkCmdUpdateBuffer(cmd, counterBuffer_, 0, sizeof(TreeDrawCounters), &resetCounters);

    // Barrier after counter reset
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, distancePipelineLayout_.get(),
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);

    // Stage 1: Distance calculation
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, distancePipeline_.get());
    uint32_t workgroupCount = (currentTreeCount_ + 255) / 256;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Barrier after distance calculation
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Stage 2: Bitonic sort
    // Re-bind descriptor set with sort pipeline layout
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sortPipelineLayout_.get(),
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);
    recordBitonicSort(cmd, currentTreeCount_);

    // Stage 3: LOD selection
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, selectPipelineLayout_.get(),
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, selectPipeline_.get());
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Final barrier before graphics
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

TreeDrawCounters TreeGPULODPipeline::readDrawCounters() {
    TreeDrawCounters counters{0, 0, 0, 0};

    void* data;
    if (vmaMapMemory(allocator_, counterAllocation_, &data) == VK_SUCCESS) {
        memcpy(&counters, data, sizeof(TreeDrawCounters));
        vmaUnmapMemory(allocator_, counterAllocation_);
    }

    return counters;
}
