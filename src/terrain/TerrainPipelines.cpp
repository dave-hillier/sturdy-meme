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

void TerrainPipelines::destroy(VkDevice /*device*/) {
    // RAII handles cleanup - nothing to do here
}

bool TerrainPipelines::createDispatcherPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainDispatcherPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!ManagedPipelineLayout::create(device, layoutInfo, dispatcherPipelineLayout_)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = dispatcherPipelineLayout_.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, dispatcherPipeline_);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    return success;
}

bool TerrainPipelines::createSubdivisionPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
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

    if (!ManagedPipelineLayout::create(device, layoutInfo, subdivisionPipelineLayout_)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = subdivisionPipelineLayout_.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, subdivisionPipeline_);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    return success;
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

    if (!ManagedPipelineLayout::create(device, layoutInfo, sumReductionPipelineLayout_)) {
        return false;
    }

    // Prepass pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = *shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout_.get();

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, sumReductionPrepassPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    // Subgroup-optimized prepass pipeline (processes 13 levels instead of 5)
    if (subgroupCaps && subgroupCaps->hasSubgroupArithmetic) {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv");
        if (shaderModule) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = *shaderModule;
            stageInfo.pName = "main";

            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = sumReductionPipelineLayout_.get();

            bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, sumReductionPrepassSubgroupPipeline_);
            vkDestroyShaderModule(device, *shaderModule, nullptr);
            if (!success) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subgroup prepass pipeline, using fallback");
            } else {
                SDL_Log("TerrainPipelines: Using subgroup-optimized sum reduction prepass");
            }
        }
    }

    // Regular sum reduction pipeline (legacy single-level per dispatch)
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = *shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout_.get();

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, sumReductionPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
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

        if (!ManagedPipelineLayout::create(device, batchedLayoutInfo, sumReductionBatchedPipelineLayout_)) {
            return false;
        }

        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_batched.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = *shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionBatchedPipelineLayout_.get();

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, sumReductionBatchedPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    return true;
}

bool TerrainPipelines::createFrustumCullPipelines() {
    // Frustum cull pipeline (with push constants for dispatch calculation)
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_frustum_cull.comp.spv");
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

        if (!ManagedPipelineLayout::create(device, layoutInfo, frustumCullPipelineLayout_)) {
            vkDestroyShaderModule(device, *shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = *shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = frustumCullPipelineLayout_.get();

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, frustumCullPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    // Prepare cull dispatch pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_prepare_cull_dispatch.comp.spv");
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

        if (!ManagedPipelineLayout::create(device, layoutInfo, prepareDispatchPipelineLayout_)) {
            vkDestroyShaderModule(device, *shaderModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = *shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = prepareDispatchPipelineLayout_.get();

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, prepareDispatchPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    return true;
}

bool TerrainPipelines::createRenderPipeline() {
    // Create render pipeline layout (shared by render and wireframe pipelines)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;

    if (!ManagedPipelineLayout::create(device, layoutInfo, renderPipelineLayout_)) {
        return false;
    }

    // Create filled render pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::filled(renderPass), renderPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    // Wrap the raw pipeline - ManagedPipeline will handle cleanup
    renderPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::wireframe(renderPass), renderPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    wireframePipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createShadowPipeline() {
    // Create shadow pipeline layout with push constants
    PipelineBuilder layoutBuilder(device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainShadowPushConstants));

    VkPipelineLayout rawLayout = VK_NULL_HANDLE;
    if (!layoutBuilder.buildPipelineLayout({renderDescriptorSetLayout}, rawLayout)) {
        return false;
    }
    shadowPipelineLayout_ = ManagedPipelineLayout::fromRaw(device, rawLayout);

    // Create shadow pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::shadow(shadowRenderPass), shadowPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    shadowPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletRenderPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::filled(renderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, renderPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    meshletRenderPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::wireframe(renderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, renderPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    meshletWireframePipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletShadowPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::shadow(shadowRenderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, shadowPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    meshletShadowPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool TerrainPipelines::createShadowCullPipelines() {
    // Create shadow cull compute pipeline
    auto cullShaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_cull.comp.spv");
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

    if (!ManagedPipelineLayout::create(device, layoutInfo, shadowCullPipelineLayout_)) {
        vkDestroyShaderModule(device, *cullShaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull pipeline layout");
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *cullShaderModule;
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
    pipelineInfo.layout = shadowCullPipelineLayout_.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, shadowCullPipeline_);
    vkDestroyShaderModule(device, *cullShaderModule, nullptr);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull compute pipeline");
        return false;
    }

    // Create shadow culled graphics pipeline (non-meshlet)
    auto shadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_culled.vert.spv");
    auto shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!shadowCulledVertModule || !shadowFragModule) {
        if (shadowCulledVertModule) vkDestroyShaderModule(device, *shadowCulledVertModule, nullptr);
        if (shadowFragModule) vkDestroyShaderModule(device, *shadowFragModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow culled shaders");
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *shadowCulledVertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *shadowFragModule;
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
    gfxPipelineInfo.layout = shadowPipelineLayout_.get();
    gfxPipelineInfo.renderPass = shadowRenderPass;
    gfxPipelineInfo.subpass = 0;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(device, *shadowCulledVertModule, nullptr);
    vkDestroyShaderModule(device, *shadowFragModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow culled graphics pipeline");
        return false;
    }
    shadowCulledPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);

    // Create meshlet shadow culled pipeline (if meshlets enabled)
    if (useMeshlets) {
        auto meshletShadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_meshlet_shadow_culled.vert.spv");
        auto meshletShadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
        if (!meshletShadowCulledVertModule || !meshletShadowFragModule) {
            if (meshletShadowCulledVertModule) vkDestroyShaderModule(device, *meshletShadowCulledVertModule, nullptr);
            if (meshletShadowFragModule) vkDestroyShaderModule(device, *meshletShadowFragModule, nullptr);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load meshlet shadow culled shaders");
            return false;
        }

        shaderStages[0].module = *meshletShadowCulledVertModule;
        shaderStages[1].module = *meshletShadowFragModule;

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

        rawPipeline = VK_NULL_HANDLE;
        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &rawPipeline);
        vkDestroyShaderModule(device, *meshletShadowCulledVertModule, nullptr);
        vkDestroyShaderModule(device, *meshletShadowFragModule, nullptr);

        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet shadow culled graphics pipeline");
            return false;
        }
        meshletShadowCulledPipeline_ = ManagedPipeline::fromRaw(device, rawPipeline);
    }

    SDL_Log("TerrainPipelines: Shadow culling pipelines created successfully");
    return true;
}
