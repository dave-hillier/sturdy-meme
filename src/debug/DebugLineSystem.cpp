#include "DebugLineSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

bool DebugLineSystem::init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass,
                            const std::string& shaderPath, uint32_t framesInFlight) {
    this->device = device;
    this->allocator = allocator;

    // Create per-frame data
    frameData.resize(framesInFlight);

    // Create pipeline
    if (!createPipeline(renderPass, shaderPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline");
        return false;
    }

    SDL_Log("DebugLineSystem: Initialized with %u frames in flight", framesInFlight);
    return true;
}

bool DebugLineSystem::init(const InitContext& ctx, VkRenderPass renderPass) {
    this->device = ctx.device;
    this->allocator = ctx.allocator;

    // Create per-frame data
    frameData.resize(ctx.framesInFlight);

    // Create pipeline
    if (!createPipeline(renderPass, ctx.shaderPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline");
        return false;
    }

    SDL_Log("DebugLineSystem: Initialized with %u frames in flight", ctx.framesInFlight);
    return true;
}

void DebugLineSystem::shutdown() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    for (auto& frame : frameData) {
        if (frame.lineVertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, frame.lineVertexBuffer, frame.lineVertexAllocation);
        }
        if (frame.triangleVertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, frame.triangleVertexBuffer, frame.triangleVertexAllocation);
        }
    }
    frameData.clear();

    destroyPipeline();

    device = VK_NULL_HANDLE;
    allocator = VK_NULL_HANDLE;
}

bool DebugLineSystem::createPipeline(VkRenderPass renderPass, const std::string& shaderPath) {
    // Load shaders
    auto vertShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.vert.spv");
    auto fragShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.frag.spv");

    if (!vertShader || !fragShader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to load shaders");
        if (vertShader) vkDestroyShaderModule(device, *vertShader, nullptr);
        if (fragShader) vkDestroyShaderModule(device, *fragShader, nullptr);
        return false;
    }

    // Push constant for view-projection matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline layout");
        vkDestroyShaderModule(device, *vertShader, nullptr);
        vkDestroyShaderModule(device, *fragShader, nullptr);
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = *vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = *fragShader;
    shaderStages[1].pName = "main";

    // Vertex input
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(DebugLineVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDescs[2]{};
    attrDescs[0].location = 0;
    attrDescs[0].binding = 0;
    attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDescs[0].offset = offsetof(DebugLineVertex, position);
    attrDescs[1].location = 1;
    attrDescs[1].binding = 0;
    attrDescs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDescs[1].offset = offsetof(DebugLineVertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

    // Input assembly for lines
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyLine{};
    inputAssemblyLine.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyLine.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssemblyLine.primitiveRestartEnable = VK_FALSE;

    // Input assembly for triangles
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyTriangle{};
    inputAssemblyTriangle.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyTriangle.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyTriangle.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport and scissor
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil - read depth but don't write (overlay on top of scene)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending - alpha blending for semi-transparent debug visualization
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

    // Create line pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyLine;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create line pipeline");
        vkDestroyShaderModule(device, *vertShader, nullptr);
        vkDestroyShaderModule(device, *fragShader, nullptr);
        return false;
    }

    // Create triangle pipeline
    pipelineInfo.pInputAssemblyState = &inputAssemblyTriangle;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create triangle pipeline");
        vkDestroyPipeline(device, linePipeline, nullptr);
        linePipeline = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, *vertShader, nullptr);
        vkDestroyShaderModule(device, *fragShader, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, *vertShader, nullptr);
    vkDestroyShaderModule(device, *fragShader, nullptr);

    return true;
}

void DebugLineSystem::destroyPipeline() {
    if (trianglePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, trianglePipeline, nullptr);
        trianglePipeline = VK_NULL_HANDLE;
    }
    if (linePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, linePipeline, nullptr);
        linePipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

void DebugLineSystem::beginFrame(uint32_t frameIndex) {
    currentFrame = frameIndex;
    lineVertices.clear();
    triangleVertices.clear();
}

void DebugLineSystem::addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
    lineVertices.push_back({start, color});
    lineVertices.push_back({end, color});
}

void DebugLineSystem::addTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec4& color) {
    triangleVertices.push_back({v0, color});
    triangleVertices.push_back({v1, color});
    triangleVertices.push_back({v2, color});
}

void DebugLineSystem::addBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    // 12 edges of a box
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {max.x, max.y, min.z}, {min.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {max.x, max.y, max.z}, {min.x, max.y, max.z}
    };
    // Bottom face
    addLine(corners[0], corners[1], color);
    addLine(corners[1], corners[2], color);
    addLine(corners[2], corners[3], color);
    addLine(corners[3], corners[0], color);
    // Top face
    addLine(corners[4], corners[5], color);
    addLine(corners[5], corners[6], color);
    addLine(corners[6], corners[7], color);
    addLine(corners[7], corners[4], color);
    // Vertical edges
    addLine(corners[0], corners[4], color);
    addLine(corners[1], corners[5], color);
    addLine(corners[2], corners[6], color);
    addLine(corners[3], corners[7], color);
}

void DebugLineSystem::addSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);

    // XY circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(cosf(a0) * radius, sinf(a0) * radius, 0);
        glm::vec3 p1 = center + glm::vec3(cosf(a1) * radius, sinf(a1) * radius, 0);
        addLine(p0, p1, color);
    }
    // XZ circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(cosf(a0) * radius, 0, sinf(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(cosf(a1) * radius, 0, sinf(a1) * radius);
        addLine(p0, p1, color);
    }
    // YZ circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(0, cosf(a0) * radius, sinf(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(0, cosf(a1) * radius, sinf(a1) * radius);
        addLine(p0, p1, color);
    }
}

void DebugLineSystem::addCapsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, int segments) {
    // Draw the cylinder part
    glm::vec3 axis = end - start;
    float height = glm::length(axis);
    if (height < 0.0001f) {
        addSphere(start, radius, color, segments);
        return;
    }
    axis = glm::normalize(axis);

    // Find perpendicular vectors
    glm::vec3 perp1 = glm::abs(axis.y) < 0.9f ?
        glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0))) :
        glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));
    glm::vec3 perp2 = glm::cross(axis, perp1);

    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);

    // Cylinder lines
    for (int i = 0; i < segments; i++) {
        float a = step * i;
        glm::vec3 offset = (cosf(a) * perp1 + sinf(a) * perp2) * radius;
        addLine(start + offset, end + offset, color);
    }

    // End cap circles
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 off0 = (cosf(a0) * perp1 + sinf(a0) * perp2) * radius;
        glm::vec3 off1 = (cosf(a1) * perp1 + sinf(a1) * perp2) * radius;
        addLine(start + off0, start + off1, color);
        addLine(end + off0, end + off1, color);
    }

    // Hemisphere arcs
    for (int i = 0; i < segments / 2; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        // Start hemisphere (pointing away from end)
        glm::vec3 p0 = start + (-axis * cosf(a0) + perp1 * sinf(a0)) * radius;
        glm::vec3 p1 = start + (-axis * cosf(a1) + perp1 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        p0 = start + (-axis * cosf(a0) + perp2 * sinf(a0)) * radius;
        p1 = start + (-axis * cosf(a1) + perp2 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        // End hemisphere (pointing away from start)
        p0 = end + (axis * cosf(a0) + perp1 * sinf(a0)) * radius;
        p1 = end + (axis * cosf(a1) + perp1 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        p0 = end + (axis * cosf(a0) + perp2 * sinf(a0)) * radius;
        p1 = end + (axis * cosf(a1) + perp2 * sinf(a1)) * radius;
        addLine(p0, p1, color);
    }
}

#ifdef JPH_DEBUG_RENDERER
void DebugLineSystem::importFromPhysicsDebugRenderer(const PhysicsDebugRenderer& renderer) {
    // Import lines
    for (const auto& line : renderer.getLines()) {
        addLine(line.start, line.end, line.color);
    }

    // Import triangles (convert to wireframe lines)
    for (const auto& tri : renderer.getTriangles()) {
        addLine(tri.v0, tri.v1, tri.color);
        addLine(tri.v1, tri.v2, tri.color);
        addLine(tri.v2, tri.v0, tri.color);
    }
}
#endif

void DebugLineSystem::uploadLines() {
    if (currentFrame >= frameData.size()) return;

    auto& frame = frameData[currentFrame];

    // Upload lines
    if (!lineVertices.empty()) {
        size_t requiredSize = lineVertices.size() * sizeof(DebugLineVertex);

        // Recreate buffer if too small
        if (frame.lineBufferSize < requiredSize) {
            if (frame.lineVertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, frame.lineVertexBuffer, frame.lineVertexAllocation);
            }

            size_t newSize = std::max(requiredSize, INITIAL_BUFFER_SIZE);

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = newSize;
            bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                               &frame.lineVertexBuffer, &frame.lineVertexAllocation, nullptr) != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create line vertex buffer");
                return;
            }

            frame.lineBufferSize = newSize;
        }

        // Copy data
        void* data;
        vmaMapMemory(allocator, frame.lineVertexAllocation, &data);
        memcpy(data, lineVertices.data(), requiredSize);
        vmaUnmapMemory(allocator, frame.lineVertexAllocation);
    }

    // Upload triangles
    if (!triangleVertices.empty()) {
        size_t requiredSize = triangleVertices.size() * sizeof(DebugLineVertex);

        if (frame.triangleBufferSize < requiredSize) {
            if (frame.triangleVertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, frame.triangleVertexBuffer, frame.triangleVertexAllocation);
            }

            size_t newSize = std::max(requiredSize, INITIAL_BUFFER_SIZE);

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = newSize;
            bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                               &frame.triangleVertexBuffer, &frame.triangleVertexAllocation, nullptr) != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create triangle vertex buffer");
                return;
            }

            frame.triangleBufferSize = newSize;
        }

        void* data;
        vmaMapMemory(allocator, frame.triangleVertexAllocation, &data);
        memcpy(data, triangleVertices.data(), requiredSize);
        vmaUnmapMemory(allocator, frame.triangleVertexAllocation);
    }
}

void DebugLineSystem::recordCommands(VkCommandBuffer cmd, const glm::mat4& viewProj) {
    if (currentFrame >= frameData.size()) return;

    auto& frame = frameData[currentFrame];

    // Draw lines
    if (!lineVertices.empty() && frame.lineVertexBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);

        VkBuffer buffers[] = { frame.lineVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdDraw(cmd, static_cast<uint32_t>(lineVertices.size()), 1, 0, 0);
    }

    // Draw triangles (as wireframe)
    if (!triangleVertices.empty() && frame.triangleVertexBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);

        VkBuffer buffers[] = { frame.triangleVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdDraw(cmd, static_cast<uint32_t>(triangleVertices.size()), 1, 0, 0);
    }
}
