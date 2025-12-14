#include "TerrainPipelines.h"
#include "TerrainSystem.h"  // For push constant structs and SubgroupCapabilities
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>

using ShaderLoader::loadShaderModule;

bool TerrainPipelines::init(const InitInfo& info) {
    device = info.device;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    computeDescriptorSetLayout = info.computeDescriptorSetLayout;
    renderDescriptorSetLayout = info.renderDescriptorSetLayout;
    shaderPath = info.shaderPath;
    useMeshlets = info.useMeshlets;
    meshletIndexCount = info.meshletIndexCount;
    subgroupCaps = info.subgroupCaps;

    if (!createDispatcherPipeline()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createSumReductionPipelines()) return false;
    if (!createFrustumCullPipelines()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;
    if (!createShadowPipeline()) return false;

    if (useMeshlets) {
        if (!createMeshletRenderPipeline()) return false;
        if (!createMeshletWireframePipeline()) return false;
        if (!createMeshletShadowPipeline()) return false;
    }

    if (!createShadowCullPipelines()) return false;

    return true;
}

void TerrainPipelines::destroy(VkDevice device) {
    // Destroy compute pipelines
    if (dispatcherPipeline) vkDestroyPipeline(device, dispatcherPipeline, nullptr);
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (sumReductionPrepassPipeline) vkDestroyPipeline(device, sumReductionPrepassPipeline, nullptr);
    if (sumReductionPrepassSubgroupPipeline) vkDestroyPipeline(device, sumReductionPrepassSubgroupPipeline, nullptr);
    if (sumReductionPipeline) vkDestroyPipeline(device, sumReductionPipeline, nullptr);
    if (sumReductionBatchedPipeline) vkDestroyPipeline(device, sumReductionBatchedPipeline, nullptr);
    if (frustumCullPipeline) vkDestroyPipeline(device, frustumCullPipeline, nullptr);
    if (prepareDispatchPipeline) vkDestroyPipeline(device, prepareDispatchPipeline, nullptr);

    // Destroy render pipelines
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);
    if (meshletRenderPipeline) vkDestroyPipeline(device, meshletRenderPipeline, nullptr);
    if (meshletWireframePipeline) vkDestroyPipeline(device, meshletWireframePipeline, nullptr);

    // Destroy shadow pipelines
    if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);
    if (meshletShadowPipeline) vkDestroyPipeline(device, meshletShadowPipeline, nullptr);

    // Destroy shadow culling pipelines
    if (shadowCullPipeline) vkDestroyPipeline(device, shadowCullPipeline, nullptr);
    if (shadowCulledPipeline) vkDestroyPipeline(device, shadowCulledPipeline, nullptr);
    if (meshletShadowCulledPipeline) vkDestroyPipeline(device, meshletShadowCulledPipeline, nullptr);

    // Destroy pipeline layouts
    if (dispatcherPipelineLayout) vkDestroyPipelineLayout(device, dispatcherPipelineLayout, nullptr);
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (sumReductionPipelineLayout) vkDestroyPipelineLayout(device, sumReductionPipelineLayout, nullptr);
    if (sumReductionBatchedPipelineLayout) vkDestroyPipelineLayout(device, sumReductionBatchedPipelineLayout, nullptr);
    if (frustumCullPipelineLayout) vkDestroyPipelineLayout(device, frustumCullPipelineLayout, nullptr);
    if (prepareDispatchPipelineLayout) vkDestroyPipelineLayout(device, prepareDispatchPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
    if (shadowCullPipelineLayout) vkDestroyPipelineLayout(device, shadowCullPipelineLayout, nullptr);
}

bool TerrainPipelines::createDispatcherPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainDispatcherPushConstants);

    dispatcherPipelineLayout = DescriptorManager::createPipelineLayout(device, computeDescriptorSetLayout, {pushConstantRange});
    if (dispatcherPipelineLayout == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = dispatcherPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dispatcherPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainPipelines::createSubdivisionPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainSubdivisionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &subdivisionPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = subdivisionPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &subdivisionPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainPipelines::createSumReductionPipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainSumReductionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sumReductionPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Prepass pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPrepassPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Subgroup-optimized prepass pipeline (processes 13 levels instead of 5)
    if (subgroupCaps && subgroupCaps->hasSubgroupArithmetic) {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv");
        if (shaderModule) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModule;
            stageInfo.pName = "main";

            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = sumReductionPipelineLayout;

            VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPrepassSubgroupPipeline);
            vkDestroyShaderModule(device, shaderModule, nullptr);
            if (result != VK_SUCCESS) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subgroup prepass pipeline, using fallback");
            } else {
                SDL_Log("TerrainPipelines: Using subgroup-optimized sum reduction prepass");
            }
        }
    }

    // Regular sum reduction pipeline (legacy single-level per dispatch)
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Batched sum reduction pipeline (multi-level per dispatch using shared memory)
    {
        // Create pipeline layout for batched push constants
        VkPushConstantRange batchedPushConstantRange{};
        batchedPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        batchedPushConstantRange.offset = 0;
        batchedPushConstantRange.size = sizeof(TerrainSumReductionBatchedPushConstants);

        VkPipelineLayoutCreateInfo batchedLayoutInfo{};
        batchedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        batchedLayoutInfo.setLayoutCount = 1;
        batchedLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        batchedLayoutInfo.pushConstantRangeCount = 1;
        batchedLayoutInfo.pPushConstantRanges = &batchedPushConstantRange;

        if (vkCreatePipelineLayout(device, &batchedLayoutInfo, nullptr, &sumReductionBatchedPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_batched.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionBatchedPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionBatchedPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    return true;
}

bool TerrainPipelines::createFrustumCullPipelines() {
    // Frustum cull pipeline (with push constants for dispatch calculation)
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_frustum_cull.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainFrustumCullPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &frustumCullPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = frustumCullPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &frustumCullPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Prepare cull dispatch pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_prepare_cull_dispatch.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainPrepareCullDispatchPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &prepareDispatchPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = prepareDispatchPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &prepareDispatchPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    return true;
}

bool TerrainPipelines::createRenderPipeline() {
    // Create render pipeline layout (shared by render and wireframe pipelines)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create filled render pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::filled(renderPass), renderPipelineLayout, renderPipeline);
}

bool TerrainPipelines::createWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::wireframe(renderPass), renderPipelineLayout, wireframePipeline);
}

bool TerrainPipelines::createShadowPipeline() {
    // Create shadow pipeline layout with push constants
    PipelineBuilder layoutBuilder(device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainShadowPushConstants));

    if (!layoutBuilder.buildPipelineLayout({renderDescriptorSetLayout}, shadowPipelineLayout)) {
        return false;
    }

    // Create shadow pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildGraphicsPipeline(PipelinePresets::shadow(shadowRenderPass), shadowPipelineLayout, shadowPipeline);
}

bool TerrainPipelines::createMeshletRenderPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::filled(renderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, renderPipelineLayout, meshletRenderPipeline);
}

bool TerrainPipelines::createMeshletWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::wireframe(renderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, renderPipelineLayout, meshletWireframePipeline);
}

bool TerrainPipelines::createMeshletShadowPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::shadow(shadowRenderPass);
    cfg.useMeshletVertexInput = true;

    return builder.buildGraphicsPipeline(cfg, shadowPipelineLayout, meshletShadowPipeline);
}

bool TerrainPipelines::createShadowCullPipelines() {
    // Create shadow cull compute pipeline
    VkShaderModule cullShaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_cull.comp.spv");
    if (!cullShaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow cull compute shader");
        return false;
    }

    // Pipeline layout for shadow cull compute
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainShadowCullPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowCullPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, cullShaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull pipeline layout");
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = cullShaderModule;
    stageInfo.pName = "main";

    // Specialization constant for meshlet index count
    VkSpecializationMapEntry specEntry{};
    specEntry.constantID = 0;
    specEntry.offset = 0;
    specEntry.size = sizeof(uint32_t);

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 1;
    specInfo.pMapEntries = &specEntry;
    specInfo.dataSize = sizeof(uint32_t);
    specInfo.pData = &meshletIndexCount;
    stageInfo.pSpecializationInfo = &specInfo;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = shadowCullPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowCullPipeline);
    vkDestroyShaderModule(device, cullShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull compute pipeline");
        return false;
    }

    // Create shadow culled graphics pipeline (non-meshlet)
    VkShaderModule shadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_culled.vert.spv");
    VkShaderModule shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!shadowCulledVertModule || !shadowFragModule) {
        if (shadowCulledVertModule) vkDestroyShaderModule(device, shadowCulledVertModule, nullptr);
        if (shadowFragModule) vkDestroyShaderModule(device, shadowFragModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow culled shaders");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = shadowCulledVertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = shadowFragModule;
    shaderStages[1].pName = "main";

    // No vertex input for non-meshlet (generated in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo gfxPipelineInfo{};
    gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gfxPipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    gfxPipelineInfo.pStages = shaderStages.data();
    gfxPipelineInfo.pVertexInputState = &vertexInputInfo;
    gfxPipelineInfo.pInputAssemblyState = &inputAssembly;
    gfxPipelineInfo.pViewportState = &viewportState;
    gfxPipelineInfo.pRasterizationState = &rasterizer;
    gfxPipelineInfo.pMultisampleState = &multisampling;
    gfxPipelineInfo.pDepthStencilState = &depthStencil;
    gfxPipelineInfo.pColorBlendState = &colorBlending;
    gfxPipelineInfo.pDynamicState = &dynamicState;
    gfxPipelineInfo.layout = shadowPipelineLayout;
    gfxPipelineInfo.renderPass = shadowRenderPass;
    gfxPipelineInfo.subpass = 0;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &shadowCulledPipeline);
    vkDestroyShaderModule(device, shadowCulledVertModule, nullptr);
    vkDestroyShaderModule(device, shadowFragModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow culled graphics pipeline");
        return false;
    }

    // Create meshlet shadow culled pipeline (if meshlets enabled)
    if (useMeshlets) {
        VkShaderModule meshletShadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_meshlet_shadow_culled.vert.spv");
        shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
        if (!meshletShadowCulledVertModule || !shadowFragModule) {
            if (meshletShadowCulledVertModule) vkDestroyShaderModule(device, meshletShadowCulledVertModule, nullptr);
            if (shadowFragModule) vkDestroyShaderModule(device, shadowFragModule, nullptr);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load meshlet shadow culled shaders");
            return false;
        }

        shaderStages[0].module = meshletShadowCulledVertModule;
        shaderStages[1].module = shadowFragModule;

        // Meshlet vertex input: vec2 for local UV
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(glm::vec2);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrDesc{};
        attrDesc.binding = 0;
        attrDesc.location = 0;
        attrDesc.format = VK_FORMAT_R32G32_SFLOAT;
        attrDesc.offset = 0;

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = 1;
        vertexInputInfo.pVertexAttributeDescriptions = &attrDesc;

        gfxPipelineInfo.pStages = shaderStages.data();

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &meshletShadowCulledPipeline);
        vkDestroyShaderModule(device, meshletShadowCulledVertModule, nullptr);
        vkDestroyShaderModule(device, shadowFragModule, nullptr);

        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet shadow culled graphics pipeline");
            return false;
        }
    }

    SDL_Log("TerrainPipelines: Shadow culling pipelines created successfully");
    return true;
}
