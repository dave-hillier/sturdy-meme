#include "VegetationUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

#include "GrassSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "LeafSystem.h"
#include "WindSystem.h"
#include "ShadowSystem.h"
#include "GlobalBufferManager.h"

#include <cmath>

void VegetationUpdater::update(RendererSystems& systems, const FrameData& frame, VkExtent2D extent) {
    updateGrass(systems, frame);
    updateTreeDescriptors(systems, frame);
    updateTreeLOD(systems, frame, extent);
    updateLeaf(systems, frame);
}

void VegetationUpdater::updateGrass(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Grass");
    systems.grass().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj,
                                   frame.terrainSize, frame.heightScale, frame.time);
    systems.grass().updateDisplacementSources(frame.playerPosition, frame.playerCapsuleRadius, frame.deltaTime);
    systems.profiler().endCpuZone("Update:Grass");
}

void VegetationUpdater::updateTreeDescriptors(RendererSystems& systems, const FrameData& frame) {
    if (!systems.treeRenderer() || !systems.tree()) return;

    systems.profiler().beginCpuZone("Update:TreeDesc");
    VkDescriptorBufferInfo windInfo = systems.wind().getBufferInfo(frame.frameIndex);

    // Update descriptor sets for each bark texture type
    for (const auto& barkType : systems.tree()->getBarkTextureTypes()) {
        Texture* barkTex = systems.tree()->getBarkTexture(barkType);
        Texture* barkNormal = systems.tree()->getBarkNormalMap(barkType);

        systems.treeRenderer()->updateBarkDescriptorSet(
            frame.frameIndex,
            barkType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
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
            frame.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.tree()->getLeafInstanceBuffer(),
            systems.tree()->getLeafInstanceBufferSize(),
            systems.globalBuffers().snowBuffers.buffers[frame.frameIndex]);

        // Update culled leaf descriptor sets (for GPU culling path)
        systems.treeRenderer()->updateCulledLeafDescriptorSet(
            frame.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.globalBuffers().snowBuffers.buffers[frame.frameIndex]);
    }

    // Update instanced shadow descriptor sets with UBO for cascadeViewProj matrices
    systems.treeRenderer()->updateInstancedShadowDescriptorSets(
        frame.frameIndex,
        systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex]);

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
