#include "TerrainPipelines.h"
#include "TerrainSystem.h"  // For push constant structs and SubgroupCapabilities
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>
#include <vulkan/vulkan.hpp>

using ShaderLoader::loadShaderModule;

std::unique_ptr<TerrainPipelines> TerrainPipelines::create(const InitInfo& info) {
    std::unique_ptr<TerrainPipelines> pipelines(new TerrainPipelines());
    if (!pipelines->initInternal(info)) {
        return nullptr;
    }
    return pipelines;
}

bool TerrainPipelines::initInternal(const InitInfo& info) {
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

bool TerrainPipelines::createDispatcherPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainDispatcherPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

    if (!ManagedPipelineLayout::create(device, layoutInfo, dispatcherPipelineLayout_)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*shaderModule))
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(dispatcherPipelineLayout_.get());

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
        *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), dispatcherPipeline_);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    return success;
}

bool TerrainPipelines::createSubdivisionPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
    if (!shaderModule) return false;

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainSubdivisionPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

    if (!ManagedPipelineLayout::create(device, layoutInfo, subdivisionPipelineLayout_)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*shaderModule))
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(subdivisionPipelineLayout_.get());

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
        *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), subdivisionPipeline_);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    return success;
}

bool TerrainPipelines::createSumReductionPipelines() {
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainSumReductionPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

    if (!ManagedPipelineLayout::create(device, layoutInfo, sumReductionPipelineLayout_)) {
        return false;
    }

    // Prepass pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(sumReductionPipelineLayout_.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), sumReductionPrepassPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    // Subgroup-optimized prepass pipeline (processes 13 levels instead of 5)
    if (subgroupCaps && subgroupCaps->hasSubgroupArithmetic) {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv");
        if (shaderModule) {
            auto stageInfo = vk::PipelineShaderStageCreateInfo{}
                .setStage(vk::ShaderStageFlagBits::eCompute)
                .setModule(static_cast<vk::ShaderModule>(*shaderModule))
                .setPName("main");

            auto pipelineInfo = vk::ComputePipelineCreateInfo{}
                .setStage(stageInfo)
                .setLayout(sumReductionPipelineLayout_.get());

            bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
                *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), sumReductionPrepassSubgroupPipeline_);
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

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(sumReductionPipelineLayout_.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), sumReductionPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    // Batched sum reduction pipeline (multi-level per dispatch using shared memory)
    {
        // Create pipeline layout for batched push constants
        auto batchedPushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainSumReductionBatchedPushConstants));

        VkPipelineLayoutCreateInfo batchedLayoutInfo{};
        batchedLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        batchedLayoutInfo.setLayoutCount = 1;
        batchedLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        batchedLayoutInfo.pushConstantRangeCount = 1;
        batchedLayoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&batchedPushConstantRange);

        if (!ManagedPipelineLayout::create(device, batchedLayoutInfo, sumReductionBatchedPipelineLayout_)) {
            return false;
        }

        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_batched.comp.spv");
        if (!shaderModule) return false;

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(sumReductionBatchedPipelineLayout_.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), sumReductionBatchedPipeline_);
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

        auto pushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainFrustumCullPushConstants));

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

        if (!ManagedPipelineLayout::create(device, layoutInfo, frustumCullPipelineLayout_)) {
            vkDestroyShaderModule(device, *shaderModule, nullptr);
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(frustumCullPipelineLayout_.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), frustumCullPipeline_);
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        if (!success) return false;
    }

    // Prepare cull dispatch pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_prepare_cull_dispatch.comp.spv");
        if (!shaderModule) return false;

        auto pushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainPrepareCullDispatchPushConstants));

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

        if (!ManagedPipelineLayout::create(device, layoutInfo, prepareDispatchPipelineLayout_)) {
            vkDestroyShaderModule(device, *shaderModule, nullptr);
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(prepareDispatchPipelineLayout_.get());

        bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
            *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), prepareDispatchPipeline_);
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
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainShadowCullPushConstants));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(&pushConstantRange);

    if (!ManagedPipelineLayout::create(device, layoutInfo, shadowCullPipelineLayout_)) {
        vkDestroyShaderModule(device, *cullShaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull pipeline layout");
        return false;
    }

    // Specialization constant for meshlet index count
    auto specEntry = vk::SpecializationMapEntry{}
        .setConstantID(0)
        .setOffset(0)
        .setSize(sizeof(uint32_t));

    auto specInfo = vk::SpecializationInfo{}
        .setMapEntries(specEntry)
        .setDataSize(sizeof(uint32_t))
        .setPData(&meshletIndexCount);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*cullShaderModule))
        .setPName("main")
        .setPSpecializationInfo(&specInfo);

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(shadowCullPipelineLayout_.get());

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
        *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), shadowCullPipeline_);
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

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(static_cast<vk::ShaderModule>(*shadowCulledVertModule))
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(static_cast<vk::ShaderModule>(*shadowFragModule))
            .setPName("main")
    };

    // No vertex input for non-meshlet (generated in shader)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eFront)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_TRUE);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(0);

    std::array<vk::DynamicState, 3> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eDepthBias
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    auto gfxPipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(shadowPipelineLayout_.get())
        .setRenderPass(shadowRenderPass)
        .setSubpass(0);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        reinterpret_cast<const VkGraphicsPipelineCreateInfo*>(&gfxPipelineInfo), nullptr, &rawPipeline);
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

        shaderStages[0].setModule(static_cast<vk::ShaderModule>(*meshletShadowCulledVertModule));
        shaderStages[1].setModule(static_cast<vk::ShaderModule>(*meshletShadowFragModule));

        // Meshlet vertex input: vec2 for local UV
        auto bindingDesc = vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(glm::vec2))
            .setInputRate(vk::VertexInputRate::eVertex);

        auto attrDesc = vk::VertexInputAttributeDescription{}
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(0);

        vertexInputInfo
            .setVertexBindingDescriptions(bindingDesc)
            .setVertexAttributeDescriptions(attrDesc);

        gfxPipelineInfo.setStages(shaderStages);

        rawPipeline = VK_NULL_HANDLE;
        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkGraphicsPipelineCreateInfo*>(&gfxPipelineInfo), nullptr, &rawPipeline);
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
