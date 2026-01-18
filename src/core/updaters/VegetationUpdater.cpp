#include "VegetationUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

#include "DisplacementSystem.h"
#include "GrassSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "LeafSystem.h"
#include "WindSystem.h"
#include "ShadowSystem.h"
#include "CloudShadowSystem.h"
#include "GlobalBufferManager.h"
#include "VegetationRenderContext.h"
#include "DeferredTerrainObjects.h"
#include "TerrainSystem.h"
#include "ScatterSystem.h"

#include <cmath>

void VegetationUpdater::update(RendererSystems& systems, const FrameData& frame, VkExtent2D extent) {
    // Try to generate deferred terrain objects (trees, detritus) if not yet done
    tryGenerateDeferredObjects(systems);

    // Build shared context once per frame
    auto ctx = VegetationRenderContext::fromSystems(systems, frame);

    updateGrass(systems, frame, ctx);
    updateTreeDescriptors(systems, frame, ctx);
    updateTreeLOD(systems, frame, extent);
    updateLeaf(systems, frame);
}

void VegetationUpdater::tryGenerateDeferredObjects(RendererSystems& systems) {
    auto* deferred = systems.deferredTerrainObjects();
    if (!deferred || deferred->isGenerated()) {
        return;  // No deferred loader or already generated
    }

    systems.profiler().beginCpuZone("DeferredTerrainGen");

    // Check if terrain is ready (has base LOD tiles loaded)
    // The terrain system loads base LOD tiles synchronously at startup,
    // so this should be true on the first frame
    bool terrainReady = true;  // Terrain base tiles are loaded during init

    // Get a reference to detritus unique_ptr for the output
    std::unique_ptr<ScatterSystem> detritusSystem;

    // Try to generate the deferred content
    bool generated = deferred->tryGenerate(
        systems.tree(),
        systems.treeLOD(),
        systems.impostorCull(),
        systems.treeRenderer(),
        &systems.rocks(),
        detritusSystem,
        terrainReady
    );

    // If detritus was created, set it on the systems
    if (generated && detritusSystem) {
        systems.setDetritus(std::move(detritusSystem));
    }

    systems.profiler().endCpuZone("DeferredTerrainGen");
}

void VegetationUpdater::updateGrass(RendererSystems& systems, const FrameData& frame,
                                     const VegetationRenderContext& ctx) {
    systems.profiler().beginCpuZone("Update:Grass");

    // Update displacement system (shared by grass and leaves)
    systems.displacement().updateRegionCenter(ctx.cameraPosition);
    systems.displacement().updateSources(frame.playerPosition, frame.playerCapsuleRadius, ctx.deltaTime);

    // Update grass uniforms using context data
    systems.grass().updateUniforms(ctx.frameIndex, ctx.cameraPosition, ctx.viewProjectionMatrix,
                                   ctx.terrainSize, ctx.terrainHeightScale, ctx.time);
    systems.profiler().endCpuZone("Update:Grass");
}

void VegetationUpdater::updateTreeDescriptors(RendererSystems& systems, const FrameData& frame,
                                               const VegetationRenderContext& ctx) {
    if (!systems.treeRenderer() || !systems.tree()) return;

    systems.profiler().beginCpuZone("Update:TreeDesc");

    // Use wind UBO from context instead of querying each time
    VkBuffer windBuffer = static_cast<VkBuffer>(ctx.windUBO);
    VkImageView shadowView = static_cast<VkImageView>(ctx.shadowMapView);
    VkSampler shadowSampler = static_cast<VkSampler>(ctx.shadowMapSampler);

    // Update descriptor sets for each bark texture type
    for (const auto& barkType : systems.tree()->getBarkTextureTypes()) {
        Texture* barkTex = systems.tree()->getBarkTexture(barkType);
        Texture* barkNormal = systems.tree()->getBarkNormalMap(barkType);

        systems.treeRenderer()->updateBarkDescriptorSet(
            ctx.frameIndex,
            barkType,
            systems.globalBuffers().uniformBuffers.buffers[ctx.frameIndex],
            windBuffer,
            shadowView,
            shadowSampler,
            barkTex->getImageView(),
            barkNormal->getImageView(),
            barkTex->getImageView(),  // roughness placeholder
            barkTex->getImageView(),  // AO placeholder
            barkTex->getSampler());
    }

    // Update descriptor sets for each leaf texture type
    for (const auto& leafType : systems.tree()->getLeafTextureTypes()) {
        Texture* leafTex = systems.tree()->getLeafTexture(leafType);

        systems.treeRenderer()->updateLeafDescriptorSet(
            ctx.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[ctx.frameIndex],
            windBuffer,
            shadowView,
            shadowSampler,
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.tree()->getLeafInstanceBuffer(),
            systems.tree()->getLeafInstanceBufferSize(),
            systems.globalBuffers().snowBuffers.buffers[ctx.frameIndex]);

        // Update culled leaf descriptor sets (for GPU culling path)
        systems.treeRenderer()->updateCulledLeafDescriptorSet(
            ctx.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[ctx.frameIndex],
            windBuffer,
            shadowView,
            shadowSampler,
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.globalBuffers().snowBuffers.buffers[ctx.frameIndex]);
    }

    // Update instanced shadow descriptor sets with UBO for cascadeViewProj matrices
    systems.treeRenderer()->updateInstancedShadowDescriptorSets(
        ctx.frameIndex,
        systems.globalBuffers().uniformBuffers.buffers[ctx.frameIndex]);

    systems.profiler().endCpuZone("Update:TreeDesc");
}

void VegetationUpdater::updateTreeLOD(RendererSystems& systems, const FrameData& frame, VkExtent2D extent) {
    if (!systems.treeLOD() || !systems.tree()) return;

    systems.profiler().beginCpuZone("Update:TreeLOD");

    // Enable GPU culling optimization when ImpostorCullSystem is available
    auto* impostorCull = systems.impostorCull();
    bool gpuCullingAvailable = impostorCull && impostorCull->getTreeCount() > 0;
    systems.treeLOD()->setGPUCullingEnabled(gpuCullingAvailable);

    // Compute screen params for screen-space error LOD
    TreeLODSystem::ScreenParams screenParams;
    screenParams.screenHeight = static_cast<float>(extent.height);
    // Extract tanHalfFOV from projection matrix: proj[1][1] = 1/tan(fov/2)
    // Note: Vulkan Y-flip makes proj[1][1] negative, so use abs()
    screenParams.tanHalfFOV = 1.0f / std::abs(frame.projection[1][1]);
    systems.treeLOD()->update(frame.deltaTime, frame.cameraPosition, *systems.tree(), screenParams);

    systems.profiler().endCpuZone("Update:TreeLOD");
}

void VegetationUpdater::updateLeaf(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Leaf");
    systems.leaf().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj,
                                  frame.cameraPosition, frame.playerVelocity, frame.deltaTime, frame.time,
                                  frame.terrainSize, frame.heightScale);
    systems.profiler().endCpuZone("Update:Leaf");
}
