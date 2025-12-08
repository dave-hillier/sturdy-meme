#ifdef JPH_DEBUG_RENDERER

#include "PhysicsDebugRenderer.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>

PhysicsDebugRenderer::~PhysicsDebugRenderer() {
    destroy();
}

bool PhysicsDebugRenderer::init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass,
                                 VkExtent2D extent, const std::string& shaderPath) {
    this->device = device;
    this->allocator = allocator;
    this->renderPass = renderPass;
    this->extent = extent;
    this->shaderPath = shaderPath;

    // Initialize base class (creates predefined geometry)
    Initialize();

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsDebugRenderer: Failed to create buffers");
        return false;
    }

    if (!createPipelines()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsDebugRenderer: Failed to create pipelines");
        return false;
    }

    initialized = true;
    SDL_Log("PhysicsDebugRenderer initialized (max %zu lines, %zu triangles)",
            MAX_LINES, MAX_TRIANGLES);
    return true;
}

void PhysicsDebugRenderer::destroy() {
    if (!device) return;

    if (linePipeline) vkDestroyPipeline(device, linePipeline, nullptr);
    if (trianglePipeline) vkDestroyPipeline(device, trianglePipeline, nullptr);
    if (linePipelineLayout) vkDestroyPipelineLayout(device, linePipelineLayout, nullptr);

    if (lineVertexBuffer) {
        vmaDestroyBuffer(allocator, lineVertexBuffer, lineVertexAllocation);
    }
    if (triangleVertexBuffer) {
        vmaDestroyBuffer(allocator, triangleVertexBuffer, triangleVertexAllocation);
    }

    linePipeline = VK_NULL_HANDLE;
    trianglePipeline = VK_NULL_HANDLE;
    linePipelineLayout = VK_NULL_HANDLE;
    lineVertexBuffer = VK_NULL_HANDLE;
    triangleVertexBuffer = VK_NULL_HANDLE;
    initialized = false;
}

bool PhysicsDebugRenderer::createBuffers() {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Line vertex buffer (2 vertices per line)
    bufferInfo.size = MAX_LINES * 2 * sizeof(DebugVertex);
    VmaAllocationInfo lineAllocInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &lineVertexBuffer, &lineVertexAllocation, &lineAllocInfo) != VK_SUCCESS) {
        return false;
    }
    lineVertexMapped = lineAllocInfo.pMappedData;

    // Triangle vertex buffer (3 vertices per triangle)
    bufferInfo.size = MAX_TRIANGLES * 3 * sizeof(DebugVertex);
    VmaAllocationInfo triAllocInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &triangleVertexBuffer, &triangleVertexAllocation, &triAllocInfo) != VK_SUCCESS) {
        return false;
    }
    triangleVertexMapped = triAllocInfo.pMappedData;

    return true;
}

bool PhysicsDebugRenderer::createPipelines() {
    // Load shaders
    VkShaderModule vertModule = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.vert.spv");
    VkShaderModule fragModule = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsDebugRenderer: Failed to load shaders");
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Pipeline layout with push constants for view-projection matrix
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &linePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Vertex input
    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(DebugVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDescs[2] = {};
    // Position
    attrDescs[0].binding = 0;
    attrDescs[0].location = 0;
    attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset = offsetof(DebugVertex, position);
    // Color
    attrDescs[1].binding = 0;
    attrDescs[1].location = 1;
    attrDescs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDescs[1].offset = offsetof(DebugVertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

    // Input assembly - lines
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil - test but no write (see through geometry)
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write depth
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (alpha blend for transparency)
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Create line pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = linePipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsDebugRenderer: Failed to create line pipeline");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Create triangle pipeline (same but with triangle topology)
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;  // Wireframe triangles

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsDebugRenderer: Failed to create triangle pipeline");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return true;
}

void PhysicsDebugRenderer::beginFrame(const glm::vec3& cameraPos) {
    // Set camera position for LOD selection
    SetCameraPos(JPH::RVec3(cameraPos.x, cameraPos.y, cameraPos.z));

    // Clear accumulated geometry
    lineVertices.clear();
    triangleVertices.clear();

    // Call base class to release unused batches
    NextFrame();
}

void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
    if (!enabled || lineVertices.size() >= MAX_LINES * 2) return;

    DebugVertex v1, v2;
    v1.position = glm::vec3(inFrom.GetX(), inFrom.GetY(), inFrom.GetZ());
    v1.color = glm::vec4(inColor.r / 255.0f, inColor.g / 255.0f, inColor.b / 255.0f, inColor.a / 255.0f);

    v2.position = glm::vec3(inTo.GetX(), inTo.GetY(), inTo.GetZ());
    v2.color = v1.color;

    lineVertices.push_back(v1);
    lineVertices.push_back(v2);
}

void PhysicsDebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                                         JPH::ColorArg inColor, ECastShadow) {
    if (!enabled || triangleVertices.size() >= MAX_TRIANGLES * 3) return;

    glm::vec4 color(inColor.r / 255.0f, inColor.g / 255.0f, inColor.b / 255.0f, inColor.a / 255.0f);

    DebugVertex v1, v2, v3;
    v1.position = glm::vec3(inV1.GetX(), inV1.GetY(), inV1.GetZ());
    v1.color = color;

    v2.position = glm::vec3(inV2.GetX(), inV2.GetY(), inV2.GetZ());
    v2.color = color;

    v3.position = glm::vec3(inV3.GetX(), inV3.GetY(), inV3.GetZ());
    v3.color = color;

    triangleVertices.push_back(v1);
    triangleVertices.push_back(v2);
    triangleVertices.push_back(v3);
}

void PhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg, const std::string_view&,
                                       JPH::ColorArg, float) {
    // Text rendering not implemented - would require font atlas
}

void PhysicsDebugRenderer::uploadVertexData() {
    if (!lineVertices.empty() && lineVertexMapped) {
        memcpy(lineVertexMapped, lineVertices.data(), lineVertices.size() * sizeof(DebugVertex));
    }

    if (!triangleVertices.empty() && triangleVertexMapped) {
        memcpy(triangleVertexMapped, triangleVertices.data(), triangleVertices.size() * sizeof(DebugVertex));
    }
}

void PhysicsDebugRenderer::render(VkCommandBuffer cmd, const glm::mat4& viewProj) {
    if (!enabled || !initialized) return;
    if (lineVertices.empty() && triangleVertices.empty()) return;

    // Upload vertex data
    uploadVertexData();

    // Set viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants
    PushConstants pc;
    pc.viewProj = viewProj;

    // Draw lines
    if (!lineVertices.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(cmd, linePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkBuffer buffers[] = {lineVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdDraw(cmd, static_cast<uint32_t>(lineVertices.size()), 1, 0, 0);
    }

    // Draw triangles (wireframe)
    if (!triangleVertices.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(cmd, linePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkBuffer buffers[] = {triangleVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdDraw(cmd, static_cast<uint32_t>(triangleVertices.size()), 1, 0, 0);
    }
}

#endif // JPH_DEBUG_RENDERER
