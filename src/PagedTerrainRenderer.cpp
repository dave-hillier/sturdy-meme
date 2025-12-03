#include "PagedTerrainRenderer.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>

using ShaderLoader::loadShaderModule;

bool PagedTerrainRenderer::init(const InitInfo& info, const PagedTerrainConfig& cfg) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    config = cfg;

    // Initialize streaming manager
    streamingManager = std::make_unique<TerrainStreamingManager>();

    StreamingManager::InitInfo streamingBaseInfo{};
    streamingBaseInfo.device = device;
    streamingBaseInfo.physicalDevice = physicalDevice;
    streamingBaseInfo.allocator = allocator;
    streamingBaseInfo.graphicsQueue = graphicsQueue;
    streamingBaseInfo.commandPool = commandPool;
    streamingBaseInfo.numWorkerThreads = 2;
    streamingBaseInfo.budget = config.streamingConfig.budget;

    if (!streamingManager->init(streamingBaseInfo, config.streamingConfig)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize terrain streaming manager");
        return false;
    }

    // Initialize shared textures
    TerrainTextures::InitInfo texturesInfo{};
    texturesInfo.device = device;
    texturesInfo.allocator = allocator;
    texturesInfo.graphicsQueue = graphicsQueue;
    texturesInfo.commandPool = commandPool;
    texturesInfo.resourcePath = info.texturePath;
    if (!textures.init(texturesInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize terrain textures");
        return false;
    }

    // Create pipelines and descriptor layouts
    if (!createDescriptorSetLayouts()) return false;
    if (!createUniformBuffers()) return false;
    if (!createPipelines()) return false;

    // Initialize descriptor set pool
    tileDescriptorSets.resize(framesInFlight);
    for (auto& frameSets : tileDescriptorSets) {
        frameSets.reserve(INITIAL_DESCRIPTOR_POOL_SIZE);
    }

    SDL_Log("PagedTerrainRenderer initialized with base tile size %.1f, %zu LOD levels",
            config.streamingConfig.tileConfig.baseTileSize,
            config.streamingConfig.lodLevels.size());
    return true;
}

void PagedTerrainRenderer::destroy() {
    vkDeviceWaitIdle(device);

    // Shutdown streaming manager first
    if (streamingManager) {
        streamingManager->shutdown();
        streamingManager.reset();
    }

    // Destroy indirect buffer pool
    for (auto& buffers : indirectBufferPool) {
        if (buffers.dispatchBuffer) {
            vmaDestroyBuffer(allocator, buffers.dispatchBuffer, buffers.dispatchAllocation);
        }
        if (buffers.drawBuffer) {
            vmaDestroyBuffer(allocator, buffers.drawBuffer, buffers.drawAllocation);
        }
    }
    indirectBufferPool.clear();

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i]) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
        }
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    // Destroy pipelines
    if (dispatcherPipeline) vkDestroyPipeline(device, dispatcherPipeline, nullptr);
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (sumReductionPrepassPipeline) vkDestroyPipeline(device, sumReductionPrepassPipeline, nullptr);
    if (sumReductionPipeline) vkDestroyPipeline(device, sumReductionPipeline, nullptr);
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);
    if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);

    // Destroy pipeline layouts
    if (dispatcherPipelineLayout) vkDestroyPipelineLayout(device, dispatcherPipelineLayout, nullptr);
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (sumReductionPipelineLayout) vkDestroyPipelineLayout(device, sumReductionPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);

    // Destroy textures
    textures.destroy(device, allocator);
}

bool PagedTerrainRenderer::createDescriptorSetLayouts() {
    // Compute descriptor set layout (per-tile CBT, indirect buffers, heightmap, uniforms)
    {
        auto makeBinding = [](uint32_t binding, VkDescriptorType type) {
            return BindingBuilder()
                .setBinding(binding)
                .setDescriptorType(type)
                .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
                .build();
        };

        std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
            makeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // CBT buffer
            makeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Indirect dispatch
            makeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Indirect draw
            makeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),  // Height map
            makeBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)   // Uniforms
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
    }

    // Render descriptor set layout
    {
        auto makeBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) {
            return BindingBuilder()
                .setBinding(binding)
                .setDescriptorType(type)
                .setStageFlags(stages)
                .build();
        };

        std::array<VkDescriptorSetLayoutBinding, 8> bindings = {
            makeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),  // CBT
            makeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),  // Height map
            makeBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),  // Uniforms
            makeBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),  // Scene UBO
            makeBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),  // Albedo
            makeBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),  // Shadow
            makeBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),  // Grass LOD
            makeBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)   // Snow mask
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &renderDescriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool PagedTerrainRenderer::createUniformBuffers() {
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    // Size for TerrainUniforms struct (from shader)
    VkDeviceSize uniformSize = 256;  // Conservative size for the uniform buffer

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = uniformSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                           &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool PagedTerrainRenderer::createPipelines() {
    // Create dispatcher pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(uint32_t) * 2;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &dispatcherPipelineLayout) != VK_SUCCESS) {
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
        if (result != VK_SUCCESS) return false;
    }

    // Create subdivision pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
        if (!shaderModule) return false;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(uint32_t);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

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
        if (result != VK_SUCCESS) return false;
    }

    // Create sum reduction pipelines
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(int);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sumReductionPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        // Prepass
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

        // Regular reduction
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
    }

    // Create render pipeline with tile offset push constants
    {
        VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_paged.vert.spv");
        VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain.frag.spv");

        // Fall back to non-paged vertex shader if paged doesn't exist
        if (!vertModule) {
            vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain.vert.spv");
        }

        if (!vertModule || !fragModule) {
            if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

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
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TileRenderPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &renderDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertModule, nullptr);
            vkDestroyShaderModule(device, fragModule, nullptr);
            return false;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = renderPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderPipeline);

        // Create wireframe variant
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline);

        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);

        if (result != VK_SUCCESS) return false;
    }

    // Create shadow pipeline
    {
        VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.vert.spv");
        VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
        if (!vertModule || !fragModule) {
            if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

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
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(TileShadowPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &renderDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertModule, nullptr);
            vkDestroyShaderModule(device, fragModule, nullptr);
            return false;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = shadowPipelineLayout;
        pipelineInfo.renderPass = shadowRenderPass;
        pipelineInfo.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);

        if (result != VK_SUCCESS) return false;
    }

    return true;
}

void PagedTerrainRenderer::update(const glm::vec3& cameraPos, uint64_t frameNumber) {
    if (streamingManager) {
        streamingManager->update(cameraPos, frameNumber);
    }
}

void PagedTerrainRenderer::updateDescriptorSets(const std::vector<VkBuffer>& sceneUniformBuffers,
                                                  VkImageView shadowMapView,
                                                  VkSampler shadowSampler) {
    // This updates the shared resources in all allocated descriptor sets
    // Per-tile resources are updated in getDescriptorSetForTile
}

void PagedTerrainRenderer::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                           const glm::mat4& view, const glm::mat4& proj) {
    // Update shared uniform buffer
    // Structure matches TerrainUniforms in shader
    struct Uniforms {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 viewProjMatrix;
        glm::vec4 frustumPlanes[6];
        glm::vec4 cameraPosition;
        glm::vec4 terrainParams;
        glm::vec4 lodParams;
        glm::vec2 screenSize;
        float lodFactor;
        float padding;
    };

    Uniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Terrain params - use base tile size (LOD 0); actual tile size passed via push constants
    float baseTileSize = config.streamingConfig.tileConfig.baseTileSize;
    float heightScale = config.streamingConfig.tileConfig.getHeightScale();
    uniforms.terrainParams = glm::vec4(baseTileSize, heightScale, config.targetEdgePixels,
                                        static_cast<float>(config.maxCBTDepth));
    uniforms.lodParams = glm::vec4(config.splitThreshold, config.mergeThreshold,
                                    static_cast<float>(config.minCBTDepth),
                                    static_cast<float>(subdivisionFrameCount & 1));
    uniforms.screenSize = glm::vec2(extent.width, extent.height);

    float fov = 2.0f * atan(1.0f / proj[1][1]);
    uniforms.lodFactor = 2.0f * log2(extent.height / (2.0f * tan(fov * 0.5f) * config.targetEdgePixels));

    // Extract frustum planes
    glm::mat4 viewProj = proj * view;
    for (int i = 0; i < 6; i++) {
        int row = i / 2;
        float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        uniforms.frustumPlanes[i] = glm::vec4(
            viewProj[0][3] + sign * viewProj[0][row],
            viewProj[1][3] + sign * viewProj[1][row],
            viewProj[2][3] + sign * viewProj[2][row],
            viewProj[3][3] + sign * viewProj[3][row]
        );
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        uniforms.frustumPlanes[i] /= len;
    }

    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(uniforms));
}

void PagedTerrainRenderer::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!streamingManager) return;

    const auto& tiles = streamingManager->getVisibleTiles();
    if (tiles.empty()) return;

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    for (TerrainTile* tile : tiles) {
        TileDescriptorSet* ds = getDescriptorSetForTile(tile, frameIndex);
        if (!ds) continue;

        recordTileCompute(cmd, tile, ds, frameIndex);

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    subdivisionFrameCount++;

    // Final barrier before rendering
    VkMemoryBarrier renderBarrier{};
    renderBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    renderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    renderBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        0, 1, &renderBarrier, 0, nullptr, 0, nullptr);
}

void PagedTerrainRenderer::recordTileCompute(VkCommandBuffer cmd, TerrainTile* tile,
                                              TileDescriptorSet* ds, uint32_t frameIndex) {
    // For now, simplified compute - would need per-tile indirect buffers
    // This is a placeholder for the full implementation
}

void PagedTerrainRenderer::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!streamingManager) return;

    const auto& tiles = streamingManager->getVisibleTiles();
    if (tiles.empty()) return;

    VkPipeline pipeline = wireframeMode ? wireframePipeline : renderPipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (TerrainTile* tile : tiles) {
        TileDescriptorSet* ds = getDescriptorSetForTile(tile, frameIndex);
        if (!ds || !ds->renderSet) continue;

        recordTileDraw(cmd, tile, ds, frameIndex);
    }
}

void PagedTerrainRenderer::recordTileDraw(VkCommandBuffer cmd, TerrainTile* tile,
                                           TileDescriptorSet* ds, uint32_t frameIndex) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout,
                           0, 1, &ds->renderSet, 0, nullptr);

    TileRenderPushConstants pc{};
    pc.tileOffset = tile->getWorldMin();
    pc.tileSize = tile->getTileSize();  // Tile size varies by LOD level
    pc.heightScale = config.streamingConfig.tileConfig.getHeightScale();

    vkCmdPushConstants(cmd, renderPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Draw tile triangles
    // For now using a fixed triangle count - full implementation would use indirect draw
    uint32_t initialTriangles = 1u << config.streamingConfig.tileConfig.cbtInitDepth;
    vkCmdDraw(cmd, initialTriangles * 3, 1, 0, 0);
}

void PagedTerrainRenderer::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                             const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!streamingManager) return;

    const auto& tiles = streamingManager->getVisibleTiles();
    if (tiles.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

    for (TerrainTile* tile : tiles) {
        TileDescriptorSet* ds = getDescriptorSetForTile(tile, frameIndex);
        if (!ds || !ds->renderSet) continue;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout,
                               0, 1, &ds->renderSet, 0, nullptr);

        TileShadowPushConstants pc{};
        pc.lightViewProj = lightViewProj;
        pc.tileOffset = tile->getWorldMin();
        pc.tileSize = tile->getTileSize();  // Tile size varies by LOD level
        pc.heightScale = config.streamingConfig.tileConfig.getHeightScale();
        pc.cascadeIndex = cascadeIndex;

        vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        uint32_t initialTriangles = 1u << config.streamingConfig.tileConfig.cbtInitDepth;
        vkCmdDraw(cmd, initialTriangles * 3, 1, 0, 0);
    }
}

PagedTerrainRenderer::TileDescriptorSet* PagedTerrainRenderer::getDescriptorSetForTile(
    TerrainTile* tile, uint32_t frameIndex) {
    if (!tile || tile->getLoadState() != TileLoadState::Loaded) {
        return nullptr;
    }

    auto& frameSets = tileDescriptorSets[frameIndex];

    // Check if we already have a descriptor set for this tile
    for (auto& ds : frameSets) {
        if (ds.tile == tile) {
            return &ds;
        }
    }

    // Find an unused descriptor set
    for (auto& ds : frameSets) {
        if (!ds.tile || ds.tile->getLoadState() != TileLoadState::Loaded) {
            ds.tile = tile;
            updateTileDescriptorSet(&ds, tile, frameIndex);
            return &ds;
        }
    }

    // Need to allocate new descriptor sets
    TileDescriptorSet newDs{};
    newDs.tile = tile;

    // Allocate compute descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &computeDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &newDs.computeSet) != VK_SUCCESS) {
        return nullptr;
    }

    // Allocate render descriptor set
    allocInfo.pSetLayouts = &renderDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &newDs.renderSet) != VK_SUCCESS) {
        return nullptr;
    }

    updateTileDescriptorSet(&newDs, tile, frameIndex);

    frameSets.push_back(newDs);
    return &frameSets.back();
}

void PagedTerrainRenderer::updateTileDescriptorSet(TileDescriptorSet* ds, TerrainTile* tile, uint32_t frameIndex) {
    // For now, skip compute set update (would need per-tile indirect buffers)

    // Update render descriptor set
    std::vector<VkWriteDescriptorSet> writes;

    // CBT buffer
    VkDescriptorBufferInfo cbtInfo{tile->getCBTBuffer(), 0, tile->getCBTBufferSize()};
    VkWriteDescriptorSet cbtWrite{};
    cbtWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    cbtWrite.dstSet = ds->renderSet;
    cbtWrite.dstBinding = 0;
    cbtWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    cbtWrite.descriptorCount = 1;
    cbtWrite.pBufferInfo = &cbtInfo;
    writes.push_back(cbtWrite);

    // Height map
    VkDescriptorImageInfo heightMapInfo{tile->getHeightmapSampler(), tile->getHeightmapView(),
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet heightWrite{};
    heightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    heightWrite.dstSet = ds->renderSet;
    heightWrite.dstBinding = 3;
    heightWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    heightWrite.descriptorCount = 1;
    heightWrite.pImageInfo = &heightMapInfo;
    writes.push_back(heightWrite);

    // Uniform buffer
    VkDescriptorBufferInfo uniformInfo{uniformBuffers[frameIndex], 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet uniformWrite{};
    uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uniformWrite.dstSet = ds->renderSet;
    uniformWrite.dstBinding = 4;
    uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite.descriptorCount = 1;
    uniformWrite.pBufferInfo = &uniformInfo;
    writes.push_back(uniformWrite);

    // Albedo texture
    VkDescriptorImageInfo albedoInfo{textures.getAlbedoSampler(), textures.getAlbedoView(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet albedoWrite{};
    albedoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    albedoWrite.dstSet = ds->renderSet;
    albedoWrite.dstBinding = 6;
    albedoWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoWrite.descriptorCount = 1;
    albedoWrite.pImageInfo = &albedoInfo;
    writes.push_back(albedoWrite);

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

float PagedTerrainRenderer::getHeightAt(float worldX, float worldZ) const {
    if (streamingManager) {
        return streamingManager->getHeightAt(worldX, worldZ);
    }
    return 0.0f;
}

uint32_t PagedTerrainRenderer::getLoadedTileCount() const {
    return streamingManager ? streamingManager->getLoadedTileCount() : 0;
}

uint32_t PagedTerrainRenderer::getVisibleTileCount() const {
    return streamingManager ? static_cast<uint32_t>(streamingManager->getVisibleTiles().size()) : 0;
}

size_t PagedTerrainRenderer::getGPUMemoryUsage() const {
    return streamingManager ? streamingManager->getCurrentGPUMemoryUsage() : 0;
}
