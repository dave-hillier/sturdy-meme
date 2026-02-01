#include "HDRPassRecorder.h"
#include "../RendererSystems.h"
#include "UBOs.h"  // For PushConstants (generated from shaders)
#include "../GPUSceneBuffer.h"  // For GPU-driven indirect rendering

// Subsystem includes
#include "PostProcessSystem.h"
#include "SkySystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "npc/NPCSimulation.h"
#include "npc/NPCRenderer.h"
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

void HDRPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const Params& params) {
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
    if (params.terrainEnabled) {
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
    if (params.sceneObjectsPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.sceneObjectsPipeline);
    }
    recordSceneObjects(cmd, frameIndex, params);
    resources_.profiler->endGpuZone(cmd, "HDR:SceneObjects");

    // Draw skinned characters with GPU skinning (player + NPCs)
    resources_.profiler->beginGpuZone(cmd, "HDR:SkinnedChar");
    {
        SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
        const auto& sceneObjects = sceneBuilder.getRenderables();

        // Draw player character (slot 0 is reserved for player)
        constexpr uint32_t PLAYER_BONE_SLOT = 0;
        if (sceneBuilder.hasCharacter()) {
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                resources_.skinnedMesh->record(cmd, frameIndex, PLAYER_BONE_SLOT, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }

        // Draw NPC characters via NPCRenderer (NPCs use slots 1+)
        if (resources_.npcRenderer) {
            if (auto* npcSim = sceneBuilder.getNPCSimulation()) {
                resources_.npcRenderer->prepare(frameIndex, *npcSim, sceneObjects);
                resources_.npcRenderer->recordDraw(cmd, frameIndex);
            }
        }
    }
    resources_.profiler->endGpuZone(cmd, "HDR:SkinnedChar");

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
    recordDebugLines(cmd, params.viewProj);

    vkCmd.endRenderPass();

    resources_.profiler->endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                            const std::vector<vk::CommandBuffer>& secondaries,
                                            const Params& params) {
    (void)params;  // Not used for secondary execution, only for recording
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

void HDRPassRecorder::recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                          uint32_t slot, const Params& params) {
    vk::CommandBuffer vkCmd(cmd);

    // Each slot records a group of draw calls to a secondary command buffer
    // The secondary buffer has already been begun with render pass inheritance

    switch (slot) {
    case 0:
        // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
        resources_.profiler->beginGpuZone(cmd, "HDR:Sky");
        resources_.sky->recordDraw(cmd, frameIndex);
        resources_.profiler->endGpuZone(cmd, "HDR:Sky");

        if (params.terrainEnabled) {
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
        if (params.sceneObjectsPipeline) {
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.sceneObjectsPipeline);
        }
        recordSceneObjects(cmd, frameIndex, params);
        resources_.profiler->endGpuZone(cmd, "HDR:SceneObjects");

        resources_.profiler->beginGpuZone(cmd, "HDR:SkinnedChar");
        {
            SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
            const auto& sceneObjects = sceneBuilder.getRenderables();

            // Draw player character (slot 0 is reserved for player)
            constexpr uint32_t PLAYER_BONE_SLOT = 0;
            if (sceneBuilder.hasCharacter()) {
                size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
                if (playerIndex < sceneObjects.size()) {
                    const Renderable& playerObj = sceneObjects[playerIndex];
                    resources_.skinnedMesh->record(cmd, frameIndex, PLAYER_BONE_SLOT, playerObj, sceneBuilder.getAnimatedCharacter());
                }
            }

            // Draw NPC characters via NPCRenderer (NPCs use slots 1+)
            if (resources_.npcRenderer) {
                if (auto* npcSim = sceneBuilder.getNPCSimulation()) {
                    resources_.npcRenderer->prepare(frameIndex, *npcSim, sceneObjects);
                    resources_.npcRenderer->recordDraw(cmd, frameIndex);
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

        recordDebugLines(cmd, params.viewProj);
        break;
    }
}

void HDRPassRecorder::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex, const Params& params) {
    if (!params.pipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "HDRPassRecorder: pipelineLayout not set");
        return;
    }

    // Use GPU-driven indirect rendering if enabled
    if (params.useIndirectDraw && params.gpuSceneBuffer && params.gpuSceneBuffer->getObjectCount() > 0) {
        recordSceneObjectsIndirect(cmd, frameIndex, params);
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
            *params.pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *params.pipelineLayout, 0, vk::DescriptorSet(descSet), {});

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

    // Get NPC simulation for skipping NPC renderables (rendered with GPU skinning)
    const NPCSimulation* npcSim = resources_.scene->getSceneBuilder().getNPCSimulation();

    for (size_t i : sortedIndices) {
        // Skip player character (rendered separately with GPU skinning)
        if (hasCharacter && i == playerIndex) {
            continue;
        }

        // Skip NPC characters (rendered separately with GPU skinning)
        bool isNPC = false;
        if (npcSim) {
            const auto& npcData = npcSim->getData();
            for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                if (i == npcData.renderableIndices[npcIdx]) {
                    isNPC = true;
                    break;
                }
            }
        }
        if (isNPC) {
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

void HDRPassRecorder::recordDebugLines(VkCommandBuffer cmd, const glm::mat4& viewProj) {
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

    resources_.debugLine->recordCommands(cmd, viewProj);
}

void HDRPassRecorder::recordSceneObjectsIndirect(VkCommandBuffer cmd, uint32_t frameIndex, const Params& params) {
    if (!params.gpuSceneBuffer || !params.instancedPipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "HDRPassRecorder: Indirect rendering requires gpuSceneBuffer and instancedPipelineLayout");
        return;
    }

    GPUSceneBuffer* sceneBuffer = params.gpuSceneBuffer;
    if (sceneBuffer->getObjectCount() == 0) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Bind instanced pipeline if provided
    if (params.instancedPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.instancedPipeline);
    }

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();
    const auto& sceneObjects = resources_.scene->getRenderables();

    // For indirect rendering, we need to:
    // 1. Bind the scene instance SSBO descriptor
    // 2. Bind vertex/index buffers per unique mesh
    // 3. Use vkCmdDrawIndexedIndirectCount to draw all visible instances

    // Since culling outputs draw commands sorted by object index (not by mesh),
    // and indirect draws need shared vertex/index buffers,
    // we use a simplified approach: draw all objects with one indirect call per mesh type

    // For now, fall back to regular rendering if indirect is requested but setup is incomplete
    // Full indirect rendering requires:
    // - A global vertex/index buffer with all meshes
    // - Indirect commands that reference offsets into the global buffer
    // - Material binding via SSBO instead of per-draw descriptor sets

    // TODO: Full implementation requires mesh batching infrastructure
    // For this phase, we demonstrate the indirect draw command structure

    // Bind first material's descriptor set (simplified - full implementation needs multi-material support)
    MaterialId firstMaterialId = INVALID_MATERIAL_ID;
    for (const auto& obj : sceneObjects) {
        if (obj.materialId != INVALID_MATERIAL_ID) {
            firstMaterialId = obj.materialId;
            break;
        }
    }

    if (firstMaterialId != INVALID_MATERIAL_ID) {
        VkDescriptorSet descSet = materialRegistry.getDescriptorSet(firstMaterialId, frameIndex);
        if (descSet != VK_NULL_HANDLE) {
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *params.instancedPipelineLayout, 0, vk::DescriptorSet(descSet), {});
        }
    }

    // For demonstration, render each unique mesh with indirect count
    // A full implementation would batch objects by mesh and issue one indirect call per batch

    // Get unique meshes
    std::vector<const Mesh*> uniqueMeshes;
    for (const auto& obj : sceneObjects) {
        if (obj.mesh && std::find(uniqueMeshes.begin(), uniqueMeshes.end(), obj.mesh) == uniqueMeshes.end()) {
            uniqueMeshes.push_back(obj.mesh);
        }
    }

    // Draw each mesh type
    for (const Mesh* mesh : uniqueMeshes) {
        if (!mesh) continue;

        vk::Buffer vertexBuffers[] = {mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        // Use vkCmdDrawIndexedIndirectCount for GPU-driven variable draw count
        // The draw count is determined by the culling pass
        vkCmd.drawIndexedIndirectCount(
            sceneBuffer->getIndirectBuffer(frameIndex),  // Indirect command buffer
            0,                                            // Offset to commands
            sceneBuffer->getDrawCountBuffer(frameIndex), // Count buffer
            0,                                            // Offset to count
            sceneBuffer->getObjectCount(),               // Max draw count
            sizeof(GPUDrawIndexedIndirectCommand)        // Stride between commands
        );
    }

    // Note: Trees, rocks, and other subsystems still use their own rendering paths
    // Full GPU-driven rendering would consolidate these into the scene buffer
}

// ============================================================================
// Legacy API implementations (deprecated)
// ============================================================================

void HDRPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    Params params;
    params.terrainEnabled = legacyConfig_.terrainEnabled;
    params.sceneObjectsPipeline = legacyConfig_.sceneObjectsPipeline;
    params.pipelineLayout = legacyConfig_.pipelineLayout;
    if (legacyConfig_.lastViewProj) {
        params.viewProj = *legacyConfig_.lastViewProj;
    }
    record(cmd, frameIndex, time, params);
}

void HDRPassRecorder::recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    Params params;
    params.terrainEnabled = legacyConfig_.terrainEnabled;
    params.sceneObjectsPipeline = legacyConfig_.sceneObjectsPipeline;
    params.pipelineLayout = legacyConfig_.pipelineLayout;
    if (legacyConfig_.lastViewProj) {
        params.viewProj = *legacyConfig_.lastViewProj;
    }
    recordWithSecondaries(cmd, frameIndex, time, secondaries, params);
}

void HDRPassRecorder::recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot) {
    Params params;
    params.terrainEnabled = legacyConfig_.terrainEnabled;
    params.sceneObjectsPipeline = legacyConfig_.sceneObjectsPipeline;
    params.pipelineLayout = legacyConfig_.pipelineLayout;
    if (legacyConfig_.lastViewProj) {
        params.viewProj = *legacyConfig_.lastViewProj;
    }
    recordSecondarySlot(cmd, frameIndex, time, slot, params);
}
