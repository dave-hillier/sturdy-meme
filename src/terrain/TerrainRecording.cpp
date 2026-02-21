#include "TerrainSystem.h"
#include "TerrainBuffers.h"
#include "TerrainCameraOptimizer.h"
#include "TerrainEffects.h"
#include "DescriptorManager.h"
#include "GpuProfiler.h"
#include "QueueSubmitDiagnostics.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>

void TerrainSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                    const glm::mat4& view, const glm::mat4& proj,
                                    const std::array<glm::vec4, 3>& snowCascadeParams,
                                    bool useVolumetricSnow,
                                    float snowMaxHeight) {
    // Track camera movement for skip-frame optimization
    cameraOptimizer.update(cameraPos, view);

    // Update tile cache - stream high-res tiles based on camera position
    // Set frame index first so tile info buffer writes to the correct triple-buffered slot
    if (tileCache) {
        tileCache->setCurrentFrameIndex(frameIndex);
        tileCache->updateActiveTiles(cameraPos, config.tileLoadRadius, config.tileUnloadRadius);
    }

    TerrainUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    uniforms.terrainParams = glm::vec4(
        config.size,
        config.heightScale,
        config.targetEdgePixels,
        static_cast<float>(config.maxDepth)
    );

    uniforms.lodParams = glm::vec4(
        config.splitThreshold,
        config.mergeThreshold,
        static_cast<float>(config.minDepth),
        static_cast<float>(subdivisionFrameCount & 1)  // 0 = split phase, 1 = merge phase
    );

    uniforms.screenSize = glm::vec2(extent.width, extent.height);

    // Compute LOD factor for screen-space edge length calculation
    float fov = 2.0f * atan(1.0f / proj[1][1]);
    uniforms.lodFactor = 2.0f * log2(extent.height / (2.0f * tan(fov * 0.5f) * config.targetEdgePixels));
    uniforms.padding = config.flatnessScale;  // flatnessScale in shader

    // Extract frustum planes
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    // Volumetric snow parameters
    uniforms.snowCascade0Params = snowCascadeParams[0];
    uniforms.snowCascade1Params = snowCascadeParams[1];
    uniforms.snowCascade2Params = snowCascadeParams[2];
    uniforms.useVolumetricSnow = useVolumetricSnow ? 1.0f : 0.0f;
    uniforms.snowMaxHeight = snowMaxHeight;
    uniforms.snowPadding1 = 0.0f;
    uniforms.snowPadding2 = 0.0f;

    memcpy(buffers->getUniformMappedPtr(frameIndex), &uniforms, sizeof(TerrainUniforms));

    // Update visual effects (caustics animation, liquid animation)
    constexpr float FRAME_DELTA_TIME = 0.0167f;  // ~60fps
    effects.updatePerFrame(frameIndex, FRAME_DELTA_TIME, buffers.get());
}

void TerrainSystem::recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler) {
    // Update tile info buffer binding to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device, computeDescriptorSets[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    // Record pending meshlet uploads (fence-free, like virtual texture system)
    if (config.useMeshlets && meshlet && meshlet->hasPendingUpload()) {
        meshlet->recordUpload(cmd, frameIndex);
    }

    // Skip-frame optimization: skip compute when camera is stationary and terrain has converged
    if (cameraOptimizer.shouldSkipCompute()) {
        cameraOptimizer.recordComputeSkipped();

        // Still need the final barrier for rendering (CBT state unchanged but GPU needs it)
        vk::CommandBuffer vkCmd(cmd);
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eVertexAttributeRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexInput,
                              {}, barrier, {}, {});
        return;
    }

    // Reset skip tracking
    cameraOptimizer.recordComputeExecuted();

    vk::CommandBuffer vkCmd = cmd;

    // 1. Dispatcher - set up indirect args
    if (profiler) profiler->beginZone(cmd, "Terrain:Dispatcher");

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getDispatcherPipeline());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getDispatcherPipelineLayout(),
                             0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletIndexCount = config.useMeshlets ? meshlet->getIndexCount() : 0;
    vkCmd.pushConstants<TerrainDispatcherPushConstants>(
        pipelines->getDispatcherPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, dispatcherPC);

    vkCmd.dispatch(1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:Dispatcher");

    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                              {}, barrier, {}, {});
    }

    // 2. Subdivision - LOD update with inline frustum culling
    // Ping-pong between split and merge to avoid race conditions
    // Even frames: split only (0), Odd frames: merge only (1)
    if (profiler) profiler->beginZone(cmd, "Terrain:Subdivision");

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getSubdivisionPipeline());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getSubdivisionPipelineLayout(),
                             0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    TerrainSubdivisionPushConstants subdivPC{};
    subdivPC.updateMode = subdivisionFrameCount & 1;  // 0 = split, 1 = merge
    subdivPC.frameIndex = subdivisionFrameCount;
    subdivPC.spreadFactor = config.spreadFactor;
    subdivPC.reserved = 0;
    vkCmd.pushConstants<TerrainSubdivisionPushConstants>(
        pipelines->getSubdivisionPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, subdivPC);

    vkCmd.dispatchIndirect(buffers->getIndirectDispatchBuffer(), 0);

    if (profiler) profiler->endZone(cmd, "Terrain:Subdivision");

    subdivisionFrameCount++;

    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                              {}, barrier, {}, {});
    }

    // 3. Sum reduction - rebuild the sum tree
    // Choose optimized or fallback path based on subgroup support
    if (profiler) profiler->beginZone(cmd, "Terrain:SumReductionPrepass");

    TerrainSumReductionPushConstants sumPC{};
    sumPC.passID = config.maxDepth;

    int levelsFromPrepass;

    if (pipelines->getSumReductionPrepassSubgroupPipeline()) {
        // Subgroup prepass - processes 13 levels:
        // - SWAR popcount: 5 levels (32 bits -> 6-bit sum)
        // - Subgroup shuffle: 5 levels (32 threads -> 11-bit sum)
        // - Shared memory: 3 levels (8 subgroups -> 14-bit sum)
        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPrepassSubgroupPipeline());
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPipelineLayout(),
                                 0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

        vkCmd.pushConstants<TerrainSumReductionPushConstants>(
            pipelines->getSumReductionPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0, sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmd.dispatch(workgroups, 1, 1);

        {
            auto barrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, barrier, {}, {});
        }

        levelsFromPrepass = 13;  // SWAR (5) + subgroup (5) + shared memory (3)
    } else {
        // Fallback path: standard prepass handles 5 levels
        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPrepassPipeline());
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPipelineLayout(),
                                 0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

        vkCmd.pushConstants<TerrainSumReductionPushConstants>(
            pipelines->getSumReductionPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0, sumPC);

        uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmd.dispatch(workgroups, 1, 1);

        {
            auto barrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, barrier, {}, {});
        }

        levelsFromPrepass = 5;
    }

    if (profiler) profiler->endZone(cmd, "Terrain:SumReductionPrepass");

    // Phase 2: Standard sum reduction for remaining levels (one dispatch per level)
    // Start from level (maxDepth - levelsFromPrepass - 1) down to 0
    int startDepth = config.maxDepth - levelsFromPrepass - 1;
    if (startDepth >= 0) {
        if (profiler) profiler->beginZone(cmd, "Terrain:SumReductionLevels");

        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPipeline());
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getSumReductionPipelineLayout(),
                                 0, vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

        for (int depth = startDepth; depth >= 0; --depth) {
            sumPC.passID = depth;
            vkCmd.pushConstants<TerrainSumReductionPushConstants>(
                pipelines->getSumReductionPipelineLayout(),
                vk::ShaderStageFlagBits::eCompute,
                0, sumPC);

            uint32_t workgroups = std::max(1u, (1u << depth) / SUM_REDUCTION_WORKGROUP_SIZE);
            vkCmd.dispatch(workgroups, 1, 1);

            {
                auto barrier = vk::MemoryBarrier{}
                    .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                      {}, barrier, {}, {});
            }
        }

        if (profiler) profiler->endZone(cmd, "Terrain:SumReductionLevels");
    }

    // 4. Final dispatcher pass to update draw args
    if (profiler) profiler->beginZone(cmd, "Terrain:FinalDispatch");

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getDispatcherPipeline());
    vkCmd.pushConstants<TerrainDispatcherPushConstants>(
        pipelines->getDispatcherPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, dispatcherPC);
    vkCmd.dispatch(1, 1, 1);

    if (profiler) profiler->endZone(cmd, "Terrain:FinalDispatch");

    // Final barrier before rendering
    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eVertexAttributeRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexInput,
                              {}, barrier, {}, {});
    }
}

void TerrainSystem::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Update tile info buffer binding to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileCache && tileCache->getTileInfoBuffer(frameIndex) != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device, renderDescriptorSets[frameIndex])
            .writeBuffer(20, tileCache->getTileInfoBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    vk::CommandBuffer vkCmd = cmd;

    vk::Pipeline pipeline;
    if (config.useMeshlets) {
        pipeline = wireframeMode ? pipelines->getMeshletWireframePipeline() : pipelines->getMeshletRenderPipeline();
    } else {
        pipeline = wireframeMode ? pipelines->getWireframePipeline() : pipelines->getRenderPipeline();
    }
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelines->getRenderPipelineLayout(),
                             0, renderDescriptorSets[frameIndex], {});

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({extent.width, extent.height});
    vkCmd.setScissor(0, scissor);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        vk::Buffer vertexBuffers[] = {meshlet->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(meshlet->getIndexBuffer(), 0, vk::IndexType::eUint16);

        // Indexed instanced draw
        vkCmd.drawIndexedIndirect(buffers->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
        DIAG_RECORD_DRAW();
    } else {
        // Direct vertex draw (no vertex buffer - vertices generated from gl_VertexIndex)
        vkCmd.drawIndirect(buffers->getIndirectDrawBuffer(), 0, 1, sizeof(VkDrawIndirectCommand));
        DIAG_RECORD_DRAW();
    }
}

void TerrainSystem::recordShadowCull(vk::CommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!shadowCullingEnabled || !pipelines->hasShadowCulling()) {
        return;
    }

    vk::CommandBuffer vkCmd = cmd;

    // Clear the shadow visible count to 0 and barrier for compute
    vkCmd.fillBuffer(buffers->getShadowVisibleBuffer(), 0, sizeof(uint32_t), 0);
    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eComputeShader,
                              {}, barrier, {}, {});
    }

    // Bind shadow cull compute pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines->getShadowCullPipeline());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelines->getShadowCullPipelineLayout(),
                             0, computeDescriptorSets[frameIndex], {});

    // Set up push constants with frustum planes
    TerrainShadowCullPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    extractFrustumPlanes(lightViewProj, pc.lightFrustumPlanes);
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = static_cast<uint32_t>(cascadeIndex);
    // Note: For indirect dispatch, we pass upper bound since actual workgroup count
    // is GPU-computed. The shader's "last workgroup" detection uses this.
    pc.totalWorkgroups = (MAX_VISIBLE_TRIANGLES + SHADOW_CULL_WORKGROUP_SIZE - 1) / SHADOW_CULL_WORKGROUP_SIZE;
    pc.maxShadowIndices = MAX_VISIBLE_TRIANGLES;
    pc._pad0 = 0;

    vkCmd.pushConstants<TerrainShadowCullPushConstants>(
        pipelines->getShadowCullPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, pc);

    // Use indirect dispatch - the workgroup count is computed on GPU in terrain_dispatcher
    vkCmd.dispatchIndirect(buffers->getIndirectDispatchBuffer(), 0);

    // Memory barrier to ensure shadow cull results are visible for draw
    {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eVertexAttributeRead);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexInput,
                              {}, barrier, {}, {});
    }
}

void TerrainSystem::recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    vk::CommandBuffer vkCmd = cmd;

    // Choose pipeline: culled vs non-culled, meshlet vs direct
    vk::Pipeline pipeline;
    bool useCulled = shadowCullingEnabled && pipelines->getShadowCulledPipeline() != VK_NULL_HANDLE;

    if (config.useMeshlets) {
        pipeline = useCulled ? pipelines->getMeshletShadowCulledPipeline() : pipelines->getMeshletShadowPipeline();
    } else {
        pipeline = useCulled ? pipelines->getShadowCulledPipeline() : pipelines->getShadowPipeline();
    }

    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelines->getShadowPipelineLayout(),
                             0, renderDescriptorSets[frameIndex], {});

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(shadowMapSize))
        .setHeight(static_cast<float>(shadowMapSize))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({shadowMapSize, shadowMapSize});
    vkCmd.setScissor(0, scissor);

    vkCmd.setDepthBias(1.25f, 0.0f, 1.75f);

    TerrainShadowPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = cascadeIndex;
    vkCmd.pushConstants<TerrainShadowPushConstants>(
        pipelines->getShadowPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex,
        0, pc);

    if (config.useMeshlets) {
        // Bind meshlet vertex and index buffers
        vk::Buffer vertexBuffers[] = {meshlet->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(meshlet->getIndexBuffer(), 0, vk::IndexType::eUint16);

        // Use shadow indirect draw buffer if culling, else main indirect buffer
        VkBuffer drawBuffer = useCulled ? buffers->getShadowIndirectDrawBuffer() : buffers->getIndirectDrawBuffer();
        vkCmd.drawIndexedIndirect(drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
        DIAG_RECORD_DRAW();
    } else {
        VkBuffer drawBuffer = useCulled ? buffers->getShadowIndirectDrawBuffer() : buffers->getIndirectDrawBuffer();
        vkCmd.drawIndirect(drawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
        DIAG_RECORD_DRAW();
    }
}
