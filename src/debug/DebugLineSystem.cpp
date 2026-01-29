#include "DebugLineSystem.h"
#include "ShaderLoader.h"
#include "core/vulkan/VertexInputBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <algorithm>

// Factory methods
std::unique_ptr<DebugLineSystem> DebugLineSystem::create(VkDevice device, VmaAllocator allocator,
                                                          VkRenderPass renderPass,
                                                          const std::string& shaderPath,
                                                          uint32_t framesInFlight) {
    auto system = std::make_unique<DebugLineSystem>(ConstructToken{});
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

    vk::Device(device).waitIdle();

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
    auto vertShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.vert.spv", ShaderLoader::RaiiTag{});
    auto fragShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.frag.spv", ShaderLoader::RaiiTag{});

    if (!vertShader || !fragShader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to load shaders");
        return false;
    }

    vk::Device vkDevice(device);

    // Push constant for view-projection matrix
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(glm::mat4));

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setPushConstantRanges(pushConstantRange);

    try {
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline layout: %s", e.what());
        return false;
    }

    // Shader stages
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(vertShader->get())
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(fragShader->get())
            .setPName("main")
    }};

    // Vertex input: position (vec3) + color (vec4)
    auto vertexInput = VertexInputBuilder()
        .addBinding(VertexBindingBuilder::perVertex<DebugLineVertex>(0))
        .addAttribute(AttributeBuilder::vec3(0, offsetof(DebugLineVertex, position)))
        .addAttribute(AttributeBuilder::vec4(1, offsetof(DebugLineVertex, color)));

    auto vertexInputInfo = vertexInput.build();

    // Input assembly for lines
    auto inputAssemblyLine = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eLineList)
        .setPrimitiveRestartEnable(VK_FALSE);

    // Input assembly for triangles
    auto inputAssemblyTriangle = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(VK_FALSE);

    // Dynamic viewport and scissor
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size()))
        .setPDynamicStates(dynamicStates.data());

    // Rasterization
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(VK_FALSE)
        .setRasterizerDiscardEnable(VK_FALSE)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_FALSE);

    // Multisampling
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(VK_FALSE)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // Depth stencil - read depth but don't write (overlay on top of scene)
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_FALSE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setDepthBoundsTestEnable(VK_FALSE)
        .setStencilTestEnable(VK_FALSE);

    // Color blending - alpha blending for semi-transparent debug visualization
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setBlendEnable(VK_TRUE)
        .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(VK_FALSE)
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    // Create line pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(shaderStages.data())
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssemblyLine)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(pipelineLayout)
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto lineResult = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    if (lineResult.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create line pipeline");
        return false;
    }
    linePipeline = lineResult.value;

    // Create triangle pipeline
    pipelineInfo.setPInputAssemblyState(&inputAssemblyTriangle);
    auto triangleResult = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    if (triangleResult.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create triangle pipeline");
        vkDevice.destroyPipeline(linePipeline);
        linePipeline = VK_NULL_HANDLE;
        return false;
    }
    trianglePipeline = triangleResult.value;

    return true;
}

void DebugLineSystem::destroyPipeline() {
    vk::Device vkDevice(device);
    if (trianglePipeline != VK_NULL_HANDLE) {
        vkDevice.destroyPipeline(trianglePipeline);
        trianglePipeline = VK_NULL_HANDLE;
    }
    if (linePipeline != VK_NULL_HANDLE) {
        vkDevice.destroyPipeline(linePipeline);
        linePipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDevice.destroyPipelineLayout(pipelineLayout);
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

void DebugLineSystem::reserveLines(size_t lineCount) {
    lineVertices.reserve(lineVertices.size() + lineCount * 2);
}

void DebugLineSystem::reserveTriangles(size_t triangleCount) {
    triangleVertices.reserve(triangleVertices.size() + triangleCount * 3);
}

void DebugLineSystem::appendLineVertices(const DebugLineVertex* vertices, size_t count) {
    lineVertices.insert(lineVertices.end(), vertices, vertices + count);
}

void DebugLineSystem::appendTriangleVertices(const DebugLineVertex* vertices, size_t count) {
    triangleVertices.insert(triangleVertices.end(), vertices, vertices + count);
}

void DebugLineSystem::setPersistentLines(const DebugLineVertex* vertices, size_t count) {
    persistentLineVertices.assign(vertices, vertices + count);
}

void DebugLineSystem::clearPersistentLines() {
    persistentLineVertices.clear();
    persistentLineVertices.shrink_to_fit();
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

void DebugLineSystem::addCone(const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& color, int segments) {
    glm::vec3 axis = tip - base;
    float height = glm::length(axis);
    if (height < 0.0001f) {
        return; // Degenerate cone
    }
    axis = glm::normalize(axis);

    // Find perpendicular vectors
    glm::vec3 perp1 = glm::abs(axis.y) < 0.9f ?
        glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0))) :
        glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));
    glm::vec3 perp2 = glm::cross(axis, perp1);

    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);

    // Draw base circle and lines to tip
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 off0 = (cosf(a0) * perp1 + sinf(a0) * perp2) * radius;
        glm::vec3 off1 = (cosf(a1) * perp1 + sinf(a1) * perp2) * radius;

        // Base circle edge
        addLine(base + off0, base + off1, color);

        // Side edges to tip
        addLine(base + off0, tip, color);
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

    // Calculate total line vertices (persistent + per-frame)
    size_t totalLineVertices = persistentLineVertices.size() + lineVertices.size();

    // Upload lines (persistent + per-frame combined)
    if (totalLineVertices > 0) {
        size_t requiredSize = totalLineVertices * sizeof(DebugLineVertex);

        // Recreate buffer if too small
        if (frame.lineBufferSize < requiredSize) {
            if (frame.lineVertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, frame.lineVertexBuffer, frame.lineVertexAllocation);
            }

            size_t newSize = std::max(requiredSize, INITIAL_BUFFER_SIZE);

            auto bufferInfo = vk::BufferCreateInfo{}
                .setSize(newSize)
                .setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
                .setSharingMode(vk::SharingMode::eExclusive);

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateBuffer(allocator, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo,
                               &frame.lineVertexBuffer, &frame.lineVertexAllocation, nullptr) != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create line vertex buffer");
                return;
            }

            frame.lineBufferSize = newSize;
        }

        // Copy persistent lines first, then per-frame lines
        void* data;
        vmaMapMemory(allocator, frame.lineVertexAllocation, &data);
        char* dst = static_cast<char*>(data);
        if (!persistentLineVertices.empty()) {
            size_t persistentSize = persistentLineVertices.size() * sizeof(DebugLineVertex);
            memcpy(dst, persistentLineVertices.data(), persistentSize);
            dst += persistentSize;
        }
        if (!lineVertices.empty()) {
            memcpy(dst, lineVertices.data(), lineVertices.size() * sizeof(DebugLineVertex));
        }
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

            auto bufferInfo = vk::BufferCreateInfo{}
                .setSize(newSize)
                .setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
                .setSharingMode(vk::SharingMode::eExclusive);

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateBuffer(allocator, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo,
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

    vk::CommandBuffer vkCmd(cmd);

    // Draw lines (persistent + per-frame)
    size_t totalLineVertices = persistentLineVertices.size() + lineVertices.size();
    if (totalLineVertices > 0 && frame.lineVertexBuffer != VK_NULL_HANDLE) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, linePipeline);
        vkCmd.pushConstants<glm::mat4>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, viewProj);

        vk::Buffer buffer(frame.lineVertexBuffer);
        vk::DeviceSize offset = 0;
        vkCmd.bindVertexBuffers(0, buffer, offset);
        vkCmd.draw(static_cast<uint32_t>(totalLineVertices), 1, 0, 0);
    }

    // Draw triangles (as wireframe)
    if (!triangleVertices.empty() && frame.triangleVertexBuffer != VK_NULL_HANDLE) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, trianglePipeline);
        vkCmd.pushConstants<glm::mat4>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, viewProj);

        vk::Buffer buffer(frame.triangleVertexBuffer);
        vk::DeviceSize offset = 0;
        vkCmd.bindVertexBuffers(0, buffer, offset);
        vkCmd.draw(static_cast<uint32_t>(triangleVertices.size()), 1, 0, 0);
    }
}
