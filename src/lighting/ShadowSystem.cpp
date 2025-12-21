#include "ShadowSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "PipelineBuilder.h"
#include "GraphicsPipelineFactory.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>

// Factory implementations
std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<ShadowSystem>(new ShadowSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitContext& ctx,
                                                    VkDescriptorSetLayout mainDescriptorSetLayout_,
                                                    VkDescriptorSetLayout skinnedDescriptorSetLayout_) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.mainDescriptorSetLayout = mainDescriptorSetLayout_;
    info.skinnedDescriptorSetLayout = skinnedDescriptorSetLayout_;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    return create(info);
}

// Destructor
ShadowSystem::~ShadowSystem() {
    cleanup();
}

bool ShadowSystem::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    mainDescriptorSetLayout = info.mainDescriptorSetLayout;
    skinnedDescriptorSetLayout = info.skinnedDescriptorSetLayout;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createShadowRenderPass()) return false;
    if (!createShadowResources()) return false;
    if (!createDynamicShadowResources()) return false;
    if (!createShadowPipeline()) return false;
    if (!createSkinnedShadowPipeline()) return false;
    if (!createDynamicShadowPipeline()) return false;

    return true;
}

void ShadowSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    // Pipeline cleanup
    if (shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, shadowPipeline, nullptr);
    if (shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
    if (skinnedShadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skinnedShadowPipeline, nullptr);
    if (skinnedShadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, skinnedShadowPipelineLayout, nullptr);
    if (dynamicShadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, dynamicShadowPipeline, nullptr);
    if (dynamicShadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, dynamicShadowPipelineLayout, nullptr);

    // CSM cleanup
    VulkanResourceFactory::destroyFramebuffers(device, cascadeFramebuffers);
    csmResources.destroy(device, allocator);

    // Dynamic shadow cleanup
    destroyDynamicShadowResources();

    // Render pass
    if (shadowRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, shadowRenderPass, nullptr);

    device = VK_NULL_HANDLE;
}

bool ShadowSystem::createShadowRenderPass() {
    VulkanResourceFactory::RenderPassConfig cfg;
    cfg.depthFormat = VK_FORMAT_D32_SFLOAT;
    cfg.depthOnly = true;
    cfg.clearDepth = true;
    cfg.storeDepth = true;
    cfg.finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return VulkanResourceFactory::createRenderPass(device, cfg, shadowRenderPass);
}

bool ShadowSystem::createShadowResources() {
    VulkanResourceFactory::DepthArrayConfig cfg;
    cfg.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
    cfg.format = VK_FORMAT_D32_SFLOAT;
    cfg.arrayLayers = NUM_SHADOW_CASCADES;

    if (!VulkanResourceFactory::createDepthArrayResources(device, allocator, cfg, csmResources)) {
        return false;
    }

    return VulkanResourceFactory::createDepthOnlyFramebuffers(
        device, shadowRenderPass, csmResources.layerViews,
        {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}, cascadeFramebuffers);
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
    PipelineBuilder layoutBuilder(device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants));
    if (!layoutBuilder.buildPipelineLayout({descriptorSetLayout}, outLayout)) return false;

    GraphicsPipelineFactory factory(device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(shaderPath + "/" + vertShader, shaderPath + "/" + fragShader)
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
        mainDescriptorSetLayout, binding, attrs, shadowPipelineLayout, shadowPipeline);
}

bool ShadowSystem::createSkinnedShadowPipeline() {
    if (skinnedDescriptorSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Skinned shadow pipeline skipped (no skinned descriptor set layout)");
        return true;
    }
    auto binding = SkinnedVertex::getBindingDescription();
    auto attrsArr = SkinnedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    bool result = createShadowPipelineCommon("skinned_shadow.vert.spv", "shadow.frag.spv",
        skinnedDescriptorSetLayout, binding, attrs, skinnedShadowPipelineLayout, skinnedShadowPipeline);
    if (result) SDL_Log("Created skinned shadow pipeline for GPU-skinned character shadows");
    return result;
}

bool ShadowSystem::createDynamicShadowPipeline() {
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    return createShadowPipelineCommon("shadow.vert.spv", "shadow.frag.spv",
        mainDescriptorSetLayout, binding, attrs, dynamicShadowPipelineLayout, dynamicShadowPipeline);
}

bool ShadowSystem::createDynamicShadowResources() {
    pointShadowResources.resize(framesInFlight);
    spotShadowResources.resize(framesInFlight);
    pointShadowFramebuffers.resize(framesInFlight);
    spotShadowFramebuffers.resize(framesInFlight);

    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        // Point lights: cubemap array (6 faces per light)
        VulkanResourceFactory::DepthArrayConfig pointCfg;
        pointCfg.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        pointCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS * 6;
        pointCfg.cubeCompatible = true;
        pointCfg.createSampler = (frame == 0);  // Only first frame needs sampler

        if (!VulkanResourceFactory::createDepthArrayResources(device, allocator, pointCfg, pointShadowResources[frame])) {
            return false;
        }

        // Create point shadow framebuffers (only first 6 layers for now)
        std::vector<VkImageView> pointViews(pointShadowResources[frame].layerViews.begin(),
                                            pointShadowResources[frame].layerViews.begin() + 6);
        if (!VulkanResourceFactory::createDepthOnlyFramebuffers(device, shadowRenderPass, pointViews,
                {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}, pointShadowFramebuffers[frame])) {
            return false;
        }

        // Spot lights: 2D array
        VulkanResourceFactory::DepthArrayConfig spotCfg;
        spotCfg.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        spotCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS;
        spotCfg.createSampler = (frame == 0);

        if (!VulkanResourceFactory::createDepthArrayResources(device, allocator, spotCfg, spotShadowResources[frame])) {
            return false;
        }

        if (!VulkanResourceFactory::createDepthOnlyFramebuffers(device, shadowRenderPass, spotShadowResources[frame].layerViews,
                {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}, spotShadowFramebuffers[frame])) {
            return false;
        }
    }

    return true;
}

void ShadowSystem::destroyDynamicShadowResources() {
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        VulkanResourceFactory::destroyFramebuffers(device, pointShadowFramebuffers[frame]);
        VulkanResourceFactory::destroyFramebuffers(device, spotShadowFramebuffers[frame]);
        if (frame < pointShadowResources.size()) pointShadowResources[frame].destroy(device, allocator);
        if (frame < spotShadowResources.size()) spotShadowResources[frame].destroy(device, allocator);
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
    for (const auto& obj : sceneObjects) {
        if (!obj.castsShadow) continue;

        ShadowPushConstants push{};
        push.model = obj.transform;
        push.cascadeIndex = static_cast<int>(cascadeOrFaceIndex);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

        VkBuffer vb[] = {obj.mesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
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
                                     const DrawCallback& skinnedDrawCallback) {
    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = shadowRenderPass;
        shadowPassInfo.framebuffer = cascadeFramebuffers[cascade];
        shadowPassInfo.renderArea.offset = {0, 0};
        shadowPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;

        vkCmdBeginRenderPass(cmd, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        drawShadowScene(cmd, shadowPipelineLayout, cascade, cascadeMatrices[cascade],
                        sceneObjects, terrainDrawCallback, grassDrawCallback, treeDrawCallback, skinnedDrawCallback);

        vkCmdEndRenderPass(cmd);
    }
}

void ShadowSystem::bindSkinnedShadowPipeline(VkCommandBuffer cmd, VkDescriptorSet descriptorSet) {
    if (skinnedShadowPipeline == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
}

void ShadowSystem::recordSkinnedMeshShadow(VkCommandBuffer cmd, uint32_t cascade,
                                            const glm::mat4& modelMatrix,
                                            const SkinnedMesh& mesh) {
    if (skinnedShadowPipelineLayout == VK_NULL_HANDLE) return;

    ShadowPushConstants shadowPush{};
    shadowPush.model = modelMatrix;
    shadowPush.cascadeIndex = static_cast<int>(cascade);
    vkCmdPushConstants(cmd, skinnedShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &shadowPush);

    VkBuffer vertexBuffers[] = {mesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.getIndexCount(), 1, 0, 0, 0);
}

void ShadowSystem::renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                                        VkDescriptorSet descriptorSet,
                                        const std::vector<Renderable>& sceneObjects,
                                        const DrawCallback& terrainDrawCallback,
                                        const DrawCallback& grassDrawCallback,
                                        const DrawCallback& skinnedDrawCallback,
                                        const std::vector<Light>& visibleLights) {
    if (dynamicShadowPipeline == VK_NULL_HANDLE) return;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE),
                        static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}};

    uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(visibleLights.size(), MAX_SHADOW_CASTING_LIGHTS));

    for (uint32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
        const Light& light = visibleLights[lightIndex];
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            if (frameIndex >= pointShadowFramebuffers.size()) continue;
            for (uint32_t face = 0; face < pointShadowFramebuffers[frameIndex].size(); face++) {
                VkRenderPassBeginInfo passInfo{};
                passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                passInfo.renderPass = shadowRenderPass;
                passInfo.framebuffer = pointShadowFramebuffers[frameIndex][face];
                passInfo.renderArea.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

                VkClearValue clear{};
                clear.depthStencil = {1.0f, 0};
                passInfo.clearValueCount = 1;
                passInfo.pClearValues = &clear;

                vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                drawShadowScene(cmd, dynamicShadowPipelineLayout, face, glm::mat4(1.0f),
                                sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

                vkCmdEndRenderPass(cmd);
            }
        } else {
            if (frameIndex >= spotShadowFramebuffers.size() || lightIndex >= spotShadowFramebuffers[frameIndex].size()) continue;

            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = shadowRenderPass;
            passInfo.framebuffer = spotShadowFramebuffers[frameIndex][lightIndex];
            passInfo.renderArea.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

            VkClearValue clear{};
            clear.depthStencil = {1.0f, 0};
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clear;

            vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamicShadowPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            drawShadowScene(cmd, dynamicShadowPipelineLayout, lightIndex, glm::mat4(1.0f),
                            sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

            vkCmdEndRenderPass(cmd);
        }
    }
}
