#include "SkinnedMeshRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "AnimatedCharacter.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "debug/QueueSubmitDiagnostics.h"

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>

std::unique_ptr<SkinnedMeshRenderer> SkinnedMeshRenderer::create(const InitInfo& info) {
    auto system = std::make_unique<SkinnedMeshRenderer>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

SkinnedMeshRenderer::~SkinnedMeshRenderer() {
    cleanup();
}

bool SkinnedMeshRenderer::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    renderPass = info.renderPass;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    addCommonBindings = info.addCommonBindings;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMeshRenderer requires raiiDevice");
        return false;
    }

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createBoneMatricesBuffers()) return false;

    SDL_Log("SkinnedMeshRenderer initialized");
    return true;
}

void SkinnedMeshRenderer::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    // RAII wrappers handle cleanup automatically
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();

    BufferUtils::destroyBuffers(allocator, boneMatricesBuffers);

    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkinnedMeshRenderer::createDescriptorSetLayout() {
    // Skinned descriptor set layout:
    // Same as main layout but with additional binding 12 for bone matrices UBO (dynamic for multi-character)
    DescriptorManager::LayoutBuilder builder(device);
    addCommonBindings(builder);
    // Add skinned-specific binding as dynamic UBO for multi-character support
    builder.addBinding(Bindings::BONE_MATRICES, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    return true;
}

bool SkinnedMeshRenderer::createPipeline() {
    // Create pipeline layout using PipelineLayoutBuilder
    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**descriptorSetLayout_)
        .addPushConstantRange<PushConstants>(
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(layoutOpt);

    // Use factory for pipeline creation with SkinnedVertex input
    auto bindingDescription = SkinnedVertex::getBindingDescription();
    auto attributeDescriptions = SkinnedVertex::getAttributeDescriptions();

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    GraphicsPipelineFactory factory(device);
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(shaderPath + "/skinned.vert.spv",
                    shaderPath + "/shader.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(renderPass)
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(extent)
        .setDynamicViewport(true)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned graphics pipeline");
        return false;
    }

    pipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("Created skinned graphics pipeline for GPU skinning");
    return true;
}

bool SkinnedMeshRenderer::createBoneMatricesBuffers() {
    // Query device properties for minimum uniform buffer offset alignment
    VkPhysicalDeviceProperties props;
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    vmaGetAllocatorInfo(allocator, nullptr);

    // Get physical device from VMA allocator info
    VmaAllocatorInfo allocatorInfo;
    vmaGetAllocatorInfo(allocator, &allocatorInfo);
    physDevice = allocatorInfo.physicalDevice;

    vkGetPhysicalDeviceProperties(physDevice, &props);
    boneBufferAlignment_ = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);

    // Calculate aligned size for each character's bone matrices
    uint32_t uboSize = sizeof(BoneMatricesUBO);
    alignedBoneBufferSize_ = (uboSize + boneBufferAlignment_ - 1) & ~(boneBufferAlignment_ - 1);

    // Total buffer size for all characters
    VkDeviceSize totalSize = static_cast<VkDeviceSize>(alignedBoneBufferSize_) * MAX_SKINNED_CHARACTERS;

    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(totalSize)
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_AUTO)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build(boneMatricesBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bone matrices buffers");
        return false;
    }

    // Initialize all character slots with identity matrices
    for (size_t frameIdx = 0; frameIdx < framesInFlight; frameIdx++) {
        uint8_t* basePtr = static_cast<uint8_t*>(boneMatricesBuffers.mappedPointers[frameIdx]);
        for (uint32_t slot = 0; slot < MAX_SKINNED_CHARACTERS; slot++) {
            BoneMatricesUBO* ubo = reinterpret_cast<BoneMatricesUBO*>(basePtr + slot * alignedBoneBufferSize_);
            for (uint32_t j = 0; j < MAX_BONES; j++) {
                ubo->bones[j] = glm::mat4(1.0f);
            }
        }
    }

    SDL_Log("Created bone matrices buffers for GPU skinning (%u characters, %u bytes each, alignment %u)",
            MAX_SKINNED_CHARACTERS, alignedBoneBufferSize_, boneBufferAlignment_);
    return true;
}

bool SkinnedMeshRenderer::createDescriptorSets(const DescriptorResources& resources) {
    // Allocate descriptor sets
    descriptorSets = descriptorPool->allocate(**descriptorSetLayout_, framesInFlight);
    if (descriptorSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate skinned descriptor sets");
        return false;
    }

    const auto& gbm = *resources.globalBufferManager;

    // Use factory to write skinned descriptor sets
    MaterialDescriptorFactory factory(device);

    for (size_t i = 0; i < framesInFlight; i++) {
        MaterialDescriptorFactory::CommonBindings common{};
        common.uniformBuffer = gbm.uniformBuffers.buffers[i];
        common.uniformBufferSize = sizeof(UniformBufferObject);
        common.shadowMapView = resources.shadowMapView;
        common.shadowMapSampler = resources.shadowMapSampler;
        common.lightBuffer = gbm.lightBuffers.buffers[i];
        common.lightBufferSize = sizeof(LightBuffer);
        common.emissiveMapView = resources.emissiveMapView;
        common.emissiveMapSampler = resources.emissiveMapSampler;
        common.pointShadowView = (*resources.pointShadowViews)[i];
        common.pointShadowSampler = resources.pointShadowSampler;
        common.spotShadowView = (*resources.spotShadowViews)[i];
        common.spotShadowSampler = resources.spotShadowSampler;
        common.snowMaskView = resources.snowMaskView;
        common.snowMaskSampler = resources.snowMaskSampler;
        // Cloud shadow not yet initialized - use white texture as fallback
        common.cloudShadowView = resources.whiteTextureView;
        common.cloudShadowSampler = resources.whiteTextureSampler;
        // Snow and cloud shadow UBOs (from GlobalBufferManager)
        common.snowUboBuffer = gbm.snowBuffers.buffers[i];
        common.snowUboBufferSize = sizeof(SnowUBO);
        common.cloudShadowUboBuffer = gbm.cloudShadowBuffers.buffers[i];
        common.cloudShadowUboBufferSize = sizeof(CloudShadowUBO);
        // Bone matrices - size is per-character slice (for dynamic offset range check)
        common.boneMatricesBuffer = boneMatricesBuffers.buffers[i];
        common.boneMatricesBufferSize = sizeof(BoneMatricesUBO);
        common.boneMatricesDynamic = true;  // Signal dynamic UBO usage
        // Placeholder texture for unused PBR bindings
        common.placeholderTextureView = resources.whiteTextureView;
        common.placeholderTextureSampler = resources.whiteTextureSampler;

        // Use player's actual material textures from MaterialRegistry
        // (avoids race condition where player could have different material based on FBX load success)
        MaterialDescriptorFactory::MaterialTextures mat{};
        mat.diffuseView = resources.playerDiffuseView;
        mat.diffuseSampler = resources.playerDiffuseSampler;
        mat.normalView = resources.playerNormalView;
        mat.normalSampler = resources.playerNormalSampler;
        factory.writeSkinnedDescriptorSet(descriptorSets[i], common, mat);
    }

    SDL_Log("Created skinned descriptor sets for GPU skinning (dynamic UBO for multi-character)");
    return true;
}

void SkinnedMeshRenderer::updateCloudShadowBinding(VkImageView cloudShadowView, VkSampler cloudShadowSampler) {
    MaterialDescriptorFactory factory(device);
    for (size_t i = 0; i < framesInFlight; i++) {
        factory.updateCloudShadowBinding(descriptorSets[i], cloudShadowView, cloudShadowSampler);
    }
}

void SkinnedMeshRenderer::updateBoneMatrices(uint32_t frameIndex, AnimatedCharacter* character) {
    BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(boneMatricesBuffers.mappedPointers[frameIndex]);

    if (!character) {
        // Ensure identity matrices when no character to prevent garbage data
        for (uint32_t i = 0; i < MAX_BONES; i++) {
            ubo->bones[i] = glm::mat4(1.0f);
        }
        return;
    }

    // Get bone matrices from animated character
    std::vector<glm::mat4> boneMatrices;
    character->computeBoneMatrices(boneMatrices);

    // Copy to mapped buffer
    size_t numBones = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < numBones; i++) {
        ubo->bones[i] = boneMatrices[i];
    }
    // Fill remaining slots with identity to prevent garbage data in unused bones
    for (size_t i = numBones; i < MAX_BONES; i++) {
        ubo->bones[i] = glm::mat4(1.0f);
    }
}

void SkinnedMeshRenderer::record(VkCommandBuffer cmd, uint32_t frameIndex,
                                  const Renderable& playerObj, AnimatedCharacter& character) {
    vk::CommandBuffer vkCmd(cmd);

    // Bind skinned pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);

    // Set dynamic viewport and scissor to handle window resize
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({extent.width, extent.height});
    vkCmd.setScissor(0, scissor);

    // Bind skinned descriptor set with dynamic offset for slot 0 (player)
    uint32_t dynamicOffset = 0;  // Slot 0 for player
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets[frameIndex]),
                             dynamicOffset);

    // Push constants with default tint (no tint = white)
    PushConstants push{};
    push.model = playerObj.transform;
    push.roughness = playerObj.roughness;
    push.metallic = playerObj.metallic;
    push.emissiveIntensity = playerObj.emissiveIntensity;
    push.opacity = playerObj.opacity;
    push.emissiveColor = glm::vec4(playerObj.emissiveColor, 1.0f);
    push.pbrFlags = playerObj.pbrFlags;
    push.tintColor = glm::vec4(1.0f);  // No tint for player

    vkCmd.pushConstants<PushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Bind skinned mesh vertex and index buffers
    SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

    VkBuffer vertexBuffers[] = {skinnedMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(skinnedMesh.getIndexBuffer(), 0, vk::IndexType::eUint32);

    vkCmd.drawIndexed(skinnedMesh.getIndexCount(), 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}

int SkinnedMeshRenderer::updateBoneMatricesForSlot(uint32_t frameIndex, uint32_t slot,
                                                    const std::vector<glm::mat4>& boneMatrices) {
    if (slot >= MAX_SKINNED_CHARACTERS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid bone matrices slot %u (max %u)", slot, MAX_SKINNED_CHARACTERS);
        return -1;
    }

    uint8_t* basePtr = static_cast<uint8_t*>(boneMatricesBuffers.mappedPointers[frameIndex]);
    BoneMatricesUBO* ubo = reinterpret_cast<BoneMatricesUBO*>(basePtr + slot * alignedBoneBufferSize_);

    // Copy bone matrices
    size_t numBones = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < numBones; i++) {
        ubo->bones[i] = boneMatrices[i];
    }
    // Fill remaining slots with identity
    for (size_t i = numBones; i < MAX_BONES; i++) {
        ubo->bones[i] = glm::mat4(1.0f);
    }

    return static_cast<int>(slot);
}

void SkinnedMeshRenderer::recordNPC(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slot,
                                     const glm::mat4& transform, const glm::vec4& tintColor,
                                     AnimatedCharacter& character) {
    if (slot >= MAX_SKINNED_CHARACTERS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid NPC slot %u", slot);
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Bind skinned pipeline (may already be bound, but safe to call)
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);

    // Set dynamic viewport and scissor
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({extent.width, extent.height});
    vkCmd.setScissor(0, scissor);

    // Bind skinned descriptor set with dynamic offset for this NPC's slot
    uint32_t dynamicOffset = slot * alignedBoneBufferSize_;
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets[frameIndex]),
                             dynamicOffset);

    // Push constants with NPC tint color
    PushConstants push{};
    push.model = transform;
    push.roughness = 0.7f;  // Default NPC material properties
    push.metallic = 0.0f;
    push.emissiveIntensity = 0.0f;
    push.opacity = 1.0f;
    push.emissiveColor = glm::vec4(1.0f);
    push.pbrFlags = 0;
    push.tintColor = tintColor;  // Apply hostility tint color

    vkCmd.pushConstants<PushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Bind skinned mesh vertex and index buffers (same mesh as player)
    SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

    VkBuffer vertexBuffers[] = {skinnedMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(skinnedMesh.getIndexBuffer(), 0, vk::IndexType::eUint32);

    vkCmd.drawIndexed(skinnedMesh.getIndexCount(), 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}

void SkinnedMeshRenderer::resetCharacterSlots(uint32_t frameIndex) {
    // No-op for now - slots are reused each frame
    // Could be used for slot tracking if needed
}
