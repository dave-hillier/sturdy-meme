#include "DebugLineSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

using namespace vk;

// Private constructor
DebugLineSystem::DebugLineSystem() = default;

// Factory methods
std::unique_ptr<DebugLineSystem> DebugLineSystem::create(VkDevice device, VmaAllocator allocator,
                                                          VkRenderPass renderPass,
                                                          const std::string& shaderPath,
                                                          uint32_t framesInFlight) {
    std::unique_ptr<DebugLineSystem> system(new DebugLineSystem());
    if (!system->initInternal(device, allocator, renderPass, shaderPath, framesInFlight)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<DebugLineSystem> DebugLineSystem::create(const InitContext& ctx, VkRenderPass renderPass) {
    return create(ctx.device, ctx.allocator, renderPass, ctx.shaderPath, ctx.framesInFlight);
}

// Destructor
DebugLineSystem::~DebugLineSystem() {
    cleanup();
}

// Move constructor
DebugLineSystem::DebugLineSystem(DebugLineSystem&& other) noexcept
    : device(other.device)
    , allocator(other.allocator)
    , pipelineLayout(other.pipelineLayout)
    , linePipeline(other.linePipeline)
    , trianglePipeline(other.trianglePipeline)
    , frameData(std::move(other.frameData))
    , currentFrame(other.currentFrame)
    , lineVertices(std::move(other.lineVertices))
    , triangleVertices(std::move(other.triangleVertices))
{
    // Null out the source to prevent double-free
    other.device = VK_NULL_HANDLE;
    other.allocator = VK_NULL_HANDLE;
    other.pipelineLayout = VK_NULL_HANDLE;
    other.linePipeline = VK_NULL_HANDLE;
    other.trianglePipeline = VK_NULL_HANDLE;
}

// Move assignment
DebugLineSystem& DebugLineSystem::operator=(DebugLineSystem&& other) noexcept {
    if (this != &other) {
        cleanup();

        device = other.device;
        allocator = other.allocator;
        pipelineLayout = other.pipelineLayout;
        linePipeline = other.linePipeline;
        trianglePipeline = other.trianglePipeline;
        frameData = std::move(other.frameData);
        currentFrame = other.currentFrame;
        lineVertices = std::move(other.lineVertices);
        triangleVertices = std::move(other.triangleVertices);

        other.device = VK_NULL_HANDLE;
        other.allocator = VK_NULL_HANDLE;
        other.pipelineLayout = VK_NULL_HANDLE;
        other.linePipeline = VK_NULL_HANDLE;
        other.trianglePipeline = VK_NULL_HANDLE;
    }
    return *this;
}

// Internal initialization
bool DebugLineSystem::initInternal(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass,
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

// Cleanup (called by destructor)
void DebugLineSystem::cleanup() {
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
    PushConstantRange pushConstantRange{
        ShaderStageFlagBits::eVertex,
        0,                              // offset
        sizeof(glm::mat4)
    };

    PipelineLayoutCreateInfo layoutInfo{
        {},                             // flags
        0, nullptr,                     // setLayouts
        1, &pushConstantRange
    };

    auto vkLayoutInfo = static_cast<VkPipelineLayoutCreateInfo>(layoutInfo);
    if (vkCreatePipelineLayout(device, &vkLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline layout");
        vkDestroyShaderModule(device, *vertShader, nullptr);
        vkDestroyShaderModule(device, *fragShader, nullptr);
        return false;
    }

    // Shader stages
    std::array<PipelineShaderStageCreateInfo, 2> shaderStages{{
        {{}, ShaderStageFlagBits::eVertex, *vertShader, "main"},
        {{}, ShaderStageFlagBits::eFragment, *fragShader, "main"}
    }};

    // Vertex input
    VertexInputBindingDescription bindingDesc{
        0,                              // binding
        sizeof(DebugLineVertex),
        VertexInputRate::eVertex
    };

    std::array<VertexInputAttributeDescription, 2> attrDescs{{
        {0, 0, Format::eR32G32B32Sfloat, offsetof(DebugLineVertex, position)},
        {1, 0, Format::eR32G32B32A32Sfloat, offsetof(DebugLineVertex, color)}
    }};

    PipelineVertexInputStateCreateInfo vertexInputInfo{
        {},                             // flags
        1, &bindingDesc,
        2, attrDescs.data()
    };

    // Input assembly for lines
    PipelineInputAssemblyStateCreateInfo inputAssemblyLine{
        {},                             // flags
        PrimitiveTopology::eLineList,
        VK_FALSE                        // primitiveRestartEnable
    };

    // Input assembly for triangles
    PipelineInputAssemblyStateCreateInfo inputAssemblyTriangle{
        {},                             // flags
        PrimitiveTopology::eTriangleList,
        VK_FALSE
    };

    // Dynamic viewport and scissor
    PipelineViewportStateCreateInfo viewportState{
        {},                             // flags
        1, nullptr,                     // viewports (dynamic)
        1, nullptr                      // scissors (dynamic)
    };

    std::array<DynamicState, 2> dynamicStates = {DynamicState::eViewport, DynamicState::eScissor};
    PipelineDynamicStateCreateInfo dynamicState{
        {},                             // flags
        static_cast<uint32_t>(dynamicStates.size()),
        dynamicStates.data()
    };

    // Rasterization
    PipelineRasterizationStateCreateInfo rasterizer{
        {},                             // flags
        VK_FALSE,                       // depthClampEnable
        VK_FALSE,                       // rasterizerDiscardEnable
        PolygonMode::eFill,
        CullModeFlagBits::eNone,
        FrontFace::eCounterClockwise,
        VK_FALSE,                       // depthBiasEnable
        0.0f, 0.0f, 0.0f,               // depthBias*
        1.0f                            // lineWidth
    };

    // Multisampling
    PipelineMultisampleStateCreateInfo multisampling{
        {},                             // flags
        SampleCountFlagBits::e1,
        VK_FALSE                        // sampleShadingEnable
    };

    // Depth stencil - read depth but don't write (overlay on top of scene)
    PipelineDepthStencilStateCreateInfo depthStencil{
        {},                             // flags
        VK_TRUE,                        // depthTestEnable
        VK_FALSE,                       // depthWriteEnable
        CompareOp::eLessOrEqual,
        VK_FALSE,                       // depthBoundsTestEnable
        VK_FALSE                        // stencilTestEnable
    };

    // Color blending - alpha blending for semi-transparent debug visualization
    PipelineColorBlendAttachmentState colorBlendAttachment{
        VK_TRUE,                        // blendEnable
        BlendFactor::eSrcAlpha,         // srcColorBlendFactor
        BlendFactor::eOneMinusSrcAlpha, // dstColorBlendFactor
        BlendOp::eAdd,                  // colorBlendOp
        BlendFactor::eOne,              // srcAlphaBlendFactor
        BlendFactor::eZero,             // dstAlphaBlendFactor
        BlendOp::eAdd,                  // alphaBlendOp
        ColorComponentFlagBits::eR | ColorComponentFlagBits::eG |
        ColorComponentFlagBits::eB | ColorComponentFlagBits::eA
    };

    PipelineColorBlendStateCreateInfo colorBlending{
        {},                             // flags
        VK_FALSE,                       // logicOpEnable
        LogicOp::eCopy,
        1, &colorBlendAttachment
    };

    // Create line pipeline (cast to VkXxx for raw API call)
    auto vkShaderStages = reinterpret_cast<const VkPipelineShaderStageCreateInfo*>(shaderStages.data());
    auto vkVertexInput = static_cast<VkPipelineVertexInputStateCreateInfo>(vertexInputInfo);
    auto vkInputAssemblyLine = static_cast<VkPipelineInputAssemblyStateCreateInfo>(inputAssemblyLine);
    auto vkInputAssemblyTriangle = static_cast<VkPipelineInputAssemblyStateCreateInfo>(inputAssemblyTriangle);
    auto vkViewportState = static_cast<VkPipelineViewportStateCreateInfo>(viewportState);
    auto vkRasterizer = static_cast<VkPipelineRasterizationStateCreateInfo>(rasterizer);
    auto vkMultisampling = static_cast<VkPipelineMultisampleStateCreateInfo>(multisampling);
    auto vkDepthStencil = static_cast<VkPipelineDepthStencilStateCreateInfo>(depthStencil);
    auto vkColorBlending = static_cast<VkPipelineColorBlendStateCreateInfo>(colorBlending);
    auto vkDynamicState = static_cast<VkPipelineDynamicStateCreateInfo>(dynamicState);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = vkShaderStages;
    pipelineInfo.pVertexInputState = &vkVertexInput;
    pipelineInfo.pInputAssemblyState = &vkInputAssemblyLine;
    pipelineInfo.pViewportState = &vkViewportState;
    pipelineInfo.pRasterizationState = &vkRasterizer;
    pipelineInfo.pMultisampleState = &vkMultisampling;
    pipelineInfo.pDepthStencilState = &vkDepthStencil;
    pipelineInfo.pColorBlendState = &vkColorBlending;
    pipelineInfo.pDynamicState = &vkDynamicState;
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
    pipelineInfo.pInputAssemblyState = &vkInputAssemblyTriangle;
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

            // 
            BufferCreateInfo bufferInfo{
                {},                             // flags
                newSize,
                BufferUsageFlagBits::eVertexBuffer,
                SharingMode::eExclusive
            };

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
            if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo,
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

            // 
            BufferCreateInfo bufferInfo{
                {},                             // flags
                newSize,
                BufferUsageFlagBits::eVertexBuffer,
                SharingMode::eExclusive
            };

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
            if (vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo,
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
