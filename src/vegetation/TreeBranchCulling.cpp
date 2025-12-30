#include "TreeBranchCulling.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ShaderLoader.h"
#include "Bindings.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <cstring>

std::unique_ptr<TreeBranchCulling> TreeBranchCulling::create(const InitInfo& info) {
    auto culling = std::unique_ptr<TreeBranchCulling>(new TreeBranchCulling());
    if (!culling->init(info)) {
        return nullptr;
    }
    return culling;
}

TreeBranchCulling::~TreeBranchCulling() {
    // outputBuffers_ and indirectBuffers_ are FrameIndexedBuffers
    // which clean up automatically via their destructor

    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);

    if (inputBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, inputBuffer_, inputAllocation_);
    }
    if (meshGroupBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, meshGroupBuffer_, meshGroupAllocation_);
    }
}

bool TreeBranchCulling::init(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxFramesInFlight_ = info.maxFramesInFlight;
    maxTrees_ = info.maxTrees;
    maxMeshGroups_ = info.maxMeshGroups;

    if (!createCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TreeBranchCulling: Culling pipeline not available, using direct rendering");
        return true; // Graceful degradation
    }

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create buffers");
        return false;
    }

    SDL_Log("TreeBranchCulling initialized successfully");
    return true;
}

bool TreeBranchCulling::createCullPipeline() {
    // Create descriptor set layout
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_BRANCH_SHADOW_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_BRANCH_SHADOW_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_BRANCH_SHADOW_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_BRANCH_SHADOW_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_BRANCH_SHADOW_GROUPS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    if (!builder.buildManaged(cullDescriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TreeBranchCulling: Failed to create descriptor set layout");
        return false;
    }

    if (!DescriptorManager::createManagedPipelineLayout(device_, cullDescriptorSetLayout_.get(), cullPipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TreeBranchCulling: Failed to create pipeline layout");
        return false;
    }

    std::string shaderPath = resourcePath_ + "/shaders/tree_branch_shadow_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TreeBranchCulling: Cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    VkShaderModule computeShaderModule = shaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = cullPipelineLayout_.get();

    VkPipeline rawPipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(device_, computeShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TreeBranchCulling: Failed to create compute pipeline");
        return false;
    }
    cullPipeline_ = ManagedPipeline::fromRaw(device_, rawPipeline);

    SDL_Log("TreeBranchCulling: Created branch shadow culling compute pipeline");
    return true;
}

bool TreeBranchCulling::createBuffers() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};

    // Input buffer: all tree transforms (CPU-writable)
    inputBufferSize_ = maxTrees_ * sizeof(BranchShadowInputGPU);
    bufferInfo.size = inputBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &inputBuffer_, &inputAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create input buffer");
        return false;
    }

    // Mesh group metadata buffer
    bufferInfo.size = maxMeshGroups_ * sizeof(BranchMeshGroupGPU);
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &meshGroupBuffer_, &meshGroupAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create mesh group buffer");
        return false;
    }

    // Output buffers: triple-buffered visible instances using FrameIndexedBuffers
    outputBufferSize_ = maxTrees_ * sizeof(BranchShadowInstanceGPU);
    if (!outputBuffers_.resize(
            allocator_, maxFramesInFlight_, outputBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create output buffers");
        return false;
    }

    // Indirect draw command buffers using FrameIndexedBuffers
    vk::DeviceSize indirectBufferSize = maxMeshGroups_ * sizeof(VkDrawIndexedIndirectCommand);
    if (!indirectBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create indirect buffers");
        return false;
    }

    // Uniform buffers (per-frame)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(BranchShadowCullUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(uniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeBranchCulling: Failed to create uniform buffers");
        return false;
    }

    return true;
}

void TreeBranchCulling::updateDescriptorSets() {
    if (descriptorSetsInitialized_) return;

    cullDescriptorSets_ = descriptorPool_->allocate(cullDescriptorSetLayout_.get(), maxFramesInFlight_);
    if (cullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TreeBranchCulling: Failed to allocate descriptor sets");
        return;
    }

    for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {

        // Input buffer (binding 0)
        VkDescriptorBufferInfo inputBufferInfo{};
        inputBufferInfo.buffer = inputBuffer_;
        inputBufferInfo.offset = 0;
        inputBufferInfo.range = inputBufferSize_;

        // Output buffer (binding 1) - using FrameIndexedBuffers for type-safe access
        VkDescriptorBufferInfo outputBufferInfo{};
        outputBufferInfo.buffer = outputBuffers_.getVk(i);
        outputBufferInfo.offset = 0;
        outputBufferInfo.range = static_cast<VkDeviceSize>(outputBufferSize_);

        // Indirect buffer (binding 2) - using FrameIndexedBuffers for type-safe access
        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers_.getVk(i);
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = maxMeshGroups_ * sizeof(VkDrawIndexedIndirectCommand);

        // Uniform buffer (binding 3)
        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers_.buffers[i];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(BranchShadowCullUniforms);

        // Mesh group buffer (binding 4)
        VkDescriptorBufferInfo meshGroupBufferInfo{};
        meshGroupBufferInfo.buffer = meshGroupBuffer_;
        meshGroupBufferInfo.offset = 0;
        meshGroupBufferInfo.range = maxMeshGroups_ * sizeof(BranchMeshGroupGPU);

        std::array<VkWriteDescriptorSet, 5> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cullDescriptorSets_[i];
        writes[0].dstBinding = Bindings::TREE_BRANCH_SHADOW_INPUT;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &inputBufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cullDescriptorSets_[i];
        writes[1].dstBinding = Bindings::TREE_BRANCH_SHADOW_OUTPUT;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &outputBufferInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = cullDescriptorSets_[i];
        writes[2].dstBinding = Bindings::TREE_BRANCH_SHADOW_INDIRECT;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &indirectBufferInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = cullDescriptorSets_[i];
        writes[3].dstBinding = Bindings::TREE_BRANCH_SHADOW_UNIFORMS;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].pBufferInfo = &uniformBufferInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = cullDescriptorSets_[i];
        writes[4].dstBinding = Bindings::TREE_BRANCH_SHADOW_GROUPS;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo = &meshGroupBufferInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    descriptorSetsInitialized_ = true;
}

void TreeBranchCulling::updateTreeData(const TreeSystem& treeSystem, const TreeLODSystem* lodSystem) {
    // Guard: buffers may not exist if pipeline creation failed (graceful degradation)
    if (inputBuffer_ == VK_NULL_HANDLE || meshGroupBuffer_ == VK_NULL_HANDLE) {
        return;
    }

    const auto& instances = treeSystem.getTreeInstances();
    const auto& branchRenderables = treeSystem.getBranchRenderables();

    if (instances.empty() || branchRenderables.empty()) {
        numTrees_ = 0;
        meshGroups_.clear();
        meshGroupRenderInfo_.clear();
        return;
    }

    numTrees_ = static_cast<uint32_t>(instances.size());

    // Build mesh groups by archetype/mesh index
    std::unordered_map<uint32_t, std::vector<uint32_t>> treesByMesh;
    for (uint32_t i = 0; i < numTrees_; ++i) {
        treesByMesh[instances[i].meshIndex].push_back(i);
    }

    // Build mesh group metadata
    meshGroups_.clear();
    meshGroupRenderInfo_.clear();
    uint32_t outputOffset = 0;

    for (const auto& [meshIndex, treeIndices] : treesByMesh) {
        if (meshIndex >= branchRenderables.size()) continue;

        const auto& renderable = branchRenderables[meshIndex];
        if (!renderable.mesh) continue;

        BranchMeshGroupGPU group{};
        group.meshIndex = meshIndex;
        group.firstTree = treeIndices.front();
        group.treeCount = static_cast<uint32_t>(treeIndices.size());
        group.barkTypeIndex = 0; // Default, could be extracted from barkType string
        group.indexCount = renderable.mesh->getIndexCount();
        group.maxInstances = static_cast<uint32_t>(treeIndices.size());
        group.outputOffset = outputOffset;

        // Determine bark type index from string
        const std::string& barkType = branchRenderables[meshIndex].barkType;
        if (barkType == "birch") group.barkTypeIndex = 0;
        else if (barkType == "oak") group.barkTypeIndex = 1;
        else if (barkType == "pine") group.barkTypeIndex = 2;
        else if (barkType == "willow") group.barkTypeIndex = 3;

        meshGroups_.push_back(group);

        MeshGroupRenderInfo info{};
        info.meshIndex = meshIndex;
        info.barkTypeIndex = group.barkTypeIndex;
        info.indirectOffset = (meshGroups_.size() - 1) * sizeof(VkDrawIndexedIndirectCommand);
        info.instanceOffset = outputOffset;
        meshGroupRenderInfo_.push_back(info);

        outputOffset += group.maxInstances;
    }

    // Pre-compute bounding sphere radius for each mesh (local-space, unscaled)
    // This is the half-diagonal of the mesh AABB, ensuring the entire mesh fits
    std::unordered_map<uint32_t, float> meshBoundingRadius;
    for (const auto& [meshIndex, treeIndices] : treesByMesh) {
        const Mesh& mesh = treeSystem.getBranchMesh(meshIndex);
        const AABB& bounds = mesh.getBounds();
        glm::vec3 extents = bounds.getExtents();
        float radius = glm::length(extents);
        meshBoundingRadius[meshIndex] = radius;
    }

    // Upload input data
    void* mappedInput = nullptr;
    vmaMapMemory(allocator_, inputAllocation_, &mappedInput);

    auto* inputData = static_cast<BranchShadowInputGPU*>(mappedInput);
    for (uint32_t i = 0; i < numTrees_; ++i) {
        const auto& inst = instances[i];
        // Get the pre-computed bounding radius for this mesh (local-space)
        float boundingRadius = meshBoundingRadius[inst.meshIndex];
        inputData[i].positionAndScale = glm::vec4(inst.position, inst.scale);
        inputData[i].rotationAndArchetype = glm::vec4(
            inst.rotation,
            glm::uintBitsToFloat(inst.meshIndex),
            glm::uintBitsToFloat(inst.archetypeIndex),
            boundingRadius  // Pass actual mesh bounding radius to GPU
        );
    }

    vmaUnmapMemory(allocator_, inputAllocation_);

    // Upload mesh group metadata
    void* mappedGroups = nullptr;
    vmaMapMemory(allocator_, meshGroupAllocation_, &mappedGroups);
    std::memcpy(mappedGroups, meshGroups_.data(), meshGroups_.size() * sizeof(BranchMeshGroupGPU));
    vmaUnmapMemory(allocator_, meshGroupAllocation_);

    // Initialize descriptor sets if needed
    if (cullPipeline_.get() != VK_NULL_HANDLE) {
        updateDescriptorSets();
    }
}

void TreeBranchCulling::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                       uint32_t cascadeIndex,
                                       const glm::vec4* cascadeFrustumPlanes,
                                       const glm::vec3& cameraPos,
                                       const TreeLODSystem* lodSystem) {
    if (!isEnabled() || numTrees_ == 0 || meshGroups_.empty()) return;

    // Guard: descriptor sets must be initialized before dispatch
    if (!descriptorSetsInitialized_ || cullDescriptorSets_.empty() ||
        frameIndex >= cullDescriptorSets_.size()) {
        return;
    }

    // Reset indirect draw commands on CPU side BEFORE dispatch.
    // This is critical: the shader's barrier() only syncs within a workgroup,
    // so other workgroups may atomicAdd before workgroup 0 finishes initialization.
    // This was the root cause of tree corruption/flickering in the woods.
    std::vector<VkDrawIndexedIndirectCommand> resetCmds(meshGroups_.size());
    for (size_t g = 0; g < meshGroups_.size(); ++g) {
        resetCmds[g].indexCount = meshGroups_[g].indexCount;
        resetCmds[g].instanceCount = 0;  // Will be incremented atomically by shader
        resetCmds[g].firstIndex = 0;
        resetCmds[g].vertexOffset = 0;
        resetCmds[g].firstInstance = 0;
    }
    {
        vk::CommandBuffer vkCmdReset(cmd);
        vkCmdReset.updateBuffer(indirectBuffers_.getVk(frameIndex), 0,
                                vk::ArrayProxy<const VkDrawIndexedIndirectCommand>(resetCmds));
    }

    // Memory barrier to ensure buffer update completes before compute shader reads
    VkMemoryBarrier resetBarrier{};
    resetBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &resetBarrier, 0, nullptr, 0, nullptr);

    // Update uniforms
    BranchShadowCullUniforms uniforms{};
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    for (int i = 0; i < 6; ++i) {
        uniforms.cascadeFrustumPlanes[i] = cascadeFrustumPlanes[i];
    }
    uniforms.fullDetailDistance = lodSystem ? lodSystem->getLODSettings().fullDetailDistance
                                            : TreeLODConstants::FULL_DETAIL_DISTANCE;
    uniforms.hysteresis = lodSystem ? lodSystem->getLODSettings().hysteresis
                                    : TreeLODConstants::HYSTERESIS;
    uniforms.cascadeIndex = cascadeIndex;
    uniforms.numTrees = numTrees_;
    uniforms.numMeshGroups = static_cast<uint32_t>(meshGroups_.size());

    void* mappedUniforms = nullptr;
    vmaMapMemory(allocator_, uniformBuffers_.allocations[frameIndex], &mappedUniforms);
    std::memcpy(mappedUniforms, &uniforms, sizeof(uniforms));
    vmaUnmapMemory(allocator_, uniformBuffers_.allocations[frameIndex]);

    // Bind pipeline and descriptor set
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, cullPipeline_.get());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cullPipelineLayout_.get(),
                             0, vk::DescriptorSet(cullDescriptorSets_[frameIndex]), {});

    // Dispatch: one workgroup per 256 trees
    uint32_t numWorkgroups = (numTrees_ + 255) / 256;
    vkCmd.dispatch(numWorkgroups, 1, 1);

    // Memory barrier: compute writes -> graphics reads
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

VkBuffer TreeBranchCulling::getInstanceBuffer(uint32_t frameIndex) const {
    return outputBuffers_.getVk(frameIndex);
}

VkBuffer TreeBranchCulling::getIndirectBuffer(uint32_t frameIndex) const {
    return indirectBuffers_.getVk(frameIndex);
}
