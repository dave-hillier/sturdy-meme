#include "VisBufferPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "GPUSceneBuffer.h"
#include "VisibilityBuffer.h"
#include "GPUMaterialBuffer.h"

#include <algorithm>

namespace VisBufferPasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems) {
    PassIds ids;

    // Only add if visibility buffer system exists
    if (!systems.hasVisibilityBuffer()) {
        return ids;
    }

    ids.resolve = graph.addPass({
        .name = "VisBufferResolve",
        .execute = [&systems](FrameGraph::RenderContext& ctx) {
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

                // Instance buffer from GPUSceneBuffer
                if (systems.hasGPUSceneBuffer()) {
                    auto& sceneBuffer = systems.gpuSceneBuffer();
                    resolveBuffers.instanceBuffer = sceneBuffer.getInstanceBuffer(frameIndex);
                    // Ensure minimum size of 1 element for valid descriptor range
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
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 28  // After HDR (30), before post passes
    });

    return ids;
}

} // namespace VisBufferPasses
