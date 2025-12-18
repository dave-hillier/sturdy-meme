#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include "InitContext.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

// Vertex for debug lines
struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

// System for rendering debug lines and triangles using Vulkan
class DebugLineSystem {
public:
    // Factory: returns nullptr on failure
    static std::unique_ptr<DebugLineSystem> create(VkDevice device, VmaAllocator allocator,
                                                    VkRenderPass renderPass,
                                                    const std::string& shaderPath,
                                                    uint32_t framesInFlight);
    static std::unique_ptr<DebugLineSystem> create(const InitContext& ctx, VkRenderPass renderPass);

    // Destructor handles cleanup
    ~DebugLineSystem();

    // Move-only (RAII handles are non-copyable)
    DebugLineSystem(DebugLineSystem&& other) noexcept;
    DebugLineSystem& operator=(DebugLineSystem&& other) noexcept;
    DebugLineSystem(const DebugLineSystem&) = delete;
    DebugLineSystem& operator=(const DebugLineSystem&) = delete;

    // Begin collecting lines for this frame
    void beginFrame(uint32_t frameIndex);

    // Add lines directly
    void addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);
    void addTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec4& color);
    void addBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void addSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
    void addCapsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, int segments = 8);

#ifdef JPH_DEBUG_RENDERER
    // Import lines from physics debug renderer
    void importFromPhysicsDebugRenderer(const PhysicsDebugRenderer& renderer);
#endif

    // Upload collected lines to GPU
    void uploadLines();

    // Record draw commands
    void recordCommands(VkCommandBuffer cmd, const glm::mat4& viewProj);

    // Check if there are any lines to draw
    bool hasLines() const { return !lineVertices.empty(); }

    // Statistics
    size_t getLineCount() const { return lineVertices.size() / 2; }
    size_t getTriangleCount() const { return triangleVertices.size() / 3; }

private:
    DebugLineSystem();  // Private: only factory can construct

    bool initInternal(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass,
                      const std::string& shaderPath, uint32_t framesInFlight);
    bool createPipeline(VkRenderPass renderPass, const std::string& shaderPath);
    void destroyPipeline();
    void cleanup();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Pipeline
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline linePipeline = VK_NULL_HANDLE;
    VkPipeline trianglePipeline = VK_NULL_HANDLE;

    // Per-frame vertex buffers (double-buffered)
    struct FrameData {
        VkBuffer lineVertexBuffer = VK_NULL_HANDLE;
        VmaAllocation lineVertexAllocation = VK_NULL_HANDLE;
        VkBuffer triangleVertexBuffer = VK_NULL_HANDLE;
        VmaAllocation triangleVertexAllocation = VK_NULL_HANDLE;
        size_t lineBufferSize = 0;
        size_t triangleBufferSize = 0;
    };
    std::vector<FrameData> frameData;
    uint32_t currentFrame = 0;

    // Collected vertices for current frame
    std::vector<DebugLineVertex> lineVertices;
    std::vector<DebugLineVertex> triangleVertices;

    static constexpr size_t INITIAL_BUFFER_SIZE = 64 * 1024; // 64KB initial buffer
};
