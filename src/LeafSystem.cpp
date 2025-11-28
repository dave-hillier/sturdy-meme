#include "LeafSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

bool LeafSystem::init(const InitInfo& info) {
    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = [this]() { return createGraphicsDescriptorSetLayout(); };
    hooks.createGraphicsPipeline = [this]() { return createGraphicsPipeline(); };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    return particleSystem.init(info, hooks, BUFFER_SET_COUNT);
}

void LeafSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    particleSystem.destroy(dev, alloc);
}

void LeafSystem::destroyBuffers(VmaAllocator alloc) {
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        vmaDestroyBuffer(alloc, particleBuffers[set], particleAllocations[set]);
        vmaDestroyBuffer(alloc, indirectBuffers[set], indirectAllocations[set]);
    }

    for (size_t i = 0; i < getFramesInFlight(); i++) {
        vmaDestroyBuffer(alloc, uniformBuffers[i], uniformAllocations[i]);
        vmaDestroyBuffer(alloc, displacementRegionBuffers[i], displacementRegionAllocations[i]);
    }
}

bool LeafSystem::createBuffers() {
    uniformBuffers.resize(getFramesInFlight());
    uniformAllocations.resize(getFramesInFlight());
    uniformMappedPtrs.resize(getFramesInFlight());

    VkDeviceSize particleBufferSize = sizeof(LeafParticle) * MAX_PARTICLES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(LeafUniforms);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    // Create double-buffered particle and indirect buffers
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Particle buffer - storage buffer for compute read/write and vertex shader read
        VkBufferCreateInfo particleBufferInfo{};
        particleBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        particleBufferInfo.size = particleBufferSize;
        particleBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        particleBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(getAllocator(), &particleBufferInfo, &allocInfo,
                           &particleBuffers[set], &particleAllocations[set],
                           nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create leaf particle buffer (set %u)", set);
            return false;
        }

        // Indirect buffer - for indirect drawing
        VkBufferCreateInfo indirectBufferInfo{};
        indirectBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indirectBufferInfo.size = indirectBufferSize;
        indirectBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        indirectBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(getAllocator(), &indirectBufferInfo, &allocInfo,
                           &indirectBuffers[set], &indirectAllocations[set],
                           nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create leaf indirect buffer (set %u)", set);
            return false;
        }
    }

    // Create uniform buffers (per-frame)
    for (size_t i = 0; i < getFramesInFlight(); i++) {
        VkBufferCreateInfo uniformBufferInfo{};
        uniformBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uniformBufferInfo.size = uniformBufferSize;
        uniformBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uniformBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo uniformAllocInfo{};
        uniformAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        uniformAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                 VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo uniformAllocInfoResult;
        if (vmaCreateBuffer(getAllocator(), &uniformBufferInfo, &uniformAllocInfo,
                           &uniformBuffers[i], &uniformAllocations[i],
                           &uniformAllocInfoResult) != VK_SUCCESS) {
            SDL_Log("Failed to create leaf uniform buffer");
            return false;
        }
        uniformMappedPtrs[i] = uniformAllocInfoResult.pMappedData;
    }

    // Create displacement region uniform buffers (per-frame)
    displacementRegionBuffers.resize(getFramesInFlight());
    displacementRegionAllocations.resize(getFramesInFlight());
    displacementRegionMappedPtrs.resize(getFramesInFlight());

    VkDeviceSize dispRegionBufferSize = sizeof(glm::vec4);  // regionCenterAndSize

    for (size_t i = 0; i < getFramesInFlight(); i++) {
        VkBufferCreateInfo dispRegionBufferInfo{};
        dispRegionBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dispRegionBufferInfo.size = dispRegionBufferSize;
        dispRegionBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        dispRegionBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo dispRegionAllocInfo{};
        dispRegionAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        dispRegionAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo dispRegionAllocResult;
        if (vmaCreateBuffer(getAllocator(), &dispRegionBufferInfo, &dispRegionAllocInfo,
                           &displacementRegionBuffers[i], &displacementRegionAllocations[i],
                           &dispRegionAllocResult) != VK_SUCCESS) {
            SDL_Log("Failed to create leaf displacement region buffer");
            return false;
        }
        displacementRegionMappedPtrs[i] = dispRegionAllocResult.pMappedData;
    }

    return true;
}

bool LeafSystem::createComputeDescriptorSetLayout() {
    // Particle buffer input (previous frame state)
    auto particleBufferInput = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Particle buffer output (current frame result)
    auto particleBufferOutput = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Indirect buffer (output)
    auto indirectBuffer = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Leaf uniforms
    auto leafUniforms = BindingBuilder()
        .setBinding(3)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Wind uniforms
    auto windUniforms = BindingBuilder()
        .setBinding(4)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Terrain heightmap
    auto terrainHeightmap = BindingBuilder()
        .setBinding(5)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Displacement map (shared with grass system for player interaction)
    auto displacementMap = BindingBuilder()
        .setBinding(6)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Displacement region uniform buffer
    auto displacementRegion = BindingBuilder()
        .setBinding(7)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 8> bindings = {
        particleBufferInput, particleBufferOutput, indirectBuffer,
        leafUniforms, windUniforms, terrainHeightmap, displacementMap, displacementRegion
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(getDevice(), &layoutInfo, nullptr,
                                    &getComputePipelineHandles().descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create leaf compute descriptor set layout");
        return false;
    }

    return true;
}

bool LeafSystem::createComputePipeline() {
    auto compShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.comp.spv");
    if (compShaderCode.empty()) {
        SDL_Log("Failed to load leaf compute shader");
        return false;
    }

    VkShaderModule compShaderModule = ShaderLoader::createShaderModule(getDevice(), compShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = compShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LeafPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &getComputePipelineHandles().descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(getDevice(), &layoutInfo, nullptr,
                               &getComputePipelineHandles().pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create leaf compute pipeline layout");
        vkDestroyShaderModule(getDevice(), compShaderModule, nullptr);
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = getComputePipelineHandles().pipelineLayout;

    VkResult result = vkCreateComputePipelines(getDevice(), VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr,
                                               &getComputePipelineHandles().pipeline);

    vkDestroyShaderModule(getDevice(), compShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create leaf compute pipeline");
        return false;
    }

    return true;
}

bool LeafSystem::createGraphicsDescriptorSetLayout() {
    // UBO (scene uniforms)
    auto uboBinding = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // Particle buffer (read-only in vertex shader)
    auto particleBuffer = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    // Wind uniforms (for consistent animation)
    auto windUniforms = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboBinding, particleBuffer, windUniforms};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(getDevice(), &layoutInfo, nullptr,
                                    &getGraphicsPipelineHandles().descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create leaf graphics descriptor set layout");
        return false;
    }

    return true;
}

bool LeafSystem::createGraphicsPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(getShaderPath() + "/leaf.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load leaf shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(getDevice(), fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // No vertex input - procedural geometry from instance buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for leaves (visible from both sides)
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;  // Write depth for proper sorting
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Alpha blending for leaf edges
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LeafPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &getGraphicsPipelineHandles().descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(getDevice(), &pipelineLayoutInfo, nullptr,
                               &getGraphicsPipelineHandles().pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create leaf graphics pipeline layout");
        vkDestroyShaderModule(getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(getDevice(), vertShaderModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = getGraphicsPipelineHandles().pipelineLayout;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(getDevice(), VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr,
                                                &getGraphicsPipelineHandles().pipeline);

    vkDestroyShaderModule(getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(getDevice(), vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create leaf graphics pipeline");
        return false;
    }

    return true;
}

bool LeafSystem::createDescriptorSets() {
    // Allocate descriptor sets for both buffer sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Compute descriptor set
        VkDescriptorSetAllocateInfo computeAllocInfo{};
        computeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        computeAllocInfo.descriptorPool = getDescriptorPool();
        computeAllocInfo.descriptorSetCount = 1;
        computeAllocInfo.pSetLayouts = &getComputePipelineHandles().descriptorSetLayout;

        VkDescriptorSet computeSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(getDevice(), &computeAllocInfo, &computeSet) != VK_SUCCESS) {
            SDL_Log("Failed to allocate leaf compute descriptor set (set %u)", set);
            return false;
        }
        particleSystem.setComputeDescriptorSet(set, computeSet);

        // Graphics descriptor set
        VkDescriptorSetAllocateInfo graphicsAllocInfo{};
        graphicsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        graphicsAllocInfo.descriptorPool = getDescriptorPool();
        graphicsAllocInfo.descriptorSetCount = 1;
        graphicsAllocInfo.pSetLayouts = &getGraphicsPipelineHandles().descriptorSetLayout;

        VkDescriptorSet graphicsSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(getDevice(), &graphicsAllocInfo, &graphicsSet) != VK_SUCCESS) {
            SDL_Log("Failed to allocate leaf graphics descriptor set (set %u)", set);
            return false;
        }
        particleSystem.setGraphicsDescriptorSet(set, graphicsSet);
    }

    return true;
}

void LeafSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                       const std::vector<VkBuffer>& windBuffers,
                                       VkImageView terrainHeightMapView,
                                       VkSampler terrainHeightMapSampler,
                                       VkImageView displacementMapViewParam,
                                       VkSampler displacementMapSamplerParam) {
    // Store displacement texture references
    this->displacementMapView = displacementMapViewParam;
    this->displacementMapSampler = displacementMapSamplerParam;

    // Update compute and graphics descriptor sets for both buffer sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        uint32_t inputSet = (set == 0) ? 1 : 0;  // Read from opposite buffer
        uint32_t outputSet = set;

        // Compute descriptor set writes
        VkDescriptorBufferInfo inputParticleBufferInfo{};
        inputParticleBufferInfo.buffer = particleBuffers[inputSet];
        inputParticleBufferInfo.offset = 0;
        inputParticleBufferInfo.range = sizeof(LeafParticle) * MAX_PARTICLES;

        VkDescriptorBufferInfo outputParticleBufferInfo{};
        outputParticleBufferInfo.buffer = particleBuffers[outputSet];
        outputParticleBufferInfo.offset = 0;
        outputParticleBufferInfo.range = sizeof(LeafParticle) * MAX_PARTICLES;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers[outputSet];
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = sizeof(VkDrawIndirectCommand);

        VkDescriptorBufferInfo leafUniformInfo{};
        leafUniformInfo.buffer = uniformBuffers[0];
        leafUniformInfo.offset = 0;
        leafUniformInfo.range = sizeof(LeafUniforms);

        VkDescriptorBufferInfo windBufferInfo{};
        windBufferInfo.buffer = windBuffers[0];
        windBufferInfo.offset = 0;
        windBufferInfo.range = 32;  // sizeof(WindUniforms)

        VkDescriptorImageInfo heightMapInfo{};
        heightMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        heightMapInfo.imageView = terrainHeightMapView;
        heightMapInfo.sampler = terrainHeightMapSampler;

        VkDescriptorImageInfo displacementMapInfo{};
        displacementMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        displacementMapInfo.imageView = displacementMapViewParam;
        displacementMapInfo.sampler = displacementMapSamplerParam;

        VkDescriptorBufferInfo dispRegionInfo{};
        dispRegionInfo.buffer = displacementRegionBuffers[0];
        dispRegionInfo.offset = 0;
        dispRegionInfo.range = sizeof(glm::vec4);

        std::array<VkWriteDescriptorSet, 8> computeWrites{};

        computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[0].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[0].dstBinding = 0;
        computeWrites[0].dstArrayElement = 0;
        computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[0].descriptorCount = 1;
        computeWrites[0].pBufferInfo = &inputParticleBufferInfo;

        computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[1].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[1].dstBinding = 1;
        computeWrites[1].dstArrayElement = 0;
        computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[1].descriptorCount = 1;
        computeWrites[1].pBufferInfo = &outputParticleBufferInfo;

        computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[2].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[2].dstBinding = 2;
        computeWrites[2].dstArrayElement = 0;
        computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[2].descriptorCount = 1;
        computeWrites[2].pBufferInfo = &indirectBufferInfo;

        computeWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[3].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[3].dstBinding = 3;
        computeWrites[3].dstArrayElement = 0;
        computeWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[3].descriptorCount = 1;
        computeWrites[3].pBufferInfo = &leafUniformInfo;

        computeWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[4].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[4].dstBinding = 4;
        computeWrites[4].dstArrayElement = 0;
        computeWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[4].descriptorCount = 1;
        computeWrites[4].pBufferInfo = &windBufferInfo;

        computeWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[5].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[5].dstBinding = 5;
        computeWrites[5].dstArrayElement = 0;
        computeWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        computeWrites[5].descriptorCount = 1;
        computeWrites[5].pImageInfo = &heightMapInfo;

        computeWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[6].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[6].dstBinding = 6;
        computeWrites[6].dstArrayElement = 0;
        computeWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        computeWrites[6].descriptorCount = 1;
        computeWrites[6].pImageInfo = &displacementMapInfo;

        computeWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[7].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[7].dstBinding = 7;
        computeWrites[7].dstArrayElement = 0;
        computeWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[7].descriptorCount = 1;
        computeWrites[7].pBufferInfo = &dispRegionInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(computeWrites.size()),
                               computeWrites.data(), 0, nullptr);

        // Graphics descriptor set writes
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = rendererUniformBuffers[0];
        uboInfo.offset = 0;
        uboInfo.range = 320;  // sizeof(UniformBufferObject)

        VkDescriptorBufferInfo particleBufferInfo{};
        particleBufferInfo.buffer = particleBuffers[set];
        particleBufferInfo.offset = 0;
        particleBufferInfo.range = sizeof(LeafParticle) * MAX_PARTICLES;

        std::array<VkWriteDescriptorSet, 3> graphicsWrites{};

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
        graphicsWrites[1].pBufferInfo = &particleBufferInfo;

        graphicsWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[2].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[2].dstBinding = 2;
        graphicsWrites[2].dstArrayElement = 0;
        graphicsWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[2].descriptorCount = 1;
        graphicsWrites[2].pBufferInfo = &windBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(graphicsWrites.size()),
                               graphicsWrites.data(), 0, nullptr);
    }
}

void LeafSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                 const glm::mat4& viewProj, const glm::vec3& playerPos,
                                 const glm::vec3& playerVel, float deltaTime, float totalTime,
                                 float terrainSize, float terrainHeightScale) {
    LeafUniforms uniforms{};
    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings ? *environmentSettings : fallbackSettings;

    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    glm::mat4 m = glm::transpose(viewProj);
    uniforms.frustumPlanes[0] = m[3] + m[0];  // Left
    uniforms.frustumPlanes[1] = m[3] - m[0];  // Right
    uniforms.frustumPlanes[2] = m[3] + m[1];  // Bottom
    uniforms.frustumPlanes[3] = m[3] - m[1];  // Top
    uniforms.frustumPlanes[4] = m[3] + m[2];  // Near
    uniforms.frustumPlanes[5] = m[3] - m[2];  // Far

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        if (len > 0.0001f) {
            uniforms.frustumPlanes[i] /= len;
        }
    }

    // Player data for disruption
    uniforms.playerPosition = glm::vec4(playerPos, 0.5f);  // w = player collision radius
    float playerSpeed = glm::length(playerVel);
    uniforms.playerVelocity = glm::vec4(playerVel, playerSpeed);

    // Spawn region
    uniforms.spawnRegionMin = glm::vec4(spawnRegionMin, 0.0f);
    uniforms.spawnRegionMax = glm::vec4(spawnRegionMax, 0.0f);

    // Confetti spawn parameters
    uniforms.confettiSpawnPos = glm::vec4(confettiSpawnPosition, confettiConeAngle);
    uniforms.confettiSpawnCount = confettiToSpawn;
    uniforms.confettiVelocity = confettiSpawnVelocity;

    // General parameters
    uniforms.groundLevel = groundLevel;
    uniforms.deltaTime = deltaTime;
    uniforms.time = totalTime;
    uniforms.maxDrawDistance = 60.0f;

    // Disruption parameters
    uniforms.disruptionRadius = settings.leafDisruptionRadius;
    uniforms.disruptionStrength = settings.leafDisruptionStrength;
    uniforms.gustThreshold = settings.leafGustLiftThreshold;

    // Target counts based on intensity
    uniforms.targetFallingCount = leafIntensity * 5000.0f;   // 0-5000 falling leaves
    uniforms.targetGroundedCount = leafIntensity * 20000.0f; // 0-20000 grounded leaves

    // Terrain parameters
    uniforms.terrainSize = terrainSize;
    uniforms.terrainHeightScale = terrainHeightScale;

    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(LeafUniforms));

    // Update displacement region to follow camera (same as grass system)
    displacementRegionCenter = glm::vec2(cameraPos.x, cameraPos.z);

    // Update displacement region uniform buffer
    glm::vec4 dispRegionData(displacementRegionCenter.x, displacementRegionCenter.y,
                             DISPLACEMENT_REGION_SIZE, 0.0f);
    memcpy(displacementRegionMappedPtrs[frameIndex], &dispRegionData, sizeof(glm::vec4));

    // Reset confetti spawn count after it's been sent to GPU
    confettiToSpawn = 0.0f;
}

void LeafSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime) {
    uint32_t writeSet = particleSystem.getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform and displacement region buffers
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = uniformBuffers[frameIndex];
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(LeafUniforms);

    VkDescriptorBufferInfo dispRegionBufferInfo{};
    dispRegionBufferInfo.buffer = displacementRegionBuffers[frameIndex];
    dispRegionBufferInfo.offset = 0;
    dispRegionBufferInfo.range = sizeof(glm::vec4);

    std::array<VkWriteDescriptorSet, 2> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[0].dstBinding = 3;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uniformBufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[1].dstBinding = 7;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &dispRegionBufferInfo;

    vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Reset indirect buffer before compute dispatch
    vkCmdFillBuffer(cmd, indirectBuffers[writeSet], 0, sizeof(VkDrawIndirectCommand), 0);

    // Barrier to ensure fill completes before compute shader runs
    VkMemoryBarrier fillBarrier{};
    fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    // Dispatch leaf compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = particleSystem.getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = deltaTime;
    vkCmdPushConstants(cmd, getComputePipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LeafPushConstants), &pushConstants);

    // Dispatch: ceil(MAX_PARTICLES / WORKGROUP_SIZE) workgroups
    uint32_t workgroupCount = (MAX_PARTICLES + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);

    // Memory barrier: compute write -> vertex shader read and indirect read
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void LeafSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    uint32_t readSet = particleSystem.getRenderBufferSet();

    // Bootstrap: if we haven't diverged yet, read from compute set
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getGraphicsPipelineHandles().pipeline);
    VkDescriptorSet graphicsSet = particleSystem.getGraphicsDescriptorSet(readSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                            &graphicsSet, 0, nullptr);

    LeafPushConstants pushConstants{};
    pushConstants.time = time;
    pushConstants.deltaTime = 0.0f;  // Not needed for rendering
    vkCmdPushConstants(cmd, getGraphicsPipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(LeafPushConstants), &pushConstants);

    // Indirect draw: 4 vertices per leaf (quad)
    vkCmdDrawIndirect(cmd, indirectBuffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void LeafSystem::advanceBufferSet() { particleSystem.advanceBufferSet(); }
