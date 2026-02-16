#include "VisBufferPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "GPUSceneBuffer.h"
#include "VisibilityBuffer.h"
#include "GPUMaterialBuffer.h"
#include "GlobalBufferManager.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "npc/NPCSimulation.h"
#include "Mesh.h"

#include <algorithm>
#include <unordered_set>

namespace VisBufferPasses {

// ============================================================================
// Raster pass: Draw scene objects into the V-buffer
// ============================================================================

static void executeRasterPass(FrameGraph::RenderContext& ctx, RendererSystems& systems) {
    RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
    if (!renderCtx) return;

    auto* visBuf = systems.visibilityBuffer();
    if (!visBuf) return;

    VkCommandBuffer cmd = renderCtx->cmd;
    uint32_t frameIndex = renderCtx->frameIndex;

    systems.profiler().beginGpuZone(cmd, "VisBufferRaster");

    // Lazily build global vertex/index buffers on first frame
    if (!visBuf->hasGlobalBuffers()) {
        const auto& sceneObjects = systems.scene().getRenderables();

        // Collect unique meshes
        std::unordered_set<const Mesh*> meshSet;
        for (const auto& obj : sceneObjects) {
            if (obj.mesh) meshSet.insert(obj.mesh);
        }
        std::vector<const Mesh*> uniqueMeshes(meshSet.begin(), meshSet.end());

        if (!uniqueMeshes.empty()) {
            visBuf->buildGlobalBuffers(uniqueMeshes);
        }
    }

    // Lazily create raster descriptor sets (need UBO buffers from GlobalBufferManager)
    if (!visBuf->hasRasterDescriptorSets()) {
        const auto& uboBuffers = systems.globalBuffers().getUniformBuffers();
        VkDeviceSize uboSize = systems.globalBuffers().getUniformBufferSize();
        visBuf->createRasterDescriptorSets(uboBuffers, uboSize);
    }

    if (!visBuf->hasRasterDescriptorSets() || !visBuf->hasGlobalBuffers()) {
        systems.profiler().endGpuZone(cmd, "VisBufferRaster");
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Begin V-buffer render pass
    VkExtent2D extent = visBuf->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0;  // V-buffer clear: 0 = no geometry
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBeginInfo{};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = visBuf->getRenderPass();
    rpBeginInfo.framebuffer = visBuf->getFramebuffer();
    rpBeginInfo.renderArea.offset = {0, 0};
    rpBeginInfo.renderArea.extent = extent;
    rpBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBeginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind raster pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                        vk::Pipeline(visBuf->getRasterPipeline()));

    // Set dynamic viewport and scissor
    vk::Viewport viewport{0.0f, 0.0f,
                           static_cast<float>(extent.width),
                           static_cast<float>(extent.height),
                           0.0f, 1.0f};
    vkCmd.setViewport(0, viewport);

    vk::Rect2D scissor{{0, 0}, {extent.width, extent.height}};
    vkCmd.setScissor(0, scissor);

    // Bind raster descriptor set (UBO + placeholder texture)
    VkDescriptorSet rasterDescSet = visBuf->getRasterDescriptorSet(frameIndex);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
                              0, vk::DescriptorSet(rasterDescSet), {});

    // Iterate scene objects (same order as GPUSceneBuffer population in FrameUpdater)
    const auto& sceneObjects = systems.scene().getRenderables();
    SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    bool hasCharacter = sceneBuilder.hasCharacter();
    const NPCSimulation* npcSim = sceneBuilder.getNPCSimulation();

    uint32_t instanceId = 0;
    uint32_t rasterizedCount = 0;

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character (rendered with GPU skinning)
        if (hasCharacter && i == playerIndex) continue;

        // Skip NPC characters (rendered with GPU skinning)
        if (npcSim) {
            bool isNPC = false;
            const auto& npcData = npcSim->getData();
            for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                if (i == npcData.renderableIndices[npcIdx]) {
                    isNPC = true;
                    break;
                }
            }
            if (isNPC) {
                continue;
            }
        }

        const auto& obj = sceneObjects[i];
        if (!obj.mesh) {
            instanceId++;
            continue;
        }

        // Look up mesh info for triangle offset
        const VisBufMeshInfo* meshInfo = visBuf->getMeshInfo(obj.mesh);
        if (!meshInfo) {
            instanceId++;
            continue;
        }

        // Push V-buffer constants
        VisBufPushConstants push{};
        push.model = obj.transform;
        push.instanceId = instanceId;
        push.triangleOffset = meshInfo->triangleOffset;
        push.alphaTestThreshold = 0.0f;  // Skip alpha testing for now (all opaque)

        vkCmd.pushConstants<VisBufPushConstants>(
            vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        // Bind per-mesh vertex/index buffers
        vk::Buffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        // Draw
        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);

        instanceId++;
        rasterizedCount++;
    }

    vkCmdEndRenderPass(cmd);

    systems.profiler().endGpuZone(cmd, "VisBufferRaster");
}

// ============================================================================
// Resolve pass: Compute shader material evaluation
// ============================================================================

static void executeResolvePass(FrameGraph::RenderContext& ctx, RendererSystems& systems) {
    RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
    if (!renderCtx) return;

    auto* visBuf = systems.visibilityBuffer();
    if (!visBuf) return;

    VkCommandBuffer cmd = renderCtx->cmd;
    uint32_t frameIndex = renderCtx->frameIndex;

    systems.profiler().beginGpuZone(cmd, "VisBufferResolve");

    // Bind external buffers to the resolve pass
    {
        VisibilityBuffer::ResolveBuffers resolveBuffers{};

        // Global vertex/index buffers from V-buffer system
        if (visBuf->hasGlobalBuffers()) {
            resolveBuffers.vertexBuffer = visBuf->getGlobalVertexBuffer();
            resolveBuffers.vertexBufferSize = visBuf->getGlobalVertexBufferSize();
            resolveBuffers.indexBuffer = visBuf->getGlobalIndexBuffer();
            resolveBuffers.indexBufferSize = visBuf->getGlobalIndexBufferSize();
        }

        // Instance buffer from GPUSceneBuffer
        if (systems.hasGPUSceneBuffer()) {
            auto& sceneBuffer = systems.gpuSceneBuffer();
            resolveBuffers.instanceBuffer = sceneBuffer.getInstanceBuffer(frameIndex);
            uint32_t count = std::max(sceneBuffer.getObjectCount(), 1u);
            resolveBuffers.instanceBufferSize = count * sizeof(GPUSceneInstanceData);
        }

        // Material buffer from GPUMaterialBuffer
        if (systems.hasGPUMaterialBuffer()) {
            auto* matBuf = systems.gpuMaterialBuffer();
            resolveBuffers.materialBuffer = matBuf->getBuffer();
            resolveBuffers.materialBufferSize = matBuf->getBufferSize();
            resolveBuffers.materialCount = matBuf->getMaterialCount();
        }

        // HDR depth for depth comparison (prevents overwriting closer HDR-pass objects)
        resolveBuffers.hdrDepthView = renderCtx->resources.hdrDepthView;
        resolveBuffers.hdrDepthImage = renderCtx->resources.hdrDepthImage;

        visBuf->setResolveBuffers(resolveBuffers);
    }

    // Update resolve uniforms
    visBuf->updateResolveUniforms(
        frameIndex,
        renderCtx->frame.view,
        renderCtx->frame.projection,
        renderCtx->frame.cameraPosition,
        renderCtx->frame.sunDirection,
        renderCtx->frame.sunIntensity,
        systems.hasGPUMaterialBuffer() ? systems.gpuMaterialBuffer()->getMaterialCount() : 0
    );

    // Dispatch the resolve compute shader
    VkImageView hdrView = renderCtx->resources.hdrColorView;
    visBuf->recordResolvePass(cmd, frameIndex, hdrView);

    systems.profiler().endGpuZone(cmd, "VisBufferResolve");
}

// ============================================================================
// Pass registration
// ============================================================================

PassIds addPasses(FrameGraph& graph, RendererSystems& systems) {
    PassIds ids;

    // Only add if visibility buffer system exists
    if (!systems.hasVisibilityBuffer()) {
        return ids;
    }

    // Raster pass: draw scene objects into V-buffer
    ids.raster = graph.addPass({
        .name = "VisBufferRaster",
        .execute = [&systems](FrameGraph::RenderContext& ctx) {
            executeRasterPass(ctx, systems);
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 31  // Higher than HDR (30) - runs first at same dependency level
    });

    // Resolve pass: compute shader material evaluation
    ids.resolve = graph.addPass({
        .name = "VisBufferResolve",
        .execute = [&systems](FrameGraph::RenderContext& ctx) {
            executeResolvePass(ctx, systems);
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 28  // After HDR (30), before post passes
    });

    return ids;
}

} // namespace VisBufferPasses
