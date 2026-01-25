#include "ShadowSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "PipelineBuilder.h"
#include "GraphicsPipelineFactory.h"
#include "debug/QueueSubmitDiagnostics.h"
#include "shaders/bindings.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "core/vulkan/RenderPassBuilder.h"
#include "core/vulkan/DescriptorWriter.h"
#include "core/InitInfoBuilder.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>

// Factory implementations
std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitInfo& info) {
    auto system = std::make_unique<ShadowSystem>(ConstructToken{}, info);
    if (!system->initialized_) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitContext& ctx,
                                                    VkDescriptorSetLayout mainDescriptorSetLayout_,
                                                    VkDescriptorSetLayout skinnedDescriptorSetLayout_) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    info.mainDescriptorSetLayout = mainDescriptorSetLayout_;
    info.skinnedDescriptorSetLayout = skinnedDescriptorSetLayout_;
    return create(info);
}

ShadowSystem::ShadowSystem(ConstructToken, const InitInfo& info)
    : initInfo_(info) {
    if (initInfo_.device == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem requires a valid VkDevice");
        return;
    }

    if (!createShadowRenderPass()
        || !createShadowResources()
        || !createDynamicShadowResources()
        || !createInstancedShadowResources()
        || !createShadowPipeline()
        || !createSkinnedShadowPipeline()
        || !createDynamicShadowPipeline()
        || !createInstancedShadowPipeline()) {
        return;
    }

    initialized_ = true;
}

// Destructor
ShadowSystem::~ShadowSystem() {
    if (initInfo_.device == VK_NULL_HANDLE) {
        return;
    }

    vk::Device vkDevice(initInfo_.device);

    // Pipeline cleanup
    if (shadowPipeline != VK_NULL_HANDLE) vkDevice.destroyPipeline(shadowPipeline);
    if (shadowPipelineLayout != VK_NULL_HANDLE) vkDevice.destroyPipelineLayout(shadowPipelineLayout);
    if (skinnedShadowPipeline != VK_NULL_HANDLE) vkDevice.destroyPipeline(skinnedShadowPipeline);
    if (skinnedShadowPipelineLayout != VK_NULL_HANDLE) vkDevice.destroyPipelineLayout(skinnedShadowPipelineLayout);
    if (dynamicShadowPipeline != VK_NULL_HANDLE) vkDevice.destroyPipeline(dynamicShadowPipeline);
    if (dynamicShadowPipelineLayout != VK_NULL_HANDLE) vkDevice.destroyPipelineLayout(dynamicShadowPipelineLayout);

    // CSM cleanup
    for (auto fb : cascadeFramebuffers) {
        if (fb != VK_NULL_HANDLE) vkDevice.destroyFramebuffer(fb);
    }
    cascadeFramebuffers.clear();
    csmResources.reset();  // RAII cleanup

    // Dynamic shadow cleanup
    destroyDynamicShadowResources();

    // Instanced shadow cleanup
    destroyInstancedShadowResources();

    // Render pass - handled by RAII shadowRenderPass_ member
    shadowRenderPass_.reset();
    shadowRenderPass = VK_NULL_HANDLE;
}

bool ShadowSystem::createShadowRenderPass() {
    // Depth-only render pass for shadow mapping, outputs to shader read for sampling
    auto renderPassOpt = RenderPassBuilder::depthOnly(vk::Format::eD32Sfloat)
        .build(*initInfo_.raiiDevice);

    if (!renderPassOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow render pass");
        return false;
    }
    shadowRenderPass_ = std::move(*renderPassOpt);
    shadowRenderPass = **shadowRenderPass_;
    return true;
}

bool ShadowSystem::createShadowResources() {
    if (!initInfo_.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem::createShadowResources: raiiDevice is null");
        return false;
    }

    DepthArrayConfig cfg;
    cfg.extent = vk::Extent2D{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
    cfg.format = vk::Format::eD32Sfloat;
    cfg.arrayLayers = NUM_SHADOW_CASCADES;

    if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, cfg, csmResources)) {
        return false;
    }

    // Create framebuffers for each cascade
    vk::Device vkDevice(initInfo_.device);
    cascadeFramebuffers.resize(NUM_SHADOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        vk::ImageView layerView(*csmResources.layerViews[i]);
        auto framebufferInfo = vk::FramebufferCreateInfo{}
            .setRenderPass(shadowRenderPass)
            .setAttachments(layerView)
            .setWidth(SHADOW_MAP_SIZE)
            .setHeight(SHADOW_MAP_SIZE)
            .setLayers(1);

        try {
            cascadeFramebuffers[i] = vkDevice.createFramebuffer(framebufferInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cascade framebuffer %u: %s", i, e.what());
            return false;
        }
    }
    return true;
}

bool ShadowSystem::createShadowPipelineCommon(
    const std::string& vertShader,
    const std::string& fragShader,
    VkDescriptorSetLayout descriptorSetLayout,
    const VkVertexInputBindingDescription& binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    VkPipelineLayout& outLayout,
    VkPipeline& outPipeline)
{
    PipelineBuilder layoutBuilder(initInfo_.device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants));
    if (!layoutBuilder.buildPipelineLayout({descriptorSetLayout}, outLayout)) return false;

    GraphicsPipelineFactory factory(initInfo_.device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(initInfo_.shaderPath + "/" + vertShader, initInfo_.shaderPath + "/" + fragShader)
           .setRenderPass(shadowRenderPass)
           .setPipelineLayout(outLayout)
           .setExtent({SHADOW_MAP_SIZE, SHADOW_MAP_SIZE})
           .setVertexInput({binding}, attributes)
           .setDepthBias(1.25f, 1.75f);

    return factory.build(outPipeline);
}

bool ShadowSystem::createShadowPipeline() {
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    return createShadowPipelineCommon("shadow.vert.spv", "shadow.frag.spv",
        initInfo_.mainDescriptorSetLayout, binding, attrs, shadowPipelineLayout, shadowPipeline);
}

bool ShadowSystem::createSkinnedShadowPipeline() {
    if (initInfo_.skinnedDescriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Skinned shadow pipeline skipped (no skinned descriptor set layout)");
        return true;
    }
    auto binding = SkinnedVertex::getBindingDescription();
    auto attrsArr = SkinnedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    bool result = createShadowPipelineCommon("skinned_shadow.vert.spv", "shadow.frag.spv",
        initInfo_.skinnedDescriptorSetLayout, binding, attrs, skinnedShadowPipelineLayout, skinnedShadowPipeline);
    if (result) SDL_Log("Created skinned shadow pipeline for GPU-skinned character shadows");
    return result;
}

bool ShadowSystem::createDynamicShadowPipeline() {
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    return createShadowPipelineCommon("shadow.vert.spv", "shadow.frag.spv",
        initInfo_.mainDescriptorSetLayout, binding, attrs, dynamicShadowPipelineLayout, dynamicShadowPipeline);
}

bool ShadowSystem::createDynamicShadowResources() {
    if (!initInfo_.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem::createDynamicShadowResources: raiiDevice is null");
        return false;
    }

    pointShadowResources.resize(initInfo_.framesInFlight);
    spotShadowResources.resize(initInfo_.framesInFlight);
    pointShadowFramebuffers.resize(initInfo_.framesInFlight);
    spotShadowFramebuffers.resize(initInfo_.framesInFlight);

    for (uint32_t frame = 0; frame < initInfo_.framesInFlight; frame++) {
        // Point lights: cubemap array (6 faces per light)
        DepthArrayConfig pointCfg;
        pointCfg.extent = vk::Extent2D{DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        pointCfg.format = vk::Format::eD32Sfloat;
        pointCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS * 6;
        pointCfg.cubeCompatible = true;
        pointCfg.createSampler = (frame == 0);  // Only first frame needs sampler

        if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, pointCfg, pointShadowResources[frame])) {
            return false;
        }

        // Create point shadow framebuffers (only first 6 layers for now)
        vk::Device vkDevice(initInfo_.device);
        pointShadowFramebuffers[frame].resize(6);
        for (uint32_t i = 0; i < 6; i++) {
            vk::ImageView layerView(*pointShadowResources[frame].layerViews[i]);
            auto framebufferInfo = vk::FramebufferCreateInfo{}
                .setRenderPass(shadowRenderPass)
                .setAttachments(layerView)
                .setWidth(DYNAMIC_SHADOW_MAP_SIZE)
                .setHeight(DYNAMIC_SHADOW_MAP_SIZE)
                .setLayers(1);

            try {
                pointShadowFramebuffers[frame][i] = vkDevice.createFramebuffer(framebufferInfo);
            } catch (const vk::SystemError&) {
                return false;
            }
        }

        // Spot lights: 2D array
        DepthArrayConfig spotCfg;
        spotCfg.extent = vk::Extent2D{DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        spotCfg.format = vk::Format::eD32Sfloat;
        spotCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS;
        spotCfg.createSampler = (frame == 0);

        if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, spotCfg, spotShadowResources[frame])) {
            return false;
        }

        // Create spot shadow framebuffers
        spotShadowFramebuffers[frame].resize(MAX_SHADOW_CASTING_LIGHTS);
        for (uint32_t i = 0; i < MAX_SHADOW_CASTING_LIGHTS; i++) {
            vk::ImageView layerView(*spotShadowResources[frame].layerViews[i]);
            auto framebufferInfo = vk::FramebufferCreateInfo{}
                .setRenderPass(shadowRenderPass)
                .setAttachments(layerView)
                .setWidth(DYNAMIC_SHADOW_MAP_SIZE)
                .setHeight(DYNAMIC_SHADOW_MAP_SIZE)
                .setLayers(1);

            try {
                spotShadowFramebuffers[frame][i] = vkDevice.createFramebuffer(framebufferInfo);
            } catch (const vk::SystemError&) {
                return false;
            }
        }
    }

    return true;
}

void ShadowSystem::destroyDynamicShadowResources() {
    vk::Device vkDevice(initInfo_.device);
    for (uint32_t frame = 0; frame < initInfo_.framesInFlight; frame++) {
        for (auto fb : pointShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDevice.destroyFramebuffer(fb);
        }
        pointShadowFramebuffers[frame].clear();
        for (auto fb : spotShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDevice.destroyFramebuffer(fb);
        }
        spotShadowFramebuffers[frame].clear();
        if (frame < pointShadowResources.size()) pointShadowResources[frame].reset();
        if (frame < spotShadowResources.size()) spotShadowResources[frame].reset();
    }
}

bool ShadowSystem::createInstancedShadowResources() {
    vk::Device vkDevice(initInfo_.device);

    // Create descriptor set layout for instanced shadow rendering
    auto layoutOpt = DescriptorSetLayoutBuilder()
        .addBinding(BindingBuilder::storageBuffer(Bindings::SHADOW_INSTANCES, vk::ShaderStageFlagBits::eVertex))
        .build(*initInfo_.raiiDevice);
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow descriptor set layout");
        return false;
    }
    instancedShadowDescriptorSetLayout = **layoutOpt;

    // Create per-frame instance buffers (persistently mapped for fast CPU writes)
    instanceBuffers.resize(initInfo_.framesInFlight);
    instanceAllocations.resize(initInfo_.framesInFlight);
    instanceMappedPtrs.resize(initInfo_.framesInFlight);

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = MAX_SHADOW_INSTANCES * sizeof(glm::mat4);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (uint32_t i = 0; i < initInfo_.framesInFlight; i++) {
        VmaAllocationInfo allocResult;
        if (vmaCreateBuffer(initInfo_.allocator, &bufferInfo, &allocInfo,
                            &instanceBuffers[i], &instanceAllocations[i], &allocResult) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instance buffer %u", i);
            return false;
        }
        instanceMappedPtrs[i] = allocResult.pMappedData;
    }

    // Allocate descriptor sets using a temporary pool
    // Note: In production, use a proper descriptor pool manager
    auto poolSize = vk::DescriptorPoolSize{}
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(initInfo_.framesInFlight);

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(initInfo_.framesInFlight)
        .setPoolSizes(poolSize);

    VkDescriptorPool pool;
    try {
        pool = vkDevice.createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow descriptor pool: %s", e.what());
        return false;
    }

    std::vector<vk::DescriptorSetLayout> layouts(initInfo_.framesInFlight, vk::DescriptorSetLayout(instancedShadowDescriptorSetLayout));
    auto allocInfoDS = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(pool)
        .setSetLayouts(layouts);

    try {
        instancedShadowDescriptorSets = vkDevice.allocateDescriptorSets(allocInfoDS);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate instanced shadow descriptor sets: %s", e.what());
        return false;
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t i = 0; i < initInfo_.framesInFlight; i++) {
        DescriptorWriter()
            .add(WriteBuilder::storageBuffer(Bindings::SHADOW_INSTANCES,
                makeBufferInfo(instanceBuffers[i], VK_WHOLE_SIZE)))
            .update(initInfo_.device, instancedShadowDescriptorSets[i]);
    }

    SDL_Log("Created instanced shadow resources: %u frames, %u max instances", initInfo_.framesInFlight, MAX_SHADOW_INSTANCES);
    return true;
}

bool ShadowSystem::createInstancedShadowPipeline() {
    vk::Device vkDevice(initInfo_.device);

    // Create pipeline layout with both main descriptor set (for UBO) and instanced set (for SSBO)
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(InstancedShadowPushConstants));

    std::array<vk::DescriptorSetLayout, 2> setLayouts = {
        vk::DescriptorSetLayout(initInfo_.mainDescriptorSetLayout),           // Set 0: UBO with cascade matrices
        vk::DescriptorSetLayout(instancedShadowDescriptorSetLayout) // Set 1: Instance SSBO
    };

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayouts)
        .setPushConstantRanges(pushConstantRange);

    try {
        instancedShadowPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow pipeline layout: %s", e.what());
        return false;
    }

    // Create pipeline using the instanced shader
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());

    GraphicsPipelineFactory factory(initInfo_.device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(initInfo_.shaderPath + "/shadow_instanced.vert.spv", initInfo_.shaderPath + "/shadow.frag.spv")
           .setRenderPass(shadowRenderPass)
           .setPipelineLayout(instancedShadowPipelineLayout)
           .setExtent({SHADOW_MAP_SIZE, SHADOW_MAP_SIZE})
           .setVertexInput({binding}, attrs)
           .setDepthBias(1.25f, 1.75f);

    if (!factory.build(instancedShadowPipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow pipeline");
        return false;
    }

    SDL_Log("Created instanced shadow pipeline");
    return true;
}

void ShadowSystem::destroyInstancedShadowResources() {
    vk::Device vkDevice(initInfo_.device);

    for (uint32_t i = 0; i < instanceBuffers.size(); i++) {
        if (instanceBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(initInfo_.allocator, instanceBuffers[i], instanceAllocations[i]);
        }
    }
    instanceBuffers.clear();
    instanceAllocations.clear();
    instanceMappedPtrs.clear();

    // Note: Descriptor sets are freed when pool is destroyed
    instancedShadowDescriptorSets.clear();

    if (instancedShadowPipeline != VK_NULL_HANDLE) {
        vkDevice.destroyPipeline(instancedShadowPipeline);
        instancedShadowPipeline = VK_NULL_HANDLE;
    }
    if (instancedShadowPipelineLayout != VK_NULL_HANDLE) {
        vkDevice.destroyPipelineLayout(instancedShadowPipelineLayout);
        instancedShadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (instancedShadowDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDevice.destroyDescriptorSetLayout(instancedShadowDescriptorSetLayout);
        instancedShadowDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void ShadowSystem::drawShadowSceneInstanced(
    VkCommandBuffer cmd,
    uint32_t frameIndex,
    uint32_t cascadeIndex,
    const std::vector<Renderable>& sceneObjects)
{
    if (sceneObjects.empty() || instancedShadowPipeline == VK_NULL_HANDLE) return;
    if (frameIndex >= instanceMappedPtrs.size()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Group objects by mesh pointer (objects sharing the same mesh can be instanced)
    std::unordered_map<const Mesh*, std::vector<const Renderable*>> meshGroups;
    for (const auto& obj : sceneObjects) {
        if (!obj.castsShadow || !obj.mesh) continue;
        meshGroups[obj.mesh].push_back(&obj);
    }

    if (meshGroups.empty()) return;

    // Upload all instance transforms to the buffer
    auto* instanceData = static_cast<glm::mat4*>(instanceMappedPtrs[frameIndex]);
    uint32_t totalInstances = 0;

    // Build instance data and track offsets per mesh group
    struct MeshBatch {
        const Mesh* mesh;
        uint32_t instanceOffset;
        uint32_t instanceCount;
    };
    std::vector<MeshBatch> batches;
    batches.reserve(meshGroups.size());

    for (const auto& [mesh, objects] : meshGroups) {
        if (totalInstances + objects.size() > MAX_SHADOW_INSTANCES) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Shadow instance limit reached (%u), skipping remaining objects", MAX_SHADOW_INSTANCES);
            break;
        }

        MeshBatch batch;
        batch.mesh = mesh;
        batch.instanceOffset = totalInstances;
        batch.instanceCount = static_cast<uint32_t>(objects.size());

        for (const auto* obj : objects) {
            instanceData[totalInstances++] = obj->transform;
        }

        batches.push_back(batch);
    }

    if (batches.empty()) return;

    // Bind instanced shadow pipeline and descriptor sets
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, instancedShadowPipeline);

    // Draw each mesh batch with instancing
    for (const auto& batch : batches) {
        vk::Buffer vb[] = {batch.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, 1, vb, offsets);
        vkCmd.bindIndexBuffer(batch.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        InstancedShadowPushConstants push{};
        push.cascadeIndex = cascadeIndex;
        push.instanceOffset = batch.instanceOffset;

        vkCmd.pushConstants<InstancedShadowPushConstants>(
            instancedShadowPipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0, push);

        vkCmd.drawIndexed(batch.mesh->getIndexCount(), batch.instanceCount, 0, 0, 0);
        DIAG_RECORD_DRAW(); // One draw call, multiple instances
    }
}

void ShadowSystem::calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits) {
    splits.resize(NUM_SHADOW_CASCADES + 1);
    splits[0] = nearClip;

    float clipRange = farClip - nearClip;
    float ratio = farClip / nearClip;

    for (uint32_t i = 1; i <= NUM_SHADOW_CASCADES; i++) {
        float p = static_cast<float>(i) / NUM_SHADOW_CASCADES;
        float logSplit = nearClip * std::pow(ratio, p);
        float uniformSplit = nearClip + clipRange * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

glm::mat4 ShadowSystem::calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, float nearSplit, float farSplit) {
    glm::vec3 lightDirNorm = glm::normalize(lightDir);
    if (glm::length(lightDirNorm) < std::numeric_limits<float>::epsilon()) {
        lightDirNorm = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    glm::mat4 cameraProj = camera.getProjectionMatrix();
    cameraProj[1][1] *= -1.0f;

    float tanHalfFov = 1.0f / cameraProj[1][1];
    float aspect = cameraProj[1][1] / cameraProj[0][0];

    float nearHeight = nearSplit * tanHalfFov;
    float nearWidth = nearHeight * aspect;
    float farHeight = farSplit * tanHalfFov;
    float farWidth = farHeight * aspect;

    glm::mat4 invView = glm::inverse(camera.getViewMatrix());
    glm::vec3 camPos = glm::vec3(invView[3]);
    glm::vec3 camForward = -glm::vec3(invView[2]);
    glm::vec3 camRight = glm::vec3(invView[0]);
    glm::vec3 camUp = glm::vec3(invView[1]);

    glm::vec3 nearCenter = camPos + camForward * nearSplit;
    glm::vec3 farCenter = camPos + camForward * farSplit;

    std::array<glm::vec3, 8> frustumCorners{
        nearCenter - camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth + camUp * nearHeight,
        nearCenter - camRight * nearWidth + camUp * nearHeight,
        farCenter - camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth + camUp * farHeight,
        farCenter - camRight * farWidth + camUp * farHeight,
    };

    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) center += corner;
    center /= 8.0f;

    float radius = 0.0f;
    for (const auto& corner : frustumCorners) {
        radius = std::max(radius, glm::length(corner - center));
    }

    glm::vec3 up = (std::abs(lightDirNorm.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 lightPos = center + lightDirNorm * (radius + 50.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, center, up);

    float orthoSize = radius * 1.1f;
    float zRange = radius * 2.0f + 100.0f;

    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, zRange);
    lightProjection[1][1] *= -1.0f;
    lightProjection[2][2] = lightProjection[2][2] * 0.5f;
    lightProjection[3][2] = lightProjection[3][2] * 0.5f + 0.5f;

    return lightProjection * lightView;
}

void ShadowSystem::updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera) {
    const float shadowNear = 0.1f;
    const float shadowFar = 150.0f;
    const float lambda = 0.5f;

    calculateCascadeSplits(shadowNear, shadowFar, lambda, cascadeSplitDepths);

    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        cascadeMatrices[i] = calculateCascadeMatrix(lightDir, camera, cascadeSplitDepths[i], cascadeSplitDepths[i + 1]);
    }
}

void ShadowSystem::drawShadowScene(
    VkCommandBuffer cmd,
    VkPipelineLayout layout,
    uint32_t cascadeOrFaceIndex,
    const glm::mat4& lightMatrix,
    const std::vector<Renderable>& sceneObjects,
    const DrawCallback& terrainCallback,
    const DrawCallback& grassCallback,
    const DrawCallback& treeCallback,
    const DrawCallback& skinnedCallback)
{
    vk::CommandBuffer vkCmd(cmd);

    for (const auto& obj : sceneObjects) {
        if (!obj.castsShadow) continue;

        ShadowPushConstants push{};
        push.model = obj.transform;
        push.cascadeIndex = static_cast<int>(cascadeOrFaceIndex);
        vkCmd.pushConstants<ShadowPushConstants>(
            layout, vk::ShaderStageFlagBits::eVertex, 0, push);

        vk::Buffer vb[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, 1, vb, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
        DIAG_RECORD_DRAW();
    }

    if (terrainCallback) terrainCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (grassCallback) grassCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (treeCallback) treeCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (skinnedCallback) skinnedCallback(cmd, cascadeOrFaceIndex, lightMatrix);
}

void ShadowSystem::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex,
                                     VkDescriptorSet descriptorSet,
                                     const std::vector<Renderable>& sceneObjects,
                                     const DrawCallback& terrainDrawCallback,
                                     const DrawCallback& grassDrawCallback,
                                     const DrawCallback& treeDrawCallback,
                                     const DrawCallback& skinnedDrawCallback,
                                     const ComputeCallback& preCascadeComputeCallback) {
    vk::CommandBuffer vkCmd(cmd);

    // Check if instanced rendering is available
    bool useInstanced = instancedShadowPipeline != VK_NULL_HANDLE &&
                        frameIndex < instancedShadowDescriptorSets.size() &&
                        !sceneObjects.empty();

    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        // Run pre-cascade compute pass (GPU culling) BEFORE the render pass
        if (preCascadeComputeCallback) {
            preCascadeComputeCallback(cmd, frameIndex, cascade, cascadeMatrices[cascade]);
        }

        vk::ClearValue shadowClear;
        shadowClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        auto shadowPassInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(shadowRenderPass)
            .setFramebuffer(cascadeFramebuffers[cascade])
            .setRenderArea({{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}})
            .setClearValues(shadowClear);

        vkCmd.beginRenderPass(shadowPassInfo, vk::SubpassContents::eInline);

        if (useInstanced) {
            // Use instanced rendering for scene objects (rocks, detritus, etc.)
            // Bind descriptor sets for instanced pipeline
            std::array<vk::DescriptorSet, 2> descSets = {
                vk::DescriptorSet(descriptorSet),
                vk::DescriptorSet(instancedShadowDescriptorSets[frameIndex])
            };
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, instancedShadowPipeline);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, instancedShadowPipelineLayout,
                                     0, descSets, {});

            drawShadowSceneInstanced(cmd, frameIndex, cascade, sceneObjects);

            // Switch back to regular pipeline for callbacks (terrain, grass, trees)
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPipeline);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shadowPipelineLayout,
                                     0, vk::DescriptorSet(descriptorSet), {});
        } else {
            // Fallback: per-object rendering
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPipeline);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shadowPipelineLayout,
                                     0, vk::DescriptorSet(descriptorSet), {});

            // Draw scene objects individually
            for (const auto& obj : sceneObjects) {
                if (!obj.castsShadow) continue;

                ShadowPushConstants push{};
                push.model = obj.transform;
                push.cascadeIndex = static_cast<int>(cascade);
                vkCmd.pushConstants<ShadowPushConstants>(
                    shadowPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, push);

                vk::Buffer vb[] = {obj.mesh->getVertexBuffer()};
                vk::DeviceSize offsets[] = {0};
                vkCmd.bindVertexBuffers(0, 1, vb, offsets);
                vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
                vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
                DIAG_RECORD_DRAW();
            }
        }

        // Call terrain/grass/tree/skinned callbacks
        if (terrainDrawCallback) terrainDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (grassDrawCallback) grassDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (treeDrawCallback) treeDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (skinnedDrawCallback) skinnedDrawCallback(cmd, cascade, cascadeMatrices[cascade]);

        vkCmd.endRenderPass();
    }
}

void ShadowSystem::bindSkinnedShadowPipeline(VkCommandBuffer cmd, VkDescriptorSet descriptorSet) {
    if (skinnedShadowPipeline == VK_NULL_HANDLE) return;
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, skinnedShadowPipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skinnedShadowPipelineLayout,
                             0, vk::DescriptorSet(descriptorSet), {});
}

void ShadowSystem::recordSkinnedMeshShadow(VkCommandBuffer cmd, uint32_t cascade,
                                            const glm::mat4& modelMatrix,
                                            const SkinnedMesh& mesh) {
    if (skinnedShadowPipelineLayout == VK_NULL_HANDLE) return;

    vk::CommandBuffer vkCmd(cmd);

    ShadowPushConstants shadowPush{};
    shadowPush.model = modelMatrix;
    shadowPush.cascadeIndex = static_cast<int>(cascade);
    vkCmd.pushConstants<ShadowPushConstants>(
        skinnedShadowPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, shadowPush);

    vk::Buffer vertexBuffers[] = {mesh.getVertexBuffer()};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(mesh.getIndexBuffer(), 0, vk::IndexType::eUint32);
    vkCmd.drawIndexed(mesh.getIndexCount(), 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}

void ShadowSystem::renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                        VkDescriptorSet descriptorSet,
                                        const std::vector<Renderable>& sceneObjects,
                                        const DrawCallback& terrainDrawCallback,
                                        const DrawCallback& grassDrawCallback,
                                        const DrawCallback& skinnedDrawCallback,
                                        const std::vector<Light>& visibleLights) {
    if (dynamicShadowPipeline == VK_NULL_HANDLE) return;

    vk::CommandBuffer vkCmd(cmd);

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE))
        .setHeight(static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE});

    uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(visibleLights.size(), MAX_SHADOW_CASTING_LIGHTS));

    for (uint32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
        const Light& light = visibleLights[lightIndex];
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            if (frameIndex >= pointShadowFramebuffers.size()) continue;
            for (uint32_t face = 0; face < pointShadowFramebuffers[frameIndex].size(); face++) {
                vk::ClearValue clear;
                clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                auto passInfo = vk::RenderPassBeginInfo{}
                    .setRenderPass(shadowRenderPass)
                    .setFramebuffer(pointShadowFramebuffers[frameIndex][face])
                    .setRenderArea({{0, 0}, {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}})
                    .setClearValues(clear);

                vkCmd.beginRenderPass(passInfo, vk::SubpassContents::eInline);
                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, dynamicShadowPipeline);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, dynamicShadowPipelineLayout,
                                         0, vk::DescriptorSet(descriptorSet), {});
                vkCmd.setViewport(0, viewport);
                vkCmd.setScissor(0, scissor);

                drawShadowScene(cmd, dynamicShadowPipelineLayout, face, glm::mat4(1.0f),
                                sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

                vkCmd.endRenderPass();
            }
        } else {
            if (frameIndex >= spotShadowFramebuffers.size() || lightIndex >= spotShadowFramebuffers[frameIndex].size()) continue;

            vk::ClearValue clear;
            clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

            auto passInfo = vk::RenderPassBeginInfo{}
                .setRenderPass(shadowRenderPass)
                .setFramebuffer(spotShadowFramebuffers[frameIndex][lightIndex])
                .setRenderArea({{0, 0}, {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}})
                .setClearValues(clear);

            vkCmd.beginRenderPass(passInfo, vk::SubpassContents::eInline);
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, dynamicShadowPipeline);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, dynamicShadowPipelineLayout,
                                     0, vk::DescriptorSet(descriptorSet), {});
            vkCmd.setViewport(0, viewport);
            vkCmd.setScissor(0, scissor);

            drawShadowScene(cmd, dynamicShadowPipelineLayout, lightIndex, glm::mat4(1.0f),
                            sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

            vkCmd.endRenderPass();
        }
    }
}
