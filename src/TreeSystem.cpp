#include "TreeSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "BindingBuilder.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

bool TreeSystem::init(const InitInfo& info) {
    shadowRenderPass = info.shadowRenderPass;
    shadowMapSize = info.shadowMapSize;

    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = [this]() { return createGraphicsDescriptorSetLayout(); };
    hooks.createGraphicsPipeline = [this]() { return createGraphicsPipeline(); };
    hooks.createExtraPipelines = [this]() { return createExtraPipelines(); };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    if (!particleSystem.init(info, hooks, BUFFER_SET_COUNT)) {
        return false;
    }

    // Set up default tree definition
    setDefaultTreeDefinition();

    return true;
}

void TreeSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    vkDestroyPipeline(dev, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(dev, shadowPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, shadowDescriptorSetLayout, nullptr);

    if (definitionBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(alloc, definitionBuffer, definitionAllocation);
    }
    if (treeInstanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(alloc, treeInstanceBuffer, treeInstanceAllocation);
    }

    particleSystem.destroy(dev, alloc);
}

void TreeSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, branchBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
}

bool TreeSystem::createBuffers() {
    VkDeviceSize branchBufferSize = sizeof(BranchInstance) * MAX_BRANCHES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(TreeUniforms);

    BufferUtils::DoubleBufferedBufferBuilder branchBuilder;
    if (!branchBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(branchBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(branchBuffers)) {
        SDL_Log("Failed to create tree branch buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers)) {
        SDL_Log("Failed to create tree indirect buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create tree uniform buffers");
        return false;
    }

    // Create definition buffer
    VkBufferCreateInfo defBufferInfo{};
    defBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    defBufferInfo.size = sizeof(TreeDefinition) * MAX_DEFINITIONS;
    defBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    defBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo defAllocInfo{};
    defAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    defAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo defMappedInfo;
    if (vmaCreateBuffer(getAllocator(), &defBufferInfo, &defAllocInfo,
                        &definitionBuffer, &definitionAllocation, &defMappedInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create tree definition buffer");
        return false;
    }
    definitionMappedPtr = defMappedInfo.pMappedData;

    // Create tree instance buffer
    VkBufferCreateInfo treeBufferInfo{};
    treeBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    treeBufferInfo.size = sizeof(TreeInstance) * MAX_TREES;
    treeBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    treeBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo treeAllocInfo{};
    treeAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    treeAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo treeMappedInfo;
    if (vmaCreateBuffer(getAllocator(), &treeBufferInfo, &treeAllocInfo,
                        &treeInstanceBuffer, &treeInstanceAllocation, &treeMappedInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create tree instance buffer");
        return false;
    }
    treeInstanceMappedPtr = treeMappedInfo.pMappedData;

    return true;
}

bool TreeSystem::createComputeDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    // binding 0: branch output buffer (storage)
    // binding 1: indirect buffer (storage)
    // binding 2: tree uniforms (uniform)
    // binding 3: tree definitions (storage)
    // binding 4: tree instances (storage)
    // binding 5: terrain heightmap (sampler)
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(getComputePipelineHandles().descriptorSetLayout);
}

bool TreeSystem::createComputePipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/tree_branch.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TreePushConstants));

    if (!builder.buildPipelineLayout({getComputePipelineHandles().descriptorSetLayout}, getComputePipelineHandles().pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(getComputePipelineHandles().pipelineLayout, getComputePipelineHandles().pipeline);
}

bool TreeSystem::createGraphicsDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    // binding 0: renderer UBO (uniform)
    // binding 1: branch buffer (storage)
    // binding 2: shadow map (sampler)
    // binding 3: wind uniforms (uniform)
    // binding 4: light buffer (storage)
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildDescriptorSetLayout(getGraphicsPipelineHandles().descriptorSetLayout);
}

bool TreeSystem::createGraphicsPipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/tree_branch.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/tree_branch.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TreePushConstants));

    // No vertex input - procedural geometry from branch buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_TRUE;  // Use primitive restart for branch segments

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(getExtent().width);
    viewport.height = static_cast<float>(getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Back-face culling for branches
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    if (!builder.buildPipelineLayout({getGraphicsPipelineHandles().descriptorSetLayout}, getGraphicsPipelineHandles().pipelineLayout)) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, getGraphicsPipelineHandles().pipelineLayout, getGraphicsPipelineHandles().pipeline);
}

bool TreeSystem::createShadowPipeline() {
    PipelineBuilder layoutBuilder(getDevice());
    layoutBuilder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);

    if (!layoutBuilder.buildDescriptorSetLayout(shadowDescriptorSetLayout)) {
        return false;
    }

    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/tree_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/tree_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TreePushConstants));

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_TRUE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.25f;
    rasterizer.depthBiasSlopeFactor = 0.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    if (!builder.buildPipelineLayout({shadowDescriptorSetLayout}, shadowPipelineLayout)) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, shadowPipelineLayout, shadowPipeline);
}

bool TreeSystem::createDescriptorSets() {
    if (!particleSystem.createStandardDescriptorSets()) {
        return false;
    }

    // Allocate shadow descriptor sets for both buffer sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        VkDescriptorSetAllocateInfo shadowAllocInfo{};
        shadowAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        shadowAllocInfo.descriptorPool = getDescriptorPool();
        shadowAllocInfo.descriptorSetCount = 1;
        shadowAllocInfo.pSetLayouts = &shadowDescriptorSetLayout;

        if (vkAllocateDescriptorSets(getDevice(), &shadowAllocInfo, &shadowDescriptorSetsDB[set]) != VK_SUCCESS) {
            SDL_Log("Failed to allocate tree shadow descriptor set (set %u)", set);
            return false;
        }

        // Update compute descriptor sets
        VkDescriptorBufferInfo branchBufferInfo{};
        branchBufferInfo.buffer = branchBuffers.buffers[set];
        branchBufferInfo.offset = 0;
        branchBufferInfo.range = sizeof(BranchInstance) * MAX_BRANCHES;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers.buffers[set];
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = sizeof(VkDrawIndirectCommand);

        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers.buffers[0];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(TreeUniforms);

        VkDescriptorBufferInfo definitionBufferInfo{};
        definitionBufferInfo.buffer = definitionBuffer;
        definitionBufferInfo.offset = 0;
        definitionBufferInfo.range = sizeof(TreeDefinition) * MAX_DEFINITIONS;

        VkDescriptorBufferInfo treeInstanceBufferInfo{};
        treeInstanceBufferInfo.buffer = treeInstanceBuffer;
        treeInstanceBufferInfo.offset = 0;
        treeInstanceBufferInfo.range = sizeof(TreeInstance) * MAX_TREES;

        std::array<VkWriteDescriptorSet, 5> computeWrites{};

        computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[0].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[0].dstBinding = 0;
        computeWrites[0].dstArrayElement = 0;
        computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[0].descriptorCount = 1;
        computeWrites[0].pBufferInfo = &branchBufferInfo;

        computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[1].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[1].dstBinding = 1;
        computeWrites[1].dstArrayElement = 0;
        computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[1].descriptorCount = 1;
        computeWrites[1].pBufferInfo = &indirectBufferInfo;

        computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[2].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[2].dstBinding = 2;
        computeWrites[2].dstArrayElement = 0;
        computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[2].descriptorCount = 1;
        computeWrites[2].pBufferInfo = &uniformBufferInfo;

        computeWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[3].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[3].dstBinding = 3;
        computeWrites[3].dstArrayElement = 0;
        computeWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[3].descriptorCount = 1;
        computeWrites[3].pBufferInfo = &definitionBufferInfo;

        computeWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[4].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[4].dstBinding = 4;
        computeWrites[4].dstArrayElement = 0;
        computeWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[4].descriptorCount = 1;
        computeWrites[4].pBufferInfo = &treeInstanceBufferInfo;

        vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(computeWrites.size()),
                               computeWrites.data(), 0, nullptr);
    }

    return true;
}

bool TreeSystem::createExtraPipelines() {
    return createShadowPipeline();
}

void TreeSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                       VkImageView shadowMapView, VkSampler shadowSampler,
                                       const std::vector<VkBuffer>& windBuffers,
                                       const std::vector<VkBuffer>& lightBuffersParam,
                                       VkImageView terrainHeightMapViewParam, VkSampler terrainHeightMapSamplerParam) {
    this->terrainHeightMapView = terrainHeightMapViewParam;
    this->terrainHeightMapSampler = terrainHeightMapSamplerParam;

    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = rendererUniformBuffers[0];
        uboInfo.offset = 0;
        uboInfo.range = 160;

        VkDescriptorBufferInfo branchBufferInfo{};
        branchBufferInfo.buffer = branchBuffers.buffers[set];
        branchBufferInfo.offset = 0;
        branchBufferInfo.range = sizeof(BranchInstance) * MAX_BRANCHES;

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = shadowMapView;
        shadowImageInfo.sampler = shadowSampler;

        VkDescriptorBufferInfo windBufferInfo{};
        windBufferInfo.buffer = windBuffers[0];
        windBufferInfo.offset = 0;
        windBufferInfo.range = 32;

        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffersParam[0];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 5> graphicsWrites{};

        graphicsWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[0].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[0].dstBinding = 0;
        graphicsWrites[0].dstArrayElement = 0;
        graphicsWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[0].descriptorCount = 1;
        graphicsWrites[0].pBufferInfo = &uboInfo;

        graphicsWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[1].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[1].dstBinding = 1;
        graphicsWrites[1].dstArrayElement = 0;
        graphicsWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[1].descriptorCount = 1;
        graphicsWrites[1].pBufferInfo = &branchBufferInfo;

        graphicsWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[2].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[2].dstBinding = 2;
        graphicsWrites[2].dstArrayElement = 0;
        graphicsWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphicsWrites[2].descriptorCount = 1;
        graphicsWrites[2].pImageInfo = &shadowImageInfo;

        graphicsWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[3].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[3].dstBinding = 3;
        graphicsWrites[3].dstArrayElement = 0;
        graphicsWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[3].descriptorCount = 1;
        graphicsWrites[3].pBufferInfo = &windBufferInfo;

        graphicsWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[4].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[4].dstBinding = 4;
        graphicsWrites[4].dstArrayElement = 0;
        graphicsWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[4].descriptorCount = 1;
        graphicsWrites[4].pBufferInfo = &lightBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(graphicsWrites.size()),
                               graphicsWrites.data(), 0, nullptr);

        // Update shadow descriptor sets
        std::array<VkWriteDescriptorSet, 3> shadowWrites{};

        shadowWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[0].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[0].dstBinding = 0;
        shadowWrites[0].dstArrayElement = 0;
        shadowWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowWrites[0].descriptorCount = 1;
        shadowWrites[0].pBufferInfo = &uboInfo;

        shadowWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[1].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[1].dstBinding = 1;
        shadowWrites[1].dstArrayElement = 0;
        shadowWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        shadowWrites[1].descriptorCount = 1;
        shadowWrites[1].pBufferInfo = &branchBufferInfo;

        shadowWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[2].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[2].dstBinding = 2;
        shadowWrites[2].dstArrayElement = 0;
        shadowWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowWrites[2].descriptorCount = 1;
        shadowWrites[2].pBufferInfo = &windBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(shadowWrites.size()),
                               shadowWrites.data(), 0, nullptr);
    }
}

void TreeSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                 float terrainSize, float terrainHeightScale, float time) {
    // Upload tree data if needed
    if (treesNeedUpload) {
        uploadTreeData();
        treesNeedUpload = false;
    }

    TreeUniforms uniforms{};
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    glm::mat4 m = glm::transpose(viewProj);
    uniforms.frustumPlanes[0] = m[3] + m[0];
    uniforms.frustumPlanes[1] = m[3] - m[0];
    uniforms.frustumPlanes[2] = m[3] + m[1];
    uniforms.frustumPlanes[3] = m[3] - m[1];
    uniforms.frustumPlanes[4] = m[3] + m[2];
    uniforms.frustumPlanes[5] = m[3] - m[2];

    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        if (len > 0.0001f) {
            uniforms.frustumPlanes[i] /= len;
        }
    }

    uniforms.maxDrawDistance = 200.0f;
    uniforms.lodTransitionStart = 100.0f;
    uniforms.lodTransitionEnd = 200.0f;
    uniforms.terrainSize = terrainSize;
    uniforms.terrainHeightScale = terrainHeightScale;
    uniforms.time = time;
    uniforms.treeCount = static_cast<uint32_t>(trees.size());

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(TreeUniforms));
}

void TreeSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    if (trees.empty()) return;

    uint32_t writeSet = particleSystem.getComputeBufferSet();

    // Update compute descriptor set with terrain heightmap
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = uniformBuffers.buffers[frameIndex];
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(TreeUniforms);

    VkDescriptorImageInfo heightMapInfo{};
    heightMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    heightMapInfo.imageView = terrainHeightMapView;
    heightMapInfo.sampler = terrainHeightMapSampler;

    std::array<VkWriteDescriptorSet, 2> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[0].dstBinding = 2;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uniformBufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[1].dstBinding = 5;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &heightMapInfo;

    vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Reset indirect buffer
    vkCmdFillBuffer(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand), 0);

    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    // Dispatch compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = particleSystem.getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    TreePushConstants treePush{};
    treePush.time = time;
    vkCmdPushConstants(cmd, getComputePipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TreePushConstants), &treePush);

    // Dispatch one workgroup per tree
    uint32_t numWorkgroups = (static_cast<uint32_t>(trees.size()) + 63) / 64;
    vkCmdDispatch(cmd, numWorkgroups, 1, 1);

    // Memory barrier
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void TreeSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    if (trees.empty()) return;

    uint32_t readSet = particleSystem.getRenderBufferSet();
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getGraphicsPipelineHandles().pipeline);
    VkDescriptorSet graphicsSet = particleSystem.getGraphicsDescriptorSet(readSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                            &graphicsSet, 0, nullptr);

    TreePushConstants treePush{};
    treePush.time = time;
    vkCmdPushConstants(cmd, getGraphicsPipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TreePushConstants), &treePush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void TreeSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    if (trees.empty()) return;

    uint32_t readSet = particleSystem.getRenderBufferSet();
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipelineLayout, 0, 1,
                            &shadowDescriptorSetsDB[readSet], 0, nullptr);

    TreePushConstants treePush{};
    treePush.time = time;
    treePush.cascadeIndex = static_cast<int>(cascadeIndex);
    vkCmdPushConstants(cmd, shadowPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TreePushConstants), &treePush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void TreeSystem::advanceBufferSet() {
    particleSystem.advanceBufferSet();
}

void TreeSystem::addTree(const glm::vec3& position, float rotation, float scale, uint32_t definitionIndex) {
    if (trees.size() >= MAX_TREES) {
        SDL_Log("Warning: Maximum tree count reached");
        return;
    }

    TreeInstance tree{};
    tree.position = position;
    tree.rotation = rotation;
    tree.scale = scale;
    tree.age = 1.0f;  // Mature tree
    tree.definitionIndex = definitionIndex < definitions.size() ? definitionIndex : 0;

    // Generate hash from position
    tree.hash = fmodf(sinf(position.x * 127.1f + position.z * 311.7f) * 43758.5453f, 1.0f);
    if (tree.hash < 0.0f) tree.hash += 1.0f;

    trees.push_back(tree);
    treesNeedUpload = true;
}

void TreeSystem::clearTrees() {
    trees.clear();
    treesNeedUpload = true;
}

void TreeSystem::addTreeDefinition(const TreeDefinition& def) {
    if (definitions.size() >= MAX_DEFINITIONS) {
        SDL_Log("Warning: Maximum tree definition count reached");
        return;
    }
    definitions.push_back(def);

    // Upload definition to GPU
    if (definitionMappedPtr) {
        memcpy(definitionMappedPtr, definitions.data(), sizeof(TreeDefinition) * definitions.size());
    }
}

void TreeSystem::setDefaultTreeDefinition() {
    definitions.clear();

    TreeDefinition def{};
    // Trunk parameters
    def.trunkHeight = 8.0f;
    def.trunkRadius = 0.3f;
    def.trunkTaper = 0.6f;
    def.trunkBend = 0.3f;

    // Branching parameters
    def.branchLevels = 2;
    def.branchAngle = 0.8f;  // ~45 degrees
    def.branchSpread = 1.2f;
    def.branchLengthRatio = 0.6f;
    def.branchRadiusRatio = 0.5f;
    def.branchesPerLevel = 4;

    // Canopy parameters
    def.canopyCenter = glm::vec3(0.0f, 2.0f, 0.0f);
    def.canopyExtent = glm::vec3(3.0f, 2.0f, 3.0f);
    def.leafDensity = 100.0f;
    def.leafSize = 0.1f;
    def.leafSizeVariance = 0.3f;

    // Animation parameters
    def.windInfluence = 1.0f;
    def.branchStiffness = 0.5f;

    // Visual parameters
    def.leafPaletteIndex = 0;
    def.barkTextureIndex = 0;

    addTreeDefinition(def);
}

void TreeSystem::uploadTreeData() {
    if (treeInstanceMappedPtr && !trees.empty()) {
        memcpy(treeInstanceMappedPtr, trees.data(), sizeof(TreeInstance) * trees.size());
    }
}
