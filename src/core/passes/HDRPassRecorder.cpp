#include "HDRPassRecorder.h"
#include "../RendererSystems.h"
#include "UBOs.h"  // For PushConstants (generated from shaders)

// Subsystem includes
#include "PostProcessSystem.h"
#include "SkySystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "SkinnedMeshRenderer.h"
#include "GrassSystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "LeafSystem.h"
#include "WeatherSystem.h"
#include "DebugLineSystem.h"
#include "RockSystem.h"
#include "DetritusSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "WindSystem.h"
#include "Profiler.h"
#include "AnimatedCharacter.h"
#include "Mesh.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

HDRPassRecorder::HDRPassRecorder(RendererSystems& systems)
    : systems_(systems)
{
}

void HDRPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    vk::CommandBuffer vkCmd(cmd);

    // Wrap entire HDR pass in a GPU zone to measure total time
    systems_.profiler().beginGpuZone(cmd, "HDRPass");

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = systems_.postProcess().getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(systems_.postProcess().getHDRRenderPass())
        .setFramebuffer(systems_.postProcess().getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eInline);

    // Draw sky (with atmosphere LUT bindings)
    systems_.profiler().beginGpuZone(cmd, "HDR:Sky");
    systems_.sky().recordDraw(cmd, frameIndex);
    systems_.profiler().endGpuZone(cmd, "HDR:Sky");

    // Draw terrain (LEB adaptive tessellation)
    if (config_.terrainEnabled) {
        systems_.profiler().beginGpuZone(cmd, "HDR:Terrain");
        systems_.terrain().recordDraw(cmd, frameIndex);
        systems_.profiler().endGpuZone(cmd, "HDR:Terrain");
    }

    // Draw Catmull-Clark subdivision surfaces
    systems_.profiler().beginGpuZone(cmd, "HDR:CatmullClark");
    systems_.catmullClark().recordDraw(cmd, frameIndex);
    systems_.profiler().endGpuZone(cmd, "HDR:CatmullClark");

    // Draw scene objects (static meshes)
    systems_.profiler().beginGpuZone(cmd, "HDR:SceneObjects");
    if (config_.sceneObjectsPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *config_.sceneObjectsPipeline);
    }
    recordSceneObjects(cmd, frameIndex);
    systems_.profiler().endGpuZone(cmd, "HDR:SceneObjects");

    // Draw skinned character with GPU skinning
    systems_.profiler().beginGpuZone(cmd, "HDR:SkinnedChar");
    {
        SceneBuilder& sceneBuilder = systems_.scene().getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                systems_.skinnedMesh().record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    }
    systems_.profiler().endGpuZone(cmd, "HDR:SkinnedChar");

    // Draw grass
    systems_.profiler().beginGpuZone(cmd, "HDR:Grass");
    systems_.grass().recordDraw(cmd, frameIndex, time);
    systems_.profiler().endGpuZone(cmd, "HDR:Grass");

    // Draw water surface (after opaque geometry, blended)
    // Use temporal tile culling: skip if no tiles were visible last frame
    if (!systems_.hasWaterTileCull() ||
        systems_.waterTileCull().wasWaterVisibleLastFrame(frameIndex)) {
        systems_.profiler().beginGpuZone(cmd, "HDR:Water");
        systems_.water().recordDraw(cmd, frameIndex);
        systems_.profiler().endGpuZone(cmd, "HDR:Water");
    }

    // Draw falling leaves - after grass, before weather
    systems_.profiler().beginGpuZone(cmd, "HDR:Leaves");
    systems_.leaf().recordDraw(cmd, frameIndex, time);
    systems_.profiler().endGpuZone(cmd, "HDR:Leaves");

    // Draw weather particles (rain/snow) - after opaque geometry
    systems_.profiler().beginGpuZone(cmd, "HDR:Weather");
    systems_.weather().recordDraw(cmd, frameIndex, time);
    systems_.profiler().endGpuZone(cmd, "HDR:Weather");

    // Draw debug lines (if any are present - includes physics debug and road/river visualization)
    recordDebugLines(cmd);

    vkCmd.endRenderPass();

    systems_.profiler().endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    vk::CommandBuffer vkCmd(cmd);

    // Wrap entire HDR pass in a GPU zone to measure total time
    systems_.profiler().beginGpuZone(cmd, "HDRPass");

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = systems_.postProcess().getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(systems_.postProcess().getHDRRenderPass())
        .setFramebuffer(systems_.postProcess().getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    // Begin render pass with secondary command buffer execution
    vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

    // Execute all pre-recorded secondary command buffers
    // Secondary buffers include all draw calls including debug lines
    if (!secondaries.empty()) {
        vkCmd.executeCommands(secondaries);
    }

    vkCmd.endRenderPass();

    systems_.profiler().endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot) {
    vk::CommandBuffer vkCmd(cmd);

    // Each slot records a group of draw calls to a secondary command buffer
    // The secondary buffer has already been begun with render pass inheritance

    switch (slot) {
    case 0:
        // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
        systems_.profiler().beginGpuZone(cmd, "HDR:Sky");
        systems_.sky().recordDraw(cmd, frameIndex);
        systems_.profiler().endGpuZone(cmd, "HDR:Sky");

        if (config_.terrainEnabled) {
            systems_.profiler().beginGpuZone(cmd, "HDR:Terrain");
            systems_.terrain().recordDraw(cmd, frameIndex);
            systems_.profiler().endGpuZone(cmd, "HDR:Terrain");
        }

        systems_.profiler().beginGpuZone(cmd, "HDR:CatmullClark");
        systems_.catmullClark().recordDraw(cmd, frameIndex);
        systems_.profiler().endGpuZone(cmd, "HDR:CatmullClark");
        break;

    case 1:
        // Slot 1: Scene Objects + Skinned Character (scene meshes)
        systems_.profiler().beginGpuZone(cmd, "HDR:SceneObjects");
        if (config_.sceneObjectsPipeline) {
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *config_.sceneObjectsPipeline);
        }
        recordSceneObjects(cmd, frameIndex);
        systems_.profiler().endGpuZone(cmd, "HDR:SceneObjects");

        systems_.profiler().beginGpuZone(cmd, "HDR:SkinnedChar");
        {
            SceneBuilder& sceneBuilder = systems_.scene().getSceneBuilder();
            if (sceneBuilder.hasCharacter()) {
                const auto& sceneObjects = sceneBuilder.getRenderables();
                size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
                if (playerIndex < sceneObjects.size()) {
                    const Renderable& playerObj = sceneObjects[playerIndex];
                    systems_.skinnedMesh().record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
                }
            }
        }
        systems_.profiler().endGpuZone(cmd, "HDR:SkinnedChar");
        break;

    case 2:
        // Slot 2: Grass + Water + Leaves + Weather + Debug lines (vegetation/effects/debug)
        systems_.profiler().beginGpuZone(cmd, "HDR:Grass");
        systems_.grass().recordDraw(cmd, frameIndex, time);
        systems_.profiler().endGpuZone(cmd, "HDR:Grass");

        if (!systems_.hasWaterTileCull() ||
            systems_.waterTileCull().wasWaterVisibleLastFrame(frameIndex)) {
            systems_.profiler().beginGpuZone(cmd, "HDR:Water");
            systems_.water().recordDraw(cmd, frameIndex);
            systems_.profiler().endGpuZone(cmd, "HDR:Water");
        }

        systems_.profiler().beginGpuZone(cmd, "HDR:Leaves");
        systems_.leaf().recordDraw(cmd, frameIndex, time);
        systems_.profiler().endGpuZone(cmd, "HDR:Leaves");

        systems_.profiler().beginGpuZone(cmd, "HDR:Weather");
        systems_.weather().recordDraw(cmd, frameIndex, time);
        systems_.profiler().endGpuZone(cmd, "HDR:Weather");

        recordDebugLines(cmd);
        break;
    }
}

void HDRPassRecorder::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!config_.pipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "HDRPassRecorder: pipelineLayout not set");
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = systems_.scene().getSceneBuilder().getMaterialRegistry();

    // Helper lambda to render a scene object with a descriptor set
    auto renderObject = [&](const Renderable& obj, VkDescriptorSet descSet) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.opacity = obj.opacity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);
        push.pbrFlags = obj.pbrFlags;
        push.alphaTestThreshold = obj.alphaTestThreshold;

        vkCmd.pushConstants<PushConstants>(
            *config_.pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *config_.pipelineLayout, 0, vk::DescriptorSet(descSet), {});

        vk::Buffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // Render scene manager objects using MaterialRegistry for descriptor set lookup
    const auto& sceneObjects = systems_.scene().getRenderables();
    size_t playerIndex = systems_.scene().getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = systems_.scene().getSceneBuilder().hasCharacter();

    // Build sorted indices by materialId to minimize descriptor set switches
    std::vector<size_t> sortedIndices(sceneObjects.size());
    std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](size_t a, size_t b) {
        return sceneObjects[a].materialId < sceneObjects[b].materialId;
    });

    MaterialId lastMaterialId = INVALID_MATERIAL_ID;
    VkDescriptorSet currentDescSet = VK_NULL_HANDLE;

    for (size_t i : sortedIndices) {
        // Skip player character (rendered separately with GPU skinning)
        if (hasCharacter && i == playerIndex) {
            continue;
        }

        const auto& obj = sceneObjects[i];

        // Only update descriptor set when material changes
        if (obj.materialId != lastMaterialId) {
            currentDescSet = materialRegistry.getDescriptorSet(obj.materialId, frameIndex);
            if (currentDescSet == VK_NULL_HANDLE) {
                // Skip objects with invalid materialId
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping object with invalid materialId %u", obj.materialId);
                continue;
            }
            lastMaterialId = obj.materialId;
        }
        renderObject(obj, currentDescSet);
    }

    // Render procedural rocks (RockSystem owns its own descriptor sets)
    if (systems_.rock().hasDescriptorSets()) {
        VkDescriptorSet rockDescSet = systems_.rock().getDescriptorSet(frameIndex);
        for (const auto& rock : systems_.rock().getSceneObjects()) {
            renderObject(rock, rockDescSet);
        }
    }

    // Render woodland detritus (DetritusSystem owns its own descriptor sets)
    if (systems_.detritus() && systems_.detritus()->hasDescriptorSets()) {
        VkDescriptorSet detritusDescSet = systems_.detritus()->getDescriptorSet(frameIndex);
        for (const auto& detritus : systems_.detritus()->getSceneObjects()) {
            renderObject(detritus, detritusDescSet);
        }
    }

    // Render procedural trees using dedicated TreeRenderer with wind animation
    if (systems_.tree() && systems_.treeRenderer()) {
        systems_.treeRenderer()->render(cmd, frameIndex, systems_.wind().getTime(), *systems_.tree(), systems_.treeLOD());
    }

    // Render tree impostors for distant trees
    if (systems_.treeLOD()) {
        auto* impostorCull = systems_.impostorCull();
        if (impostorCull && impostorCull->getTreeCount() > 0) {
            // Use GPU-culled indirect rendering
            systems_.treeLOD()->renderImpostorsGPUCulled(
                cmd, frameIndex,
                systems_.globalBuffers().uniformBuffers.buffers[frameIndex],
                systems_.shadow().getShadowImageView(),
                systems_.shadow().getShadowSampler(),
                impostorCull->getVisibleImpostorBuffer(),
                impostorCull->getIndirectDrawBuffer()
            );
        } else {
            // Fall back to CPU-culled rendering
            systems_.treeLOD()->renderImpostors(
                cmd, frameIndex,
                systems_.globalBuffers().uniformBuffers.buffers[frameIndex],
                systems_.shadow().getShadowImageView(),
                systems_.shadow().getShadowSampler()
            );
        }
    }
}

void HDRPassRecorder::recordDebugLines(VkCommandBuffer cmd) {
    if (!systems_.debugLine().hasLines()) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Set up viewport and scissor for debug rendering
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(systems_.postProcess().getExtent().width))
        .setHeight(static_cast<float>(systems_.postProcess().getExtent().height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    VkExtent2D debugExtent = systems_.postProcess().getExtent();
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{}.setWidth(debugExtent.width).setHeight(debugExtent.height));
    vkCmd.setScissor(0, scissor);

    // Need to get viewProj from the config (stored by Renderer)
    // For now, use identity if not set (could be improved by always passing viewProj)
    glm::mat4 viewProj = config_.lastViewProj ? *config_.lastViewProj : glm::mat4(1.0f);
    systems_.debugLine().recordCommands(cmd, viewProj);
}
