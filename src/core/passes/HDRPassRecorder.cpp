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
#include "ScatterSystem.h"
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
#include "NPCManager.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

HDRPassRecorder::HDRPassRecorder(const HDRPassResources& resources)
    : resources_(resources)
{
}

HDRPassRecorder::HDRPassRecorder(RendererSystems& systems)
    : resources_(HDRPassResources::collect(systems))
{
}

void HDRPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    vk::CommandBuffer vkCmd(cmd);

    // Wrap entire HDR pass in a GPU zone to measure total time
    resources_.profiler->beginGpuZone(cmd, "HDRPass");

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = resources_.postProcess->getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(resources_.postProcess->getHDRRenderPass())
        .setFramebuffer(resources_.postProcess->getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eInline);

    // Draw sky (with atmosphere LUT bindings)
    resources_.profiler->beginGpuZone(cmd, "HDR:Sky");
    resources_.sky->recordDraw(cmd, frameIndex);
    resources_.profiler->endGpuZone(cmd, "HDR:Sky");

    // Draw terrain (LEB adaptive tessellation)
    if (config_.terrainEnabled) {
        resources_.profiler->beginGpuZone(cmd, "HDR:Terrain");
        resources_.terrain->recordDraw(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:Terrain");
    }

    // Draw Catmull-Clark subdivision surfaces
    resources_.profiler->beginGpuZone(cmd, "HDR:CatmullClark");
    resources_.geometry.catmullClark().recordDraw(cmd, frameIndex);
    resources_.profiler->endGpuZone(cmd, "HDR:CatmullClark");

    // Draw scene objects (static meshes)
    resources_.profiler->beginGpuZone(cmd, "HDR:SceneObjects");
    if (config_.sceneObjectsPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *config_.sceneObjectsPipeline);
    }
    recordSceneObjects(cmd, frameIndex);
    resources_.profiler->endGpuZone(cmd, "HDR:SceneObjects");

    // Draw skinned character with GPU skinning
    resources_.profiler->beginGpuZone(cmd, "HDR:SkinnedChar");
    {
        SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                resources_.skinnedMesh->record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    }
    resources_.profiler->endGpuZone(cmd, "HDR:SkinnedChar");

    // Draw NPCs with skinned mesh (shares mesh with player, different bone matrices)
    if (resources_.npcManager) {
        resources_.profiler->beginGpuZone(cmd, "HDR:NPCs");
        resources_.npcManager->render(cmd, frameIndex, *resources_.skinnedMesh);
        resources_.profiler->endGpuZone(cmd, "HDR:NPCs");
    }

    // Draw grass
    resources_.profiler->beginGpuZone(cmd, "HDR:Grass");
    resources_.vegetation.grass().recordDraw(cmd, frameIndex, time);
    resources_.profiler->endGpuZone(cmd, "HDR:Grass");

    // Draw water surface (after opaque geometry, blended)
    // Use temporal tile culling: skip if no tiles were visible last frame
    if (!resources_.hasWaterTileCull() ||
        resources_.waterTileCull->wasWaterVisibleLastFrame(frameIndex)) {
        resources_.profiler->beginGpuZone(cmd, "HDR:Water");
        resources_.water->recordDraw(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:Water");
    }

    // Draw falling leaves - after grass, before weather
    resources_.profiler->beginGpuZone(cmd, "HDR:Leaves");
    resources_.snow.leaf().recordDraw(cmd, frameIndex, time);
    resources_.profiler->endGpuZone(cmd, "HDR:Leaves");

    // Draw weather particles (rain/snow) - after opaque geometry
    resources_.profiler->beginGpuZone(cmd, "HDR:Weather");
    resources_.snow.weather().recordDraw(cmd, frameIndex, time);
    resources_.profiler->endGpuZone(cmd, "HDR:Weather");

    // Draw debug lines (if any are present - includes physics debug and road/river visualization)
    recordDebugLines(cmd);

    vkCmd.endRenderPass();

    resources_.profiler->endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    vk::CommandBuffer vkCmd(cmd);

    // Wrap entire HDR pass in a GPU zone to measure total time
    resources_.profiler->beginGpuZone(cmd, "HDRPass");

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = resources_.postProcess->getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(resources_.postProcess->getHDRRenderPass())
        .setFramebuffer(resources_.postProcess->getHDRFramebuffer())
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

    resources_.profiler->endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot) {
    vk::CommandBuffer vkCmd(cmd);

    // Each slot records a group of draw calls to a secondary command buffer
    // The secondary buffer has already been begun with render pass inheritance

    switch (slot) {
    case 0:
        // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
        resources_.profiler->beginGpuZone(cmd, "HDR:Sky");
        resources_.sky->recordDraw(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:Sky");

        if (config_.terrainEnabled) {
            resources_.profiler->beginGpuZone(cmd, "HDR:Terrain");
            resources_.terrain->recordDraw(cmd, frameIndex);
            resources_.profiler->endGpuZone(cmd, "HDR:Terrain");
        }

        resources_.profiler->beginGpuZone(cmd, "HDR:CatmullClark");
        resources_.geometry.catmullClark().recordDraw(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:CatmullClark");
        break;

    case 1:
        // Slot 1: Scene Objects + Skinned Character (scene meshes)
        resources_.profiler->beginGpuZone(cmd, "HDR:SceneObjects");
        if (config_.sceneObjectsPipeline) {
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *config_.sceneObjectsPipeline);
        }
        recordSceneObjects(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:SceneObjects");

        resources_.profiler->beginGpuZone(cmd, "HDR:SkinnedChar");
        {
            SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
            if (sceneBuilder.hasCharacter()) {
                const auto& sceneObjects = sceneBuilder.getRenderables();
                size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
                if (playerIndex < sceneObjects.size()) {
                    const Renderable& playerObj = sceneObjects[playerIndex];
                    resources_.skinnedMesh->record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
                }
            }
        }
        resources_.profiler->endGpuZone(cmd, "HDR:SkinnedChar");
        break;

    case 2:
        // Slot 2: Grass + Water + Leaves + Weather + Debug lines (vegetation/effects/debug)
        resources_.profiler->beginGpuZone(cmd, "HDR:Grass");
        resources_.vegetation.grass().recordDraw(cmd, frameIndex, time);
        resources_.profiler->endGpuZone(cmd, "HDR:Grass");

        if (!resources_.hasWaterTileCull() ||
            resources_.waterTileCull->wasWaterVisibleLastFrame(frameIndex)) {
            resources_.profiler->beginGpuZone(cmd, "HDR:Water");
            resources_.water->recordDraw(cmd, frameIndex);
            resources_.profiler->endGpuZone(cmd, "HDR:Water");
        }

        resources_.profiler->beginGpuZone(cmd, "HDR:Leaves");
        resources_.snow.leaf().recordDraw(cmd, frameIndex, time);
        resources_.profiler->endGpuZone(cmd, "HDR:Leaves");

        resources_.profiler->beginGpuZone(cmd, "HDR:Weather");
        resources_.snow.weather().recordDraw(cmd, frameIndex, time);
        resources_.profiler->endGpuZone(cmd, "HDR:Weather");

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
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();

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
    const auto& sceneObjects = resources_.scene->getRenderables();
    size_t playerIndex = resources_.scene->getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = resources_.scene->getSceneBuilder().hasCharacter();

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

    // Render procedural rocks (ScatterSystem owns its own descriptor sets)
    if (resources_.vegetation.rocks().hasDescriptorSets()) {
        VkDescriptorSet rockDescSet = resources_.vegetation.rocks().getDescriptorSet(frameIndex);
        for (const auto& rock : resources_.vegetation.rocks().getSceneObjects()) {
            renderObject(rock, rockDescSet);
        }
    }

    // Render woodland detritus (ScatterSystem owns its own descriptor sets)
    if (resources_.vegetation.hasDetritus() && resources_.vegetation.detritus()->hasDescriptorSets()) {
        VkDescriptorSet detritusDescSet = resources_.vegetation.detritus()->getDescriptorSet(frameIndex);
        for (const auto& detritus : resources_.vegetation.detritus()->getSceneObjects()) {
            renderObject(detritus, detritusDescSet);
        }
    }

    // Render procedural trees using dedicated TreeRenderer with wind animation
    if (resources_.vegetation.hasTree() && resources_.vegetation.hasTreeRenderer()) {
        resources_.vegetation.treeRenderer()->render(cmd, frameIndex, resources_.wind->getTime(), *resources_.vegetation.tree(), resources_.vegetation.treeLOD());
    }

    // Render tree impostors for distant trees
    if (resources_.vegetation.hasTreeLOD()) {
        auto* impostorCull = resources_.vegetation.impostorCull();
        if (impostorCull && impostorCull->getTreeCount() > 0) {
            // Use GPU-culled indirect rendering
            resources_.vegetation.treeLOD()->renderImpostorsGPUCulled(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler(),
                impostorCull->getVisibleImpostorBuffer(),
                impostorCull->getIndirectDrawBuffer()
            );
        } else {
            // Fall back to CPU-culled rendering
            resources_.vegetation.treeLOD()->renderImpostors(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler()
            );
        }
    }
}

void HDRPassRecorder::recordDebugLines(VkCommandBuffer cmd) {
    if (!resources_.debugLine->hasLines()) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Set up viewport and scissor for debug rendering
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(resources_.postProcess->getExtent().width))
        .setHeight(static_cast<float>(resources_.postProcess->getExtent().height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    VkExtent2D debugExtent = resources_.postProcess->getExtent();
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{}.setWidth(debugExtent.width).setHeight(debugExtent.height));
    vkCmd.setScissor(0, scissor);

    // Need to get viewProj from the config (stored by Renderer)
    // For now, use identity if not set (could be improved by always passing viewProj)
    glm::mat4 viewProj = config_.lastViewProj ? *config_.lastViewProj : glm::mat4(1.0f);
    resources_.debugLine->recordCommands(cmd, viewProj);
}
