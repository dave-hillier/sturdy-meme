#include "SkinnedMeshRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "AnimatedCharacter.h"
#include "CharacterLOD.h"
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
    physicalDevice = info.physicalDevice;
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

    if (!physicalDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMeshRenderer requires physicalDevice for dynamic UBO alignment");
        return false;
    }

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createBoneMatricesBuffers()) return false;

    SDL_Log("SkinnedMeshRenderer initialized with %u character slots", MAX_SKINNED_CHARACTERS);
    return true;
}

void SkinnedMeshRenderer::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    // RAII wrappers handle cleanup automatically
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();

    BufferUtils::destroyBuffer(allocator, boneMatricesBuffer_);

    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkinnedMeshRenderer::createDescriptorSetLayout() {
    // Skinned descriptor set layout:
    // Same as main layout but with binding 12 as DYNAMIC uniform buffer for bone matrices
    // This allows per-draw dynamic offset to select different character's bone data
    DescriptorManager::LayoutBuilder builder(device);
    addCommonBindings(builder);
    // Add skinned-specific binding as DYNAMIC UBO - enables per-draw offset selection
    builder.addBinding(Bindings::BONE_MATRICES, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT);

    VkDescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    SDL_Log("Created skinned descriptor layout with dynamic UBO for per-character bone matrices");
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
    // Create multi-slot dynamic buffer: MAX_SKINNED_CHARACTERS slots per frame
    // Each slot holds one character's bone matrices, selected via dynamic offset at draw time
    if (!BufferUtils::MultiSlotDynamicBufferBuilder()
            .setAllocator(allocator)
            .setPhysicalDevice(physicalDevice)
            .setFrameCount(framesInFlight)
            .setSlotsPerFrame(MAX_SKINNED_CHARACTERS)
            .setElementSize(sizeof(BoneMatricesUBO))
            .build(boneMatricesBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bone matrices dynamic buffer");
        return false;
    }

    // Initialize all slots with identity matrices
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        for (uint32_t slot = 0; slot < MAX_SKINNED_CHARACTERS; slot++) {
            BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(
                boneMatricesBuffer_.getMappedPtr(frame, slot));
            for (uint32_t j = 0; j < MAX_BONES; j++) {
                ubo->bones[j] = glm::mat4(1.0f);
            }
        }
    }

    SDL_Log("Created bone matrices dynamic buffer: %u slots x %u frames for GPU skinning",
            MAX_SKINNED_CHARACTERS, framesInFlight);
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
        // Bone matrices - use full buffer, dynamic offset selects per-character slot
        // The descriptor points to the entire buffer; actual slot is selected at bind time
        common.boneMatricesBuffer = boneMatricesBuffer_.buffer;
        common.boneMatricesBufferSize = boneMatricesBuffer_.getAlignedSlotSize();
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

    SDL_Log("Created skinned descriptor sets with dynamic bone matrices for GPU skinning");
    return true;
}

void SkinnedMeshRenderer::updateCloudShadowBinding(VkImageView cloudShadowView, VkSampler cloudShadowSampler) {
    MaterialDescriptorFactory factory(device);
    for (size_t i = 0; i < framesInFlight; i++) {
        factory.updateCloudShadowBinding(descriptorSets[i], cloudShadowView, cloudShadowSampler);
    }
}

void SkinnedMeshRenderer::updateBoneMatrices(uint32_t frameIndex, uint32_t slotIndex, AnimatedCharacter* character) {
    if (slotIndex >= MAX_SKINNED_CHARACTERS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Bone matrix slot %u exceeds max %u", slotIndex, MAX_SKINNED_CHARACTERS);
        return;
    }

    BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(
        boneMatricesBuffer_.getMappedPtr(frameIndex, slotIndex));

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

    // Copy to mapped buffer slot
    size_t numBones = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
    for (size_t i = 0; i < numBones; i++) {
        ubo->bones[i] = boneMatrices[i];
    }
    // Fill remaining slots with identity to prevent garbage data in unused bones
    for (size_t i = numBones; i < MAX_BONES; i++) {
        ubo->bones[i] = glm::mat4(1.0f);
    }
}

void SkinnedMeshRenderer::record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
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

    // Calculate dynamic offset to select this character's bone matrix slot
    // This is the key fix: each character gets its own slot in the buffer,
    // selected at draw time via dynamic offset instead of updating descriptors
    uint32_t dynamicOffset = boneMatricesBuffer_.getDynamicOffset(frameIndex, slotIndex);

    // Bind skinned descriptor set with dynamic offset for bone matrices
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets[frameIndex]),
                             dynamicOffset);

    // Push constants
    PushConstants push{};
    push.model = playerObj.transform;
    push.roughness = playerObj.roughness;
    push.metallic = playerObj.metallic;
    push.emissiveIntensity = playerObj.emissiveIntensity;
    push.opacity = playerObj.opacity;
    push.emissiveColor = glm::vec4(playerObj.emissiveColor, 1.0f);
    push.pbrFlags = playerObj.pbrFlags;

    vkCmd.pushConstants<PushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Bind skinned mesh vertex and index buffers
    SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

    vk::Buffer vertexBuffers[] = {skinnedMesh.getVertexBuffer()};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(skinnedMesh.getIndexBuffer(), 0, vk::IndexType::eUint32);

    vkCmd.drawIndexed(skinnedMesh.getIndexCount(), 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}

void SkinnedMeshRenderer::recordWithLOD(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                                         const Renderable& playerObj, AnimatedCharacter& character,
                                         const CharacterLODMesh& lodMesh) {
    // Validate LOD mesh
    if (!lodMesh.isValid()) {
        // Fallback to normal record
        record(cmd, frameIndex, slotIndex, playerObj, character);
        return;
    }

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

    // Calculate dynamic offset to select this character's bone matrix slot
    uint32_t dynamicOffset = boneMatricesBuffer_.getDynamicOffset(frameIndex, slotIndex);

    // Bind skinned descriptor set with dynamic offset for bone matrices
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets[frameIndex]),
                             dynamicOffset);

    // Push constants
    PushConstants push{};
    push.model = playerObj.transform;
    push.roughness = playerObj.roughness;
    push.metallic = playerObj.metallic;
    push.emissiveIntensity = playerObj.emissiveIntensity;
    push.opacity = playerObj.opacity;
    push.emissiveColor = glm::vec4(playerObj.emissiveColor, 1.0f);
    push.pbrFlags = playerObj.pbrFlags;

    vkCmd.pushConstants<PushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Bind LOD mesh vertex and index buffers
    vk::Buffer vertexBuffers[] = {lodMesh.vertexBuffer};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(lodMesh.indexBuffer, 0, vk::IndexType::eUint32);

    vkCmd.drawIndexed(lodMesh.indexCount, 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}
