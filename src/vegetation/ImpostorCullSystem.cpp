#include "ImpostorCullSystem.h"
#include "TreeSystem.h"
#include "TreeImpostorAtlas.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
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

    if (treeInputBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, treeInputBuffer_, treeInputAllocation_);
        treeInputBuffer_ = VK_NULL_HANDLE;
    }
    if (archetypeBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, archetypeBuffer_, archetypeAllocation_);
        archetypeBuffer_ = VK_NULL_HANDLE;
    }
    if (visibleImpostorBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, visibleImpostorBuffer_, visibleImpostorAllocation_);
        visibleImpostorBuffer_ = VK_NULL_HANDLE;
    }
    if (indirectDrawBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, indirectDrawBuffer_, indirectDrawAllocation_);
        indirectDrawBuffer_ = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (visibilityCacheBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, visibilityCacheBuffers_[i], visibilityCacheAllocations_[i]);
            visibilityCacheBuffers_[i] = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
}

bool ImpostorCullSystem::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

    // Binding 0: Tree input data (SSBO)
    bindings[0].binding = BINDING_TREE_IMPOSTOR_CULL_INPUT;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Visible impostor output (SSBO)
    bindings[1].binding = BINDING_TREE_IMPOSTOR_CULL_OUTPUT;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Indirect draw command (SSBO)
    bindings[2].binding = BINDING_TREE_IMPOSTOR_CULL_INDIRECT;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Culling uniforms (UBO)
    bindings[3].binding = BINDING_TREE_IMPOSTOR_CULL_UNIFORMS;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Archetype data (SSBO)
    bindings[4].binding = BINDING_TREE_IMPOSTOR_CULL_ARCHETYPE;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: Hi-Z pyramid (sampler2D)
    bindings[5].binding = BINDING_TREE_IMPOSTOR_CULL_HIZ;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: Visibility cache (SSBO) for temporal coherence
    bindings[6].binding = BINDING_TREE_IMPOSTOR_CULL_VISIBILITY;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return false;
    }
    cullDescriptorSetLayout_ = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device_, layout));

    return true;
}

bool ImpostorCullSystem::createComputePipeline() {
    // Create pipeline layout
    VkDescriptorSetLayout layouts[] = {cullDescriptorSetLayout_.get()};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    cullPipelineLayout_ = ManagedPipelineLayout(makeUniquePipelineLayout(device_, pipelineLayout));

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
    pipelineInfo.layout = pipelineLayout;

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
    // Tree input buffer
    treeInputBufferSize_ = maxTrees_ * sizeof(TreeCullInputData);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = treeInputBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &treeInputBuffer_, &treeInputAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Archetype buffer
    archetypeBufferSize_ = maxArchetypes_ * sizeof(ArchetypeCullData);
    bufferInfo.size = archetypeBufferSize_;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &archetypeBuffer_, &archetypeAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Visible impostor output buffer
    visibleImpostorBufferSize_ = maxTrees_ * sizeof(ImpostorOutputData);
    bufferInfo.size = visibleImpostorBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &visibleImpostorBuffer_, &visibleImpostorAllocation_, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Indirect draw command buffer
    bufferInfo.size = sizeof(VkDrawIndexedIndirectCommand);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &indirectDrawBuffer_, &indirectDrawAllocation_, nullptr) != VK_SUCCESS) {
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

    // Visibility cache buffers for temporal coherence (per-frame to avoid cross-frame races)
    // 1 bit per tree, packed into uint32_t words
    visibilityCacheBufferSize_ = ((maxTrees_ + 31) / 32) * sizeof(uint32_t);
    bufferInfo.size = visibilityCacheBufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                            &visibilityCacheBuffers_[i], &visibilityCacheAllocations_[i], nullptr) != VK_SUCCESS) {
            return false;
        }
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

        // Use the tree's stored archetype index (set based on leaf type)
        uint32_t archetypeIndex = tree.archetypeIndex;
        if (numArchetypes > 0 && archetypeIndex >= numArchetypes) {
            archetypeIndex = archetypeIndex % numArchetypes;
        }

        inputData[i].rotationAndArchetype = glm::vec4(
            tree.rotation,
            glm::uintBitsToFloat(archetypeIndex),
            0.0f, 0.0f
        );

        // Compute per-tree sizing from actual mesh bounds (at scale=1)
        if (tree.meshIndex < treeSystem.getMeshCount()) {
            const auto& meshBounds = treeSystem.getBranchMesh(tree.meshIndex).getBounds();
            glm::vec3 minB = meshBounds.min;
            glm::vec3 maxB = meshBounds.max;
            glm::vec3 extent = maxB - minB;

            float horizontalRadius = std::max(extent.x, extent.z) * 0.5f;
            float halfHeight = extent.y * 0.5f;
            float boundingSphereRadius = glm::length(extent) * 0.5f;

            // Billboard sizing: hSize uses bounding sphere for horizontal coverage,
            // vSize uses half height to prevent ground penetration.
            // The billboard center is at centerHeight, and extends vSize up/down.
            // If vSize > centerHeight, the billboard bottom would go below ground.
            float hSize = boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            float vSize = halfHeight * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            float centerHeight = (minB.y + maxB.y) * 0.5f;

            inputData[i].sizeAndOffset = glm::vec4(hSize, vSize, centerHeight, 0.0f);
        } else if (atlas && archetypeIndex < atlas->getArchetypeCount()) {
            // Fallback to archetype bounds
            const auto* archetype = atlas->getArchetype(archetypeIndex);
            float hSize = archetype->boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            float vSize = archetype->treeHeight * 0.5f * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            inputData[i].sizeAndOffset = glm::vec4(hSize, vSize, archetype->centerHeight, 0.0f);
        } else {
            // Default fallback
            inputData[i].sizeAndOffset = glm::vec4(10.0f, 10.0f, 5.0f, 0.0f);
        }
    }

    // Upload to GPU
    void* data;
    vmaMapMemory(allocator_, treeInputAllocation_, &data);
    memcpy(data, inputData.data(), inputData.size() * sizeof(TreeCullInputData));
    vmaUnmapMemory(allocator_, treeInputAllocation_);
}

void ImpostorCullSystem::updateArchetypeData(const TreeImpostorAtlas* atlas) {
    if (!atlas) return;

    archetypeCount_ = static_cast<uint32_t>(atlas->getArchetypeCount());
    if (archetypeCount_ == 0 || archetypeCount_ > maxArchetypes_) return;

    std::vector<ArchetypeCullData> archetypeData(archetypeCount_);

    for (uint32_t i = 0; i < archetypeCount_; i++) {
        const auto* archetype = atlas->getArchetype(i);
        if (archetype) {
            // Billboard sizing: hSize uses bounding sphere, vSize uses half height
            // to prevent ground penetration when billboard center is at centerHeight
            float hSize = archetype->boundingSphereRadius * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            float vSize = archetype->treeHeight * 0.5f * TreeLODConstants::IMPOSTOR_SIZE_MARGIN;
            archetypeData[i].sizingData = glm::vec4(
                hSize,                                     // hSize (horizontal radius)
                vSize,                                     // vSize (half height)
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
    void* data;
    vmaMapMemory(allocator_, archetypeAllocation_, &data);
    memcpy(data, archetypeData.data(), archetypeData.size() * sizeof(ArchetypeCullData));
    vmaUnmapMemory(allocator_, archetypeAllocation_);
}

void ImpostorCullSystem::initializeDescriptorSets() {
    // Initialize static descriptor bindings for all frames
    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight_; ++frameIndex) {
        std::array<VkWriteDescriptorSet, 6> writes{};

        // Tree input buffer (static)
        VkDescriptorBufferInfo inputInfo{};
        inputInfo.buffer = treeInputBuffer_;
        inputInfo.offset = 0;
        inputInfo.range = VK_WHOLE_SIZE;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cullDescriptorSets_[frameIndex];
        writes[0].dstBinding = BINDING_TREE_IMPOSTOR_CULL_INPUT;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &inputInfo;

        // Visible output buffer (static)
        VkDescriptorBufferInfo outputInfo{};
        outputInfo.buffer = visibleImpostorBuffer_;
        outputInfo.offset = 0;
        outputInfo.range = VK_WHOLE_SIZE;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cullDescriptorSets_[frameIndex];
        writes[1].dstBinding = BINDING_TREE_IMPOSTOR_CULL_OUTPUT;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &outputInfo;

        // Indirect draw buffer (static)
        VkDescriptorBufferInfo indirectInfo{};
        indirectInfo.buffer = indirectDrawBuffer_;
        indirectInfo.offset = 0;
        indirectInfo.range = VK_WHOLE_SIZE;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = cullDescriptorSets_[frameIndex];
        writes[2].dstBinding = BINDING_TREE_IMPOSTOR_CULL_INDIRECT;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &indirectInfo;

        // Uniform buffer (per-frame indexed, but static binding)
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = uniformBuffers_.buffers[frameIndex];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(ImpostorCullUniforms);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = cullDescriptorSets_[frameIndex];
        writes[3].dstBinding = BINDING_TREE_IMPOSTOR_CULL_UNIFORMS;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &uniformInfo;

        // Archetype buffer (static)
        VkDescriptorBufferInfo archetypeInfo{};
        archetypeInfo.buffer = archetypeBuffer_;
        archetypeInfo.offset = 0;
        archetypeInfo.range = VK_WHOLE_SIZE;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = cullDescriptorSets_[frameIndex];
        writes[4].dstBinding = BINDING_TREE_IMPOSTOR_CULL_ARCHETYPE;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &archetypeInfo;

        // Visibility cache buffer (per-frame indexed, but static binding)
        VkDescriptorBufferInfo visibilityInfo{};
        visibilityInfo.buffer = visibilityCacheBuffers_[frameIndex];
        visibilityInfo.offset = 0;
        visibilityInfo.range = VK_WHOLE_SIZE;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = cullDescriptorSets_[frameIndex];
        writes[5].dstBinding = BINDING_TREE_IMPOSTOR_CULL_VISIBILITY;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &visibilityInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Reset lastHiZView_ to force Hi-Z binding update on first use
    lastHiZView_ = VK_NULL_HANDLE;
}

void ImpostorCullSystem::updateHiZDescriptor(uint32_t frameIndex, VkImageView hiZPyramidView, VkSampler hiZSampler) {
    // Only update the Hi-Z pyramid descriptor (binding 5)
    VkDescriptorImageInfo hiZInfo{};
    hiZInfo.sampler = hiZSampler;
    hiZInfo.imageView = hiZPyramidView;
    hiZInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = cullDescriptorSets_[frameIndex];
    write.dstBinding = BINDING_TREE_IMPOSTOR_CULL_HIZ;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &hiZInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void ImpostorCullSystem::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                        const glm::vec3& cameraPos,
                                        const glm::vec4* frustumPlanes,
                                        const glm::mat4& viewProjMatrix,
                                        VkImageView hiZPyramidView,
                                        VkSampler hiZSampler,
                                        const LODParams& lodParams) {
    if (treeCount_ == 0) return;

    // Temporal coherence - determine update mode based on camera movement
    uint32_t temporalUpdateMode = 0;  // 0=full, 1=partial, 2=skip
    uint32_t temporalUpdateOffset = 0;
    uint32_t temporalUpdateCount = 0;

    if (temporalSettings_.enabled) {
        // Calculate camera position delta
        float posDelta = glm::length(cameraPos - lastCameraPos_);

        // Extract camera forward direction from view matrix (third column negated)
        glm::vec3 cameraDir = -glm::normalize(glm::vec3(viewProjMatrix[0][2], viewProjMatrix[1][2], viewProjMatrix[2][2]));
        float rotDelta = glm::degrees(glm::acos(glm::clamp(glm::dot(cameraDir, lastCameraDir_), -1.0f, 1.0f)));

        // Increment frame counter
        temporalSettings_.framesSinceFullUpdate++;

        // Determine update mode
        if (posDelta > temporalSettings_.positionThreshold ||
            rotDelta > temporalSettings_.rotationThreshold ||
            temporalSettings_.framesSinceFullUpdate >= temporalSettings_.maxFramesBetweenFullUpdates) {
            // Full update: camera moved significantly or periodic forced update
            temporalUpdateMode = 0;
            temporalSettings_.framesSinceFullUpdate = 0;
        } else if (posDelta < 0.1f && rotDelta < 0.5f) {
            // Skip update: camera nearly stationary
            temporalUpdateMode = 2;
        } else {
            // Partial update: camera moving slowly, update a subset of trees
            temporalUpdateMode = 1;
            temporalUpdateCount = static_cast<uint32_t>(
                static_cast<float>(treeCount_) * temporalSettings_.partialUpdateFraction);
            temporalUpdateCount = std::max(temporalUpdateCount, 1u);
            temporalUpdateOffset = partialUpdateOffset_;

            // Advance rolling offset for next frame
            partialUpdateOffset_ = (partialUpdateOffset_ + temporalUpdateCount) % treeCount_;
        }

        // Update camera tracking state
        lastCameraPos_ = cameraPos;
        lastCameraDir_ = cameraDir;
    }

    // For skip/partial modes, copy visibility cache from previous frame to current frame
    // This ensures temporal coherence works correctly with per-frame buffers
    if (temporalUpdateMode != 0) {
        uint32_t prevFrameIndex = (frameIndex + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = visibilityCacheBufferSize_;
        vkCmdCopyBuffer(cmd, visibilityCacheBuffers_[prevFrameIndex],
                        visibilityCacheBuffers_[frameIndex], 1, &copyRegion);

        // Barrier to ensure copy completes before compute shader reads/writes
        VkMemoryBarrier copyBarrier{};
        copyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &copyBarrier, 0, nullptr, 0, nullptr);
    }

    // Update uniforms
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
    uniforms.fullDetailDistance = lodParams.fullDetailDistance;
    uniforms.impostorDistance = lodParams.impostorDistance;
    uniforms.hysteresis = lodParams.hysteresis;
    uniforms.blendRange = lodParams.blendRange;
    uniforms.numTrees = treeCount_;
    uniforms.enableHiZ = (hiZEnabled_ && hiZPyramidView != VK_NULL_HANDLE) ? 1u : 0u;
    // Screen-space error LOD parameters
    uniforms.useScreenSpaceError = lodParams.useScreenSpaceError ? 1u : 0u;
    uniforms.tanHalfFOV = lodParams.tanHalfFOV;
    uniforms.errorThresholdFull = lodParams.errorThresholdFull;
    uniforms.errorThresholdImpostor = lodParams.errorThresholdImpostor;
    uniforms.errorThresholdCull = lodParams.errorThresholdCull;
    // Temporal coherence parameters
    uniforms.temporalUpdateMode = temporalUpdateMode;
    uniforms.temporalUpdateOffset = temporalUpdateOffset;
    uniforms.temporalUpdateCount = temporalUpdateCount;

    // Upload uniforms
    void* data;
    vmaMapMemory(allocator_, uniformBuffers_.allocations[frameIndex], &data);
    memcpy(data, &uniforms, sizeof(ImpostorCullUniforms));
    vmaUnmapMemory(allocator_, uniformBuffers_.allocations[frameIndex]);

    // Update Hi-Z descriptor only if Hi-Z view changed
    if (hiZPyramidView != lastHiZView_) {
        updateHiZDescriptor(frameIndex, hiZPyramidView, hiZSampler);
        lastHiZView_ = hiZPyramidView;
    }

    // Reset indirect draw command with proper values
    // instanceCount starts at 0 and is incremented atomically by the shader
    // We use vkCmdUpdateBuffer to set the full struct (small update, fits in command buffer)
    VkDrawIndexedIndirectCommand resetCmd{};
    resetCmd.indexCount = 6;        // Billboard quad: 6 indices
    resetCmd.instanceCount = 0;     // Will be incremented atomically by shader
    resetCmd.firstIndex = 0;
    resetCmd.vertexOffset = 0;
    resetCmd.firstInstance = 0;
    vkCmdUpdateBuffer(cmd, indirectDrawBuffer_, 0, sizeof(VkDrawIndexedIndirectCommand), &resetCmd);

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
