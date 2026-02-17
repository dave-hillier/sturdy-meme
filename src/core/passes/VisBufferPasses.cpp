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
#include "MaterialRegistry.h"
#include "MeshClusterBuilder.h"
#include "TwoPassCuller.h"

#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace VisBufferPasses {

// ============================================================================
// Persistent cluster state (survives across frames)
// ============================================================================

struct ClusterState {
    bool built = false;

    // Clustered mesh data (CPU-side, kept for reference)
    std::unordered_map<const Mesh*, ClusteredMesh> clusteredMeshes;

    uint32_t totalDrawCommands = 0;
};

// Static cluster state - persists across frames
static ClusterState s_clusterState;

// ============================================================================
// Build clusters from scene meshes (called once lazily)
// ============================================================================

static bool buildClusterState(RendererSystems& systems) {
    auto* visBuf = systems.visibilityBuffer();
    auto* clusterBuf = systems.gpuClusterBuffer();
    if (!visBuf || !clusterBuf) return false;

    const auto& sceneObjects = systems.scene().getRenderables();
    SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    bool hasCharacter = sceneBuilder.hasCharacter();
    const NPCSimulation* npcSim = sceneBuilder.getNPCSimulation();

    // Collect unique meshes from renderable scene objects (excluding player/NPCs)
    std::unordered_set<const Mesh*> meshSet;
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        if (hasCharacter && i == playerIndex) continue;
        if (npcSim) {
            bool isNPC = false;
            const auto& npcData = npcSim->getData();
            for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                if (i == npcData.renderableIndices[npcIdx]) {
                    isNPC = true;
                    break;
                }
            }
            if (isNPC) continue;
        }
        if (sceneObjects[i].mesh) {
            meshSet.insert(sceneObjects[i].mesh);
        }
    }

    if (meshSet.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VisBufferPasses: No meshes to cluster");
        return false;
    }

    // Build clusters for each unique mesh
    MeshClusterBuilder builder;
    builder.setTargetClusterSize(64);

    uint32_t meshId = 0;
    std::vector<std::pair<const Mesh*, const ClusteredMesh*>> meshClusterPairs;

    for (const Mesh* mesh : meshSet) {
        const auto& vertices = mesh->getVertices();
        const auto& indices = mesh->getIndices();

        if (vertices.empty() || indices.empty()) {
            meshId++;
            continue;
        }

        // Build clusters with DAG hierarchy for LOD
        ClusteredMesh clustered = builder.buildWithDAG(vertices, indices, meshId);

        SDL_Log("VisBufferPasses: Mesh %u clustered: %u clusters, %u triangles, %u DAG levels",
                meshId, clustered.totalClusters, clustered.totalTriangles, clustered.dagLevels);

        // Upload to GPUClusterBuffer
        uint32_t baseCluster = clusterBuf->uploadMesh(clustered);
        if (baseCluster == UINT32_MAX) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VisBufferPasses: Failed to upload mesh %u to GPUClusterBuffer", meshId);
            meshId++;
            continue;
        }

        s_clusterState.clusteredMeshes[mesh] = std::move(clustered);
        meshClusterPairs.push_back({mesh, &s_clusterState.clusteredMeshes[mesh]});
        meshId++;
    }

    if (meshClusterPairs.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VisBufferPasses: No meshes successfully clustered");
        return false;
    }

    // Build the packed vertex/index buffer for resolve from cluster data
    // This ensures triangleIds in the raster output match the resolve buffer
    visBuf->buildGlobalBuffersFromClusters(meshClusterPairs);

    // Count total draw commands (leaf clusters x instances)
    uint32_t totalDraws = 0;
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        if (hasCharacter && i == playerIndex) continue;
        if (npcSim) {
            bool isNPC = false;
            const auto& npcData = npcSim->getData();
            for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                if (i == npcData.renderableIndices[npcIdx]) {
                    isNPC = true;
                    break;
                }
            }
            if (isNPC) continue;
        }
        const auto& obj = sceneObjects[i];
        if (!obj.mesh) continue;
        auto it = s_clusterState.clusteredMeshes.find(obj.mesh);
        if (it == s_clusterState.clusteredMeshes.end()) continue;
        for (const auto& cluster : it->second.clusters) {
            if (cluster.lodLevel == 0) totalDraws++;
        }
    }

    s_clusterState.totalDrawCommands = totalDraws;
    s_clusterState.built = true;

    SDL_Log("VisBufferPasses: Cluster state built: %u draw commands from %zu meshes",
            totalDraws, meshClusterPairs.size());
    return true;
}

// ============================================================================
// Raster pass: Draw scene objects into the V-buffer
// ============================================================================

// ============================================================================
// Cull pass: Run TwoPassCuller compute to produce indirect draw commands
// ============================================================================

static void executeCullPass(FrameGraph::RenderContext& ctx, RendererSystems& systems) {
    RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
    if (!renderCtx) return;

    auto* culler = systems.twoPassCuller();
    if (!culler || !culler->hasDescriptorSets()) return;

    VkCommandBuffer cmd = renderCtx->cmd;
    uint32_t frameIndex = renderCtx->frameIndex;

    auto* clusterBuf = systems.gpuClusterBuffer();
    if (!clusterBuf) return;

    systems.profiler().beginGpuZone(cmd, "VisBufferCull");

    // Update culling uniforms
    glm::vec4 frustumPlanes[6];
    // Extract frustum planes from view-projection matrix
    glm::mat4 vp = renderCtx->frame.projection * renderCtx->frame.view;
    for (int i = 0; i < 3; ++i) {
        frustumPlanes[i * 2 + 0] = glm::vec4(
            vp[0][3] + vp[0][i], vp[1][3] + vp[1][i],
            vp[2][3] + vp[2][i], vp[3][3] + vp[3][i]);
        frustumPlanes[i * 2 + 1] = glm::vec4(
            vp[0][3] - vp[0][i], vp[1][3] - vp[1][i],
            vp[2][3] - vp[2][i], vp[3][3] - vp[3][i]);
    }
    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frustumPlanes[i]));
        if (len > 0.0f) frustumPlanes[i] /= len;
    }

    uint32_t instanceCount = systems.hasGPUSceneBuffer()
        ? systems.gpuSceneBuffer().getObjectCount() : 0;

    culler->updateUniforms(frameIndex,
        renderCtx->frame.view, renderCtx->frame.projection,
        renderCtx->frame.cameraPosition,
        frustumPlanes,
        clusterBuf->getTotalClusters(), instanceCount,
        renderCtx->frame.nearPlane, renderCtx->frame.farPlane, 0);

    // Run pass 1 (frustum cull previous frame's visible clusters)
    culler->recordPass1(cmd, frameIndex);

    systems.profiler().endGpuZone(cmd, "VisBufferCull");
}

// ============================================================================
// Raster pass: GPU-driven indirect draws from TwoPassCuller output
// ============================================================================

static void executeRasterPass(FrameGraph::RenderContext& ctx, RendererSystems& systems) {
    RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
    if (!renderCtx) return;

    auto* visBuf = systems.visibilityBuffer();
    if (!visBuf) return;

    VkCommandBuffer cmd = renderCtx->cmd;
    uint32_t frameIndex = renderCtx->frameIndex;

    systems.profiler().beginGpuZone(cmd, "VisBufferRaster");

    // Lazily build cluster state on first frame
    if (!s_clusterState.built && systems.hasGPUClusterBuffer()) {
        buildClusterState(systems);
    }

    // Lazily build material texture array and re-upload material indices
    if (!visBuf->hasTextureArray()) {
        const auto& registry = systems.scene().getSceneBuilder().getMaterialRegistry();
        if (visBuf->buildMaterialTextureArray(registry)) {
            if (systems.hasGPUMaterialBuffer()) {
                systems.gpuMaterialBuffer()->uploadFromRegistry(registry, *visBuf);
            }
        }
    }

    // Lazily wire TwoPassCuller with external buffers
    auto* culler = systems.twoPassCuller();
    auto* clusterBuf = systems.gpuClusterBuffer();
    if (culler && clusterBuf && !culler->hasDescriptorSets() && systems.hasGPUSceneBuffer()) {
        auto& sceneBuffer = systems.gpuSceneBuffer();
        uint32_t objCount = std::max(sceneBuffer.getObjectCount(), 1u);
        std::vector<VkBuffer> instanceBuffers;
        uint32_t framesInFlight = systems.globalBuffers().getFramesInFlight();
        for (uint32_t fi = 0; fi < framesInFlight; ++fi) {
            instanceBuffers.push_back(sceneBuffer.getInstanceBuffer(fi));
        }
        culler->setExternalBuffers(
            clusterBuf->getClusterBuffer(),
            clusterBuf->getTotalClusters() * sizeof(MeshCluster),
            instanceBuffers,
            objCount * sizeof(GPUSceneInstanceData));
    }

    // Lazily create raster descriptor sets (with DrawData and Instance SSBOs)
    if (!visBuf->hasRasterDescriptorSets()) {
        const auto& uboBuffers = systems.globalBuffers().getUniformBuffers();
        VkDeviceSize uboSize = systems.globalBuffers().getUniformBufferSize();

        std::vector<VkBuffer> drawDataBuffers;
        VkDeviceSize drawDataSize = 0;
        std::vector<VkBuffer> instanceBuffers;
        VkDeviceSize instanceSize = 0;

        if (culler && systems.hasGPUSceneBuffer()) {
            auto& sceneBuffer = systems.gpuSceneBuffer();
            uint32_t objCount = std::max(sceneBuffer.getObjectCount(), 1u);
            drawDataSize = culler->getDrawDataBufferSize();
            instanceSize = objCount * sizeof(GPUSceneInstanceData);

            uint32_t framesInFlight = systems.globalBuffers().getFramesInFlight();
            for (uint32_t fi = 0; fi < framesInFlight; ++fi) {
                drawDataBuffers.push_back(culler->getPass1DrawDataBuffer(fi));
                instanceBuffers.push_back(sceneBuffer.getInstanceBuffer(fi));
            }
        }

        visBuf->createRasterDescriptorSets(uboBuffers, uboSize,
            drawDataBuffers, drawDataSize, instanceBuffers, instanceSize);
    }

    if (!visBuf->hasRasterDescriptorSets() || !visBuf->hasGlobalBuffers()) {
        systems.profiler().endGpuZone(cmd, "VisBufferRaster");
        return;
    }

    if (!clusterBuf) {
        systems.profiler().endGpuZone(cmd, "VisBufferRaster");
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Begin V-buffer render pass
    VkExtent2D extent = visBuf->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0;
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

    // Bind raster descriptor set (UBO + texture + DrawData + Instances)
    VkDescriptorSet rasterDescSet = visBuf->getRasterDescriptorSet(frameIndex);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
                              0, vk::DescriptorSet(rasterDescSet), {});

    // Bind GPUClusterBuffer vertex/index buffers
    vk::Buffer vertexBuffers[] = {vk::Buffer(clusterBuf->getVertexBuffer())};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(vk::Buffer(clusterBuf->getIndexBuffer()), 0, vk::IndexType::eUint32);

    // GPU-driven indirect draw from TwoPassCuller pass 1 output
    if (culler && culler->hasDescriptorSets()) {
        if (culler->supportsDrawIndirectCount()) {
            // GPU-driven draw count: only draws commands actually written by cull shader
            vkCmdDrawIndexedIndirectCount(cmd,
                culler->getPass1IndirectBuffer(frameIndex), 0,
                culler->getPass1DrawCountBuffer(frameIndex), 0,
                culler->getMaxDrawCommands(),
                sizeof(VkDrawIndexedIndirectCommand));
        } else {
            // Fallback: draw all slots (unused slots zeroed by cull pass clear)
            vkCmdDrawIndexedIndirect(cmd,
                culler->getPass1IndirectBuffer(frameIndex), 0,
                culler->getMaxDrawCommands(),
                sizeof(VkDrawIndexedIndirectCommand));
        }
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

        // Material texture array (albedo textures)
        if (visBuf->hasTextureArray()) {
            resolveBuffers.textureArrayView = visBuf->getTextureArrayView();
            resolveBuffers.textureArraySampler = visBuf->getTextureArraySampler();
        }

        // HDR color image for layout transitions (resolve writes to it via imageStore)
        resolveBuffers.hdrColorImage = renderCtx->resources.hdrColorImage;

        // Dynamic light buffer for multi-light resolve
        const auto& lightBuffers = systems.globalBuffers().getLightBuffers();
        if (frameIndex < lightBuffers.size()) {
            resolveBuffers.lightBuffer = lightBuffers[frameIndex];
            resolveBuffers.lightBufferSize = systems.globalBuffers().getLightBufferSize();
        }

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

    // Cull pass: GPU compute culling to produce indirect draw commands
    if (systems.hasTwoPassCuller()) {
        ids.cull = graph.addPass({
            .name = "VisBufferCull",
            .execute = [&systems](FrameGraph::RenderContext& ctx) {
                executeCullPass(ctx, systems);
            },
            .canUseSecondary = false,
            .mainThreadOnly = true,
            .priority = 32  // Before raster (31)
        });
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
