#include "ImpostorCullSystem.h"
#include "TreeSystem.h"
#include "TreeImpostorAtlas.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "core/DescriptorManager.h"
#include "shaders/bindings.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstring>

std::unique_ptr<ImpostorCullSystem> ImpostorCullSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<ImpostorCullSystem>(new ImpostorCullSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

ImpostorCullSystem::~ImpostorCullSystem() {
    cleanup();
}

bool ImpostorCullSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    extent_ = info.extent;
    maxFramesInFlight_ = info.maxFramesInFlight;
    maxTrees_ = info.maxTrees;
    maxArchetypes_ = info.maxArchetypes;

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem: Failed to create descriptor set layout");
        return false;
    }

    if (!createComputePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem: Failed to create compute pipeline");
        return false;
    }

    if (!allocateDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem: Failed to allocate descriptor sets");
        return false;
    }

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem: Failed to create buffers");
        return false;
    }

    SDL_Log("ImpostorCullSystem: Initialized with max %u trees, %u archetypes", maxTrees_, maxArchetypes_);
    return true;
}

void ImpostorCullSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, visibleImpostorBuffers_);
    BufferUtils::destroyBuffers(allocator_, indirectDrawBuffers_);
    // treeInputBuffer_, archetypeBuffer_, visibilityCacheBuffer_ are ManagedBuffer (RAII - auto-cleanup)

    device_ = VK_NULL_HANDLE;
}

bool ImpostorCullSystem::createDescriptorSetLayout() {
    return DescriptorManager::LayoutBuilder(device_)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_UNIFORMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_ARCHETYPE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(BINDING_TREE_IMPOSTOR_CULL_VISIBILITY, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_SHADER_STAGE_COMPUTE_BIT)
        .buildManaged(cullDescriptorSetLayout_);
}

bool ImpostorCullSystem::createComputePipeline() {
    // Create pipeline layout (no push constants)
    if (!DescriptorManager::createManagedPipelineLayout(
            device_, cullDescriptorSetLayout_.get(), cullPipelineLayout_)) {
        return false;
    }

    // Load compute shader
    std::string shaderPath = resourcePath_ + "/shaders/tree_impostor_cull.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem: Failed to load shader %s", shaderPath.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = cullPipelineLayout_.get();

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device_, *shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        return false;
    }
    cullPipeline_ = ManagedPipeline(makeUniquePipeline(device_, pipeline));

    return true;
}

bool ImpostorCullSystem::allocateDescriptorSets() {
    cullDescriptorSets_ = descriptorPool_->allocate(cullDescriptorSetLayout_.get(), maxFramesInFlight_);
    return !cullDescriptorSets_.empty();
}

bool ImpostorCullSystem::createBuffers() {
    // Tree input buffer (CPU-writable for uploading tree data) - RAII ManagedBuffer
    treeInputBufferSize_ = maxTrees_ * sizeof(TreeCullInputData);
    VkBufferCreateInfo treeInputInfo{};
    treeInputInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    treeInputInfo.size = treeInputBufferSize_;
    treeInputInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo cpuToGpuAlloc{};
    cpuToGpuAlloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    cpuToGpuAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (!ManagedBuffer::create(allocator_, treeInputInfo, cpuToGpuAlloc, treeInputBuffer_)) {
        return false;
    }

    // Archetype buffer (CPU-writable for uploading archetype data) - RAII ManagedBuffer
    archetypeBufferSize_ = maxArchetypes_ * sizeof(ArchetypeCullData);
    VkBufferCreateInfo archetypeInfo{};
    archetypeInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    archetypeInfo.size = archetypeBufferSize_;
    archetypeInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!ManagedBuffer::create(allocator_, archetypeInfo, cpuToGpuAlloc, archetypeBuffer_)) {
        return false;
    }

    // Visible impostor output buffers (per-frame)
    visibleImpostorBufferSize_ = maxTrees_ * sizeof(ImpostorOutputData);
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(visibleImpostorBufferSize_)
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
            .build(visibleImpostorBuffers_)) {
        return false;
    }

    // Indirect draw command buffers (per-frame)
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(VkDrawIndexedIndirectCommand))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
            .build(indirectDrawBuffers_)) {
        return false;
    }

    // Uniform buffers
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator_)
            .setFrameCount(maxFramesInFlight_)
            .setSize(sizeof(ImpostorCullUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(uniformBuffers_)) {
        return false;
    }

    // Visibility cache buffer for temporal coherence (GPU-only) - RAII ManagedBuffer
    // 1 bit per tree, packed into uint32_t words
    visibilityCacheBufferSize_ = ((maxTrees_ + 31) / 32) * sizeof(uint32_t);
    VkBufferCreateInfo visibilityInfo{};
    visibilityInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    visibilityInfo.size = visibilityCacheBufferSize_;
    visibilityInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo gpuOnlyAlloc{};
    gpuOnlyAlloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedBuffer::create(allocator_, visibilityInfo, gpuOnlyAlloc, visibilityCacheBuffer_)) {
        return false;
    }

    return true;
}

void ImpostorCullSystem::updateTreeData(const TreeSystem& treeSystem, const TreeImpostorAtlas* atlas) {
    const auto& trees = treeSystem.getTreeInstances();
    treeCount_ = static_cast<uint32_t>(trees.size());

    if (treeCount_ == 0 || treeCount_ > maxTrees_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "ImpostorCullSystem: Tree count %u exceeds max %u or is 0", treeCount_, maxTrees_);
        return;
    }

    // Prepare tree input data
    std::vector<TreeCullInputData> inputData(treeCount_);
    uint32_t numArchetypes = atlas ? static_cast<uint32_t>(atlas->getArchetypeCount()) : 0;

    for (size_t i = 0; i < trees.size(); i++) {
        const auto& tree = trees[i];
        inputData[i].positionAndScale = glm::vec4(tree.position, tree.scale);

        // Assign archetype index based on tree type
        uint32_t archetypeIndex = 0;
        if (numArchetypes > 0) {
            archetypeIndex = static_cast<uint32_t>(i % numArchetypes);
        }

        inputData[i].rotationAndArchetype = glm::vec4(
            tree.rotation,
            glm::uintBitsToFloat(archetypeIndex),
            0.0f, 0.0f
        );
    }

    // Upload to GPU
    void* data = treeInputBuffer_.map();
    if (data) {
        memcpy(data, inputData.data(), inputData.size() * sizeof(TreeCullInputData));
        treeInputBuffer_.unmap();
    }
}

void ImpostorCullSystem::updateArchetypeData(const TreeImpostorAtlas* atlas) {
    if (!atlas) return;

    archetypeCount_ = static_cast<uint32_t>(atlas->getArchetypeCount());
    if (archetypeCount_ == 0 || archetypeCount_ > maxArchetypes_) return;

    std::vector<ArchetypeCullData> archetypeData(archetypeCount_);

    for (uint32_t i = 0; i < archetypeCount_; i++) {
        const auto* archetype = atlas->getArchetype(i);
        if (archetype) {
            // Match the octahedral capture projection sizing (15% margin)
            // projSize = max(boundingSphereRadius * 1.15, halfHeight * 1.15)
            float maxHSize = archetype->boundingSphereRadius * 1.15f;
            float maxVSize = archetype->treeHeight * 0.5f * 1.15f;
            float projSize = glm::max(maxHSize, maxVSize);
            archetypeData[i].sizingData = glm::vec4(
                projSize,                                  // hSize (matches capture)
                projSize,                                  // vSize (matches capture)
                archetype->centerHeight,                   // baseOffset (center of tree)
                archetype->boundingSphereRadius            // bounding radius for culling
            );
            // LOD error data for screen-space error calculation
            // worldErrorFull: smallest visible detail at full geometry (e.g., thin branch ~0.1m)
            // worldErrorImpostor: canopy-level detail for impostor (10% of canopy radius)
            float worldErrorFull = 0.1f;  // ~10cm branch thickness
            float worldErrorImpostor = archetype->boundingSphereRadius * 0.1f;  // 10% of canopy
            archetypeData[i].lodErrorData = glm::vec4(worldErrorFull, worldErrorImpostor, 0.0f, 0.0f);
        } else {
            // Default values
            archetypeData[i].sizingData = glm::vec4(10.0f, 10.0f, 0.0f, 10.0f);
            archetypeData[i].lodErrorData = glm::vec4(0.1f, 1.0f, 0.0f, 0.0f);
        }
    }

    // Upload to GPU
    void* data = archetypeBuffer_.map();
    if (data) {
        memcpy(data, archetypeData.data(), archetypeData.size() * sizeof(ArchetypeCullData));
        archetypeBuffer_.unmap();
    }
}

void ImpostorCullSystem::updateDescriptorSets(uint32_t frameIndex, VkImageView hiZPyramidView, VkSampler hiZSampler) {
    DescriptorManager::SetWriter(device_, cullDescriptorSets_[frameIndex])
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_INPUT, treeInputBuffer_.get(),
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_OUTPUT, visibleImpostorBuffers_.buffers[frameIndex],
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_INDIRECT, indirectDrawBuffers_.buffers[frameIndex],
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_UNIFORMS, uniformBuffers_.buffers[frameIndex],
                     0, sizeof(ImpostorCullUniforms))
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_ARCHETYPE, archetypeBuffer_.get(),
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeImage(BINDING_TREE_IMPOSTOR_CULL_HIZ, hiZPyramidView, hiZSampler)
        .writeBuffer(BINDING_TREE_IMPOSTOR_CULL_VISIBILITY, visibilityCacheBuffer_.get(),
                     0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .update();
}

void ImpostorCullSystem::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                        const glm::vec3& cameraPos,
                                        const glm::vec4* frustumPlanes,
                                        const glm::mat4& viewProjMatrix,
                                        VkImageView hiZPyramidView,
                                        VkSampler hiZSampler,
                                        const TreeLODSettings& lodSettings,
                                        float tanHalfFOV) {
    if (treeCount_ == 0) return;

    // Update uniforms (always full update - temporal coherence removed as it caused flickering)
    ImpostorCullUniforms uniforms{};
    uniforms.cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; i++) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.viewProjMatrix = viewProjMatrix;
    uniforms.screenParams = glm::vec4(
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        1.0f / static_cast<float>(extent_.width),
        1.0f / static_cast<float>(extent_.height)
    );
    uniforms.fullDetailDistance = lodSettings.fullDetailDistance;
    uniforms.impostorDistance = lodSettings.impostorDistance;
    uniforms.hysteresis = lodSettings.hysteresis;
    uniforms.blendRange = lodSettings.blendRange;
    uniforms.numTrees = treeCount_;
    uniforms.enableHiZ = (hiZEnabled_ && hiZPyramidView != VK_NULL_HANDLE) ? 1u : 0u;
    // Screen-space error LOD parameters (from single source of truth: TreeLODSettings)
    uniforms.useScreenSpaceError = lodSettings.useScreenSpaceError ? 1u : 0u;
    uniforms.tanHalfFOV = tanHalfFOV;
    uniforms.errorThresholdFull = lodSettings.errorThresholdFull;
    uniforms.errorThresholdImpostor = lodSettings.errorThresholdImpostor;
    uniforms.errorThresholdCull = lodSettings.errorThresholdCull;
    // Always full update (mode 0) - temporal coherence disabled
    uniforms.temporalUpdateMode = 0;
    uniforms.temporalUpdateOffset = 0;
    uniforms.temporalUpdateCount = 0;

    // Upload uniforms
    void* data;
    vmaMapMemory(allocator_, uniformBuffers_.allocations[frameIndex], &data);
    memcpy(data, &uniforms, sizeof(ImpostorCullUniforms));
    vmaUnmapMemory(allocator_, uniformBuffers_.allocations[frameIndex]);

    // Update descriptor sets if Hi-Z view changed
    if (hiZPyramidView != lastHiZView_ || hiZPyramidView != VK_NULL_HANDLE) {
        updateDescriptorSets(frameIndex, hiZPyramidView, hiZSampler);
        lastHiZView_ = hiZPyramidView;
    }

    // Reset indirect draw count by filling the buffer with zeros
    // instanceCount starts at 0 and is incremented atomically by the shader
    VkDrawIndexedIndirectCommand resetCmd{};
    resetCmd.indexCount = 0;
    resetCmd.instanceCount = 0;
    resetCmd.firstIndex = 0;
    resetCmd.vertexOffset = 0;
    resetCmd.firstInstance = 0;
    vkCmdFillBuffer(cmd, indirectDrawBuffers_.buffers[frameIndex], 0, sizeof(VkDrawIndexedIndirectCommand), 0);

    // Memory barrier to ensure fill is complete before compute
    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_.get(),
                           0, 1, &cullDescriptorSets_[frameIndex], 0, nullptr);

    // Dispatch compute shader
    // Each workgroup processes 256 trees
    uint32_t workgroupCount = (treeCount_ + 255) / 256;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier for compute output -> indirect draw
    VkMemoryBarrier computeBarrier{};
    computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

void ImpostorCullSystem::setExtent(VkExtent2D newExtent) {
    extent_ = newExtent;
}
