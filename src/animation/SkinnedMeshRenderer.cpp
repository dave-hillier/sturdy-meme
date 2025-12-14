#include "SkinnedMeshRenderer.h"
#include "GraphicsPipelineFactory.h"
#include "AnimatedCharacter.h"
#include "Bindings.h"
#include "UBOs.h"

#include <SDL3/SDL.h>

bool SkinnedMeshRenderer::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    renderPass = info.renderPass;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    addCommonBindings = info.addCommonBindings;

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createBoneMatricesBuffers()) return false;

    SDL_Log("SkinnedMeshRenderer initialized");
    return true;
}

void SkinnedMeshRenderer::destroy() {
    if (device == VK_NULL_HANDLE) return;

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < boneMatricesBuffers.size(); ++i) {
        if (boneMatricesBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, boneMatricesBuffers[i], boneMatricesAllocations[i]);
        }
    }
    boneMatricesBuffers.clear();
    boneMatricesAllocations.clear();
    boneMatricesMapped.clear();

    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkinnedMeshRenderer::createDescriptorSetLayout() {
    // Skinned descriptor set layout:
    // Same as main layout but with additional binding 12 for bone matrices UBO
    DescriptorManager::LayoutBuilder builder(device);
    addCommonBindings(builder);
    // Add skinned-specific binding
    builder.addBinding(Bindings::BONE_MATRICES, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    descriptorSetLayout = builder.build();

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned descriptor set layout");
        return false;
    }

    return true;
}

bool SkinnedMeshRenderer::createPipeline() {
    // Create pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned pipeline layout");
        return false;
    }

    // Use factory for pipeline creation with SkinnedVertex input
    auto bindingDescription = SkinnedVertex::getBindingDescription();
    auto attributeDescriptions = SkinnedVertex::getAttributeDescriptions();

    GraphicsPipelineFactory factory(device);
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(shaderPath + "/skinned.vert.spv",
                    shaderPath + "/shader.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(renderPass)
        .setPipelineLayout(pipelineLayout)
        .setExtent(extent)
        .setDynamicViewport(true)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(pipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create skinned graphics pipeline");
        return false;
    }

    SDL_Log("Created skinned graphics pipeline for GPU skinning");
    return true;
}

bool SkinnedMeshRenderer::createBoneMatricesBuffers() {
    VkDeviceSize bufferSize = sizeof(BoneMatricesUBO);

    boneMatricesBuffers.resize(framesInFlight);
    boneMatricesAllocations.resize(framesInFlight);
    boneMatricesMapped.resize(framesInFlight);

    for (size_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &boneMatricesBuffers[i],
                            &boneMatricesAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bone matrices buffer");
            return false;
        }

        boneMatricesMapped[i] = allocationInfo.pMappedData;

        // Initialize with identity matrices
        BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(boneMatricesMapped[i]);
        for (uint32_t j = 0; j < MAX_BONES; j++) {
            ubo->bones[j] = glm::mat4(1.0f);
        }
    }

    SDL_Log("Created bone matrices buffers for GPU skinning");
    return true;
}

bool SkinnedMeshRenderer::createDescriptorSets(const DescriptorResources& resources) {
    // Allocate descriptor sets
    descriptorSets = descriptorPool->allocate(descriptorSetLayout, framesInFlight);
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
        // Bone matrices
        common.boneMatricesBuffer = boneMatricesBuffers[i];
        common.boneMatricesBufferSize = sizeof(BoneMatricesUBO);
        // Placeholder texture for unused PBR bindings
        common.placeholderTextureView = resources.whiteTextureView;
        common.placeholderTextureSampler = resources.whiteTextureSampler;

        MaterialDescriptorFactory::MaterialTextures mat{};
        mat.diffuseView = resources.whiteTextureView;
        mat.diffuseSampler = resources.whiteTextureSampler;
        mat.normalView = resources.whiteTextureView;
        mat.normalSampler = resources.whiteTextureSampler;
        factory.writeSkinnedDescriptorSet(descriptorSets[i], common, mat);
    }

    SDL_Log("Created skinned descriptor sets for GPU skinning");
    return true;
}

void SkinnedMeshRenderer::updateCloudShadowBinding(VkImageView cloudShadowView, VkSampler cloudShadowSampler) {
    MaterialDescriptorFactory factory(device);
    for (size_t i = 0; i < framesInFlight; i++) {
        factory.updateCloudShadowBinding(descriptorSets[i], cloudShadowView, cloudShadowSampler);
    }
}

void SkinnedMeshRenderer::updateBoneMatrices(uint32_t frameIndex, AnimatedCharacter* character) {
    BoneMatricesUBO* ubo = static_cast<BoneMatricesUBO*>(boneMatricesMapped[frameIndex]);

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
    // Bind skinned pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Set dynamic viewport and scissor to handle window resize
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind skinned descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Push constants
    PushConstants push{};
    push.model = playerObj.transform;
    push.roughness = playerObj.roughness;
    push.metallic = playerObj.metallic;
    push.emissiveIntensity = playerObj.emissiveIntensity;
    push.opacity = playerObj.opacity;
    push.emissiveColor = glm::vec4(playerObj.emissiveColor, 1.0f);
    push.pbrFlags = playerObj.pbrFlags;

    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);

    // Bind skinned mesh vertex and index buffers
    SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

    VkBuffer vertexBuffers[] = {skinnedMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, skinnedMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, skinnedMesh.getIndexCount(), 1, 0, 0, 0);
}
