#include "GrassSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>  // for std::swap

// Forward declare UniformBufferObject size (needed for descriptor set update)
struct UniformBufferObject;

bool GrassSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createComputePipeline()) return false;
    if (!createGraphicsDescriptorSetLayout()) return false;
    if (!createGraphicsPipeline()) return false;
    if (!createShadowPipeline()) return false;
    if (!createDescriptorSets()) return false;

    return true;
}

void GrassSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    vkDestroyPipeline(dev, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(dev, shadowPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, shadowDescriptorSetLayout, nullptr);
    vkDestroyPipeline(dev, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(dev, graphicsPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, graphicsDescriptorSetLayout, nullptr);
    vkDestroyPipeline(dev, computePipeline, nullptr);
    vkDestroyPipelineLayout(dev, computePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, computeDescriptorSetLayout, nullptr);

    // Destroy double-buffered instance and indirect buffers
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        vmaDestroyBuffer(alloc, instanceBuffers[set], instanceAllocations[set]);
        vmaDestroyBuffer(alloc, indirectBuffers[set], indirectAllocations[set]);
    }

    // Destroy uniform buffers (not double-buffered)
    for (size_t i = 0; i < framesInFlight; i++) {
        vmaDestroyBuffer(alloc, uniformBuffers[i], uniformAllocations[i]);
    }
}

bool GrassSystem::createBuffers() {
    // Uniform buffers (per-frame, for culling params that change each frame)
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    VkDeviceSize instanceBufferSize = sizeof(GrassInstance) * MAX_INSTANCES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(GrassUniforms);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    // Create double-buffered instance and indirect buffers (one per set, not per frame)
    // The set alternation provides isolation: compute writes to A while graphics reads from B
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Instance buffer - written by compute, read by vertex shader
        VkBufferCreateInfo instanceBufferInfo{};
        instanceBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        instanceBufferInfo.size = instanceBufferSize;
        instanceBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        instanceBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(allocator, &instanceBufferInfo, &allocInfo,
                           &instanceBuffers[set], &instanceAllocations[set],
                           nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create grass instance buffer (set %u)", set);
            return false;
        }

        // Indirect buffer - written by compute, read by vkCmdDrawIndirect, cleared by vkCmdFillBuffer
        VkBufferCreateInfo indirectBufferInfo{};
        indirectBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indirectBufferInfo.size = indirectBufferSize;
        indirectBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        indirectBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(allocator, &indirectBufferInfo, &allocInfo,
                           &indirectBuffers[set], &indirectAllocations[set],
                           nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create grass indirect buffer (set %u)", set);
            return false;
        }
    }

    // Create uniform buffers (not double-buffered)
    for (size_t i = 0; i < framesInFlight; i++) {
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
        if (vmaCreateBuffer(allocator, &uniformBufferInfo, &uniformAllocInfo,
                           &uniformBuffers[i], &uniformAllocations[i],
                           &uniformAllocInfoResult) != VK_SUCCESS) {
            SDL_Log("Failed to create grass uniform buffer");
            return false;
        }
        uniformMappedPtrs[i] = uniformAllocInfoResult.pMappedData;
    }

    return true;
}

bool GrassSystem::createComputeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Instance buffer (output)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Indirect buffer (output)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Grass uniforms (culling parameters)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &computeDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass compute descriptor set layout");
        return false;
    }

    return true;
}

bool GrassSystem::createComputePipeline() {
    auto compShaderCode = ShaderLoader::readFile(shaderPath + "/grass.comp.spv");
    if (compShaderCode.empty()) {
        SDL_Log("Failed to load grass compute shader");
        return false;
    }

    VkShaderModule compShaderModule = ShaderLoader::createShaderModule(device, compShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = compShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GrassPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                               &computePipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass compute pipeline layout");
        vkDestroyShaderModule(device, compShaderModule, nullptr);
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = computePipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr,
                                               &computePipeline);

    vkDestroyShaderModule(device, compShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create grass compute pipeline");
        return false;
    }

    return true;
}

bool GrassSystem::createGraphicsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

    // UBO (same as main pipeline)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Instance buffer (read-only in vertex shader)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Shadow map sampler (for receiving shadows)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    // Wind uniform buffer (for vertex shader wind animation)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &graphicsDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass graphics descriptor set layout");
        return false;
    }

    return true;
}

bool GrassSystem::createGraphicsPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/grass.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/grass.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load grass shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

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
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for grass
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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GrassPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &graphicsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &graphicsPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass graphics pipeline layout");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
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
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr,
                                                &graphicsPipeline);

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create grass graphics pipeline");
        return false;
    }

    return true;
}

bool GrassSystem::createShadowPipeline() {
    // Shadow descriptor set layout: UBO (binding 0) + instance buffer (binding 1) + wind (binding 2)
    std::array<VkDescriptorSetLayoutBinding, 3> shadowBindings{};

    // UBO for light space matrix
    shadowBindings[0].binding = 0;
    shadowBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowBindings[0].descriptorCount = 1;
    shadowBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Instance buffer
    shadowBindings[1].binding = 1;
    shadowBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowBindings[1].descriptorCount = 1;
    shadowBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Wind uniform buffer (for consistent wind animation in shadows)
    shadowBindings[2].binding = 2;
    shadowBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowBindings[2].descriptorCount = 1;
    shadowBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo shadowLayoutInfo{};
    shadowLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    shadowLayoutInfo.bindingCount = static_cast<uint32_t>(shadowBindings.size());
    shadowLayoutInfo.pBindings = shadowBindings.data();

    if (vkCreateDescriptorSetLayout(device, &shadowLayoutInfo, nullptr,
                                    &shadowDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass shadow descriptor set layout");
        return false;
    }

    // Load shadow shaders
    auto vertShaderCode = ShaderLoader::readFile(shaderPath + "/grass_shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(shaderPath + "/grass_shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load grass shadow shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

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

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for grass
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

    // No color attachment for shadow pass
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GrassPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create grass shadow pipeline layout");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
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
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr,
                                                &shadowPipeline);

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create grass shadow pipeline");
        return false;
    }

    return true;
}

bool GrassSystem::createDescriptorSets() {
    // Allocate and update descriptor sets for both buffer sets (A and B)
    // We only need one descriptor set per buffer set for compute (no per-frame needed)
    // The uniform buffer binding will use dynamic offset or be updated each frame
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Allocate compute descriptor set for this buffer set
        VkDescriptorSetAllocateInfo computeAllocInfo{};
        computeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        computeAllocInfo.descriptorPool = descriptorPool;
        computeAllocInfo.descriptorSetCount = 1;
        computeAllocInfo.pSetLayouts = &computeDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &computeAllocInfo, &computeDescriptorSets[set]) != VK_SUCCESS) {
            SDL_Log("Failed to allocate grass compute descriptor set (set %u)", set);
            return false;
        }

        // Allocate graphics descriptor set for this buffer set
        VkDescriptorSetAllocateInfo graphicsAllocInfo{};
        graphicsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        graphicsAllocInfo.descriptorPool = descriptorPool;
        graphicsAllocInfo.descriptorSetCount = 1;
        graphicsAllocInfo.pSetLayouts = &graphicsDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &graphicsAllocInfo, &graphicsDescriptorSets[set]) != VK_SUCCESS) {
            SDL_Log("Failed to allocate grass graphics descriptor set (set %u)", set);
            return false;
        }

        // Allocate shadow descriptor set for this buffer set
        VkDescriptorSetAllocateInfo shadowAllocInfo{};
        shadowAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        shadowAllocInfo.descriptorPool = descriptorPool;
        shadowAllocInfo.descriptorSetCount = 1;
        shadowAllocInfo.pSetLayouts = &shadowDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &shadowAllocInfo, &shadowDescriptorSetsDB[set]) != VK_SUCCESS) {
            SDL_Log("Failed to allocate grass shadow descriptor set (set %u)", set);
            return false;
        }

        // Update compute descriptor sets (instance and indirect buffers only)
        // Uniform buffer will be updated in updateDescriptorSets with the right frame's buffer
        VkDescriptorBufferInfo instanceBufferInfo{};
        instanceBufferInfo.buffer = instanceBuffers[set];
        instanceBufferInfo.offset = 0;
        instanceBufferInfo.range = sizeof(GrassInstance) * MAX_INSTANCES;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers[set];
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = sizeof(VkDrawIndirectCommand);

        // Use first frame's uniform buffer initially; will be updated per-frame
        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers[0];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(GrassUniforms);

        std::array<VkWriteDescriptorSet, 3> computeWrites{};

        computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[0].dstSet = computeDescriptorSets[set];
        computeWrites[0].dstBinding = 0;
        computeWrites[0].dstArrayElement = 0;
        computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[0].descriptorCount = 1;
        computeWrites[0].pBufferInfo = &instanceBufferInfo;

        computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[1].dstSet = computeDescriptorSets[set];
        computeWrites[1].dstBinding = 1;
        computeWrites[1].dstArrayElement = 0;
        computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[1].descriptorCount = 1;
        computeWrites[1].pBufferInfo = &indirectBufferInfo;

        computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[2].dstSet = computeDescriptorSets[set];
        computeWrites[2].dstBinding = 2;
        computeWrites[2].dstArrayElement = 0;
        computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[2].descriptorCount = 1;
        computeWrites[2].pBufferInfo = &uniformBufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWrites.size()),
                               computeWrites.data(), 0, nullptr);
    }

    return true;
}

void GrassSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
                                        VkImageView shadowMapView, VkSampler shadowSampler,
                                        const std::vector<VkBuffer>& windBuffers) {
    // Update graphics and shadow descriptor sets for both buffer sets (A and B)
    // Note: We use the first frame's UBO/wind buffer since they're updated each frame anyway
    // For proper per-frame handling, we'd need dynamic offsets or per-frame descriptor sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Use first frame's uniform buffers (they're updated before each draw anyway)
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = rendererUniformBuffers[0];
        uboInfo.offset = 0;
        uboInfo.range = 160;  // sizeof(UniformBufferObject) - matches Renderer's UBO

        VkDescriptorBufferInfo instanceBufferInfo{};
        instanceBufferInfo.buffer = instanceBuffers[set];
        instanceBufferInfo.offset = 0;
        instanceBufferInfo.range = sizeof(GrassInstance) * MAX_INSTANCES;

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = shadowMapView;
        shadowImageInfo.sampler = shadowSampler;

        VkDescriptorBufferInfo windBufferInfo{};
        windBufferInfo.buffer = windBuffers[0];
        windBufferInfo.offset = 0;
        windBufferInfo.range = 32;  // sizeof(WindUniforms) - 2 vec4s

        std::array<VkWriteDescriptorSet, 4> graphicsWrites{};

        graphicsWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[0].dstSet = graphicsDescriptorSets[set];
        graphicsWrites[0].dstBinding = 0;
        graphicsWrites[0].dstArrayElement = 0;
        graphicsWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[0].descriptorCount = 1;
        graphicsWrites[0].pBufferInfo = &uboInfo;

        graphicsWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[1].dstSet = graphicsDescriptorSets[set];
        graphicsWrites[1].dstBinding = 1;
        graphicsWrites[1].dstArrayElement = 0;
        graphicsWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[1].descriptorCount = 1;
        graphicsWrites[1].pBufferInfo = &instanceBufferInfo;

        graphicsWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[2].dstSet = graphicsDescriptorSets[set];
        graphicsWrites[2].dstBinding = 2;
        graphicsWrites[2].dstArrayElement = 0;
        graphicsWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphicsWrites[2].descriptorCount = 1;
        graphicsWrites[2].pImageInfo = &shadowImageInfo;

        graphicsWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[3].dstSet = graphicsDescriptorSets[set];
        graphicsWrites[3].dstBinding = 3;
        graphicsWrites[3].dstArrayElement = 0;
        graphicsWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[3].descriptorCount = 1;
        graphicsWrites[3].pBufferInfo = &windBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(graphicsWrites.size()),
                               graphicsWrites.data(), 0, nullptr);

        // Update shadow descriptor sets (UBO + instance buffer + wind buffer)
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
        shadowWrites[1].pBufferInfo = &instanceBufferInfo;

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

void GrassSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    GrassUniforms uniforms{};

    // Camera position
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    // Each plane equation: ax + by + cz + d = 0
    // Using row-major extraction (GLM is column-major, so we transpose conceptually)
    glm::mat4 m = glm::transpose(viewProj);

    // Left:   row3 + row0
    uniforms.frustumPlanes[0] = m[3] + m[0];
    // Right:  row3 - row0
    uniforms.frustumPlanes[1] = m[3] - m[0];
    // Bottom: row3 + row1
    uniforms.frustumPlanes[2] = m[3] + m[1];
    // Top:    row3 - row1
    uniforms.frustumPlanes[3] = m[3] - m[1];
    // Near:   row3 + row2
    uniforms.frustumPlanes[4] = m[3] + m[2];
    // Far:    row3 - row2
    uniforms.frustumPlanes[5] = m[3] - m[2];

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        if (len > 0.0001f) {
            uniforms.frustumPlanes[i] /= len;
        }
    }

    // Distance thresholds
    uniforms.maxDrawDistance = 50.0f;
    uniforms.lodTransitionStart = 30.0f;
    uniforms.lodTransitionEnd = 50.0f;
    uniforms.padding = 0.0f;

    // Copy to mapped buffer
    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(GrassUniforms));
}

void GrassSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: compute writes to computeBufferSet
    uint32_t writeSet = computeBufferSet;

    // Update compute descriptor set to use this frame's uniform buffer
    // (uniforms contain per-frame camera/frustum data)
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = uniformBuffers[frameIndex];
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(GrassUniforms);

    VkWriteDescriptorSet uniformWrite{};
    uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uniformWrite.dstSet = computeDescriptorSets[writeSet];
    uniformWrite.dstBinding = 2;
    uniformWrite.dstArrayElement = 0;
    uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite.descriptorCount = 1;
    uniformWrite.pBufferInfo = &uniformBufferInfo;

    vkUpdateDescriptorSets(device, 1, &uniformWrite, 0, nullptr);

    // Reset indirect buffer before compute dispatch to prevent accumulation
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

    // Dispatch grass compute shader using the compute buffer set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout, 0, 1,
                            &computeDescriptorSets[writeSet], 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    // Dispatch: ceil(1,000,000 / 64) = 15,625 workgroups (1000x1000 grid)
    vkCmdDispatch(cmd, 15625, 1, 1);

    // Memory barrier: compute write -> vertex shader read (storage buffer) and indirect read
    // Note: This barrier ensures the compute results are visible when we draw from this buffer
    // in the NEXT frame (after advanceBufferSet swaps the sets)
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void GrassSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: graphics reads from renderBufferSet
    // On first frame before double-buffering kicks in, read from compute set (same as renderBufferSet)
    uint32_t readSet = renderBufferSet;

    // Bootstrap: if we haven't diverged yet, read from what we just computed
    if (computeBufferSet == renderBufferSet) {
        readSet = computeBufferSet;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphicsPipelineLayout, 0, 1,
                            &graphicsDescriptorSets[readSet], 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, graphicsPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    // Double-buffer: shadow pass reads from renderBufferSet (same as main draw)
    uint32_t readSet = renderBufferSet;

    // Bootstrap: if we haven't diverged yet, read from what we just computed
    if (computeBufferSet == renderBufferSet) {
        readSet = computeBufferSet;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipelineLayout, 0, 1,
                            &shadowDescriptorSetsDB[readSet], 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.cascadeIndex = static_cast<int>(cascadeIndex);
    vkCmdPushConstants(cmd, shadowPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::advanceBufferSet() {
    // Swap compute and render buffer sets for next frame
    // After this call:
    // - computeBufferSet points to what was the render set (now safe to overwrite)
    // - renderBufferSet points to what was the compute set (now contains fresh data)
    //
    // Bootstrap case: on frame 0, both are 0 (same buffer used sequentially)
    // After first call, we diverge to true double-buffering
    if (computeBufferSet == renderBufferSet) {
        // First frame done - set up for double buffering
        // renderBufferSet stays at 0 (what we just computed)
        // computeBufferSet moves to 1 (next frame will compute here)
        computeBufferSet = 1;
    } else {
        std::swap(computeBufferSet, renderBufferSet);
    }
}
