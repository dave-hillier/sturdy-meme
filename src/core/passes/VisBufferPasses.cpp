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

static void executeRasterPass(FrameGraph::RenderContext& ctx, RendererSystems& systems) {
    RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
    if (!renderCtx) return;

    auto* visBuf = systems.visibilityBuffer();
    if (!visBuf) return;

    VkCommandBuffer cmd = renderCtx->cmd;
    uint32_t frameIndex = renderCtx->frameIndex;

    systems.profiler().beginGpuZone(cmd, "VisBufferRaster");

    // Lazily build cluster state if GPUClusterBuffer is available
    bool useClusters = false;
    if (systems.hasGPUClusterBuffer() && !s_clusterState.built) {
        buildClusterState(systems);
    }
    useClusters = s_clusterState.built;

    // Lazily build global vertex/index buffers on first frame (fallback path)
    if (!visBuf->hasGlobalBuffers() && !useClusters) {
        const auto& sceneObjects = systems.scene().getRenderables();

        std::unordered_set<const Mesh*> meshSet;
        for (const auto& obj : sceneObjects) {
            if (obj.mesh) meshSet.insert(obj.mesh);
        }
        std::vector<const Mesh*> uniqueMeshes(meshSet.begin(), meshSet.end());

        if (!uniqueMeshes.empty()) {
            visBuf->buildGlobalBuffers(uniqueMeshes);
        }
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

    // Lazily create raster descriptor sets
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

    // Bind raster descriptor set (UBO + placeholder texture)
    VkDescriptorSet rasterDescSet = visBuf->getRasterDescriptorSet(frameIndex);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
                              0, vk::DescriptorSet(rasterDescSet), {});

    // Iterate scene objects
    const auto& sceneObjects = systems.scene().getRenderables();
    SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    bool hasCharacter = sceneBuilder.hasCharacter();
    const NPCSimulation* npcSim = sceneBuilder.getNPCSimulation();

    uint32_t instanceId = 0;

    if (useClusters) {
        // Cluster-based raster: bind GPUClusterBuffer vertex/index once,
        // then draw each cluster per object with push constants.
        // Future: replace with vkCmdDrawIndexedIndirectCount via TwoPassCuller.
        auto* clusterBuf = systems.gpuClusterBuffer();

        vk::Buffer vertexBuffers[] = {vk::Buffer(clusterBuf->getVertexBuffer())};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(vk::Buffer(clusterBuf->getIndexBuffer()), 0, vk::IndexType::eUint32);

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
            if (!obj.mesh) {
                instanceId++;
                continue;
            }

            auto it = s_clusterState.clusteredMeshes.find(obj.mesh);
            if (it == s_clusterState.clusteredMeshes.end()) {
                instanceId++;
                continue;
            }

            const ClusteredMesh& clustered = it->second;
            const VisBufMeshInfo* meshInfo = visBuf->getMeshInfo(obj.mesh);
            if (!meshInfo) {
                instanceId++;
                continue;
            }

            // Draw each leaf cluster for this instance
            for (const auto& cluster : clustered.clusters) {
                if (cluster.lodLevel != 0) continue;

                VisBufPushConstants push{};
                push.model = obj.transform;
                push.instanceId = instanceId;
                push.triangleOffset = meshInfo->triangleOffset + cluster.firstIndex / 3;
                push.alphaTestThreshold = 0.0f;

                vkCmd.pushConstants<VisBufPushConstants>(
                    vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, push);

                vkCmd.drawIndexed(cluster.indexCount, 1,
                                  meshInfo->globalIndexOffset + cluster.firstIndex,
                                  static_cast<int32_t>(meshInfo->globalVertexOffset), 0);
            }

            instanceId++;
        }
    } else {
        // Fallback: per-object draws with original mesh vertex/index buffers
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
            if (!obj.mesh) {
                instanceId++;
                continue;
            }

            const VisBufMeshInfo* meshInfo = visBuf->getMeshInfo(obj.mesh);
            if (!meshInfo) {
                instanceId++;
                continue;
            }

            VisBufPushConstants push{};
            push.model = obj.transform;
            push.instanceId = instanceId;
            push.triangleOffset = meshInfo->triangleOffset;
            push.alphaTestThreshold = 0.0f;

            vkCmd.pushConstants<VisBufPushConstants>(
                vk::PipelineLayout(visBuf->getRasterPipelineLayout()),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, push);

            vk::Buffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
            vk::DeviceSize offsets[] = {0};
            vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
            vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

            vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);

            instanceId++;
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

        // HDR depth for depth comparison
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
