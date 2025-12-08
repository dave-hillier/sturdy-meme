#pragma once

#ifdef JPH_DEBUG_RENDERER

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>

// Must define JPH_DEBUG_RENDERER before including Jolt headers
#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

// Forward declarations
class VulkanContext;

// Vulkan implementation of Jolt's debug renderer
// Renders lines and triangles for physics debug visualization
class PhysicsDebugRenderer : public JPH::DebugRendererSimple {
public:
    PhysicsDebugRenderer() = default;
    ~PhysicsDebugRenderer();

    // Initialize Vulkan resources
    bool init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass,
              VkExtent2D extent, const std::string& shaderPath);
    void destroy();

    // Update viewport extent (on resize)
    void setExtent(VkExtent2D extent) { this->extent = extent; }

    // Begin a new frame - clears accumulated geometry
    void beginFrame(const glm::vec3& cameraPos);

    // Flush accumulated lines/triangles to GPU and record draw commands
    void render(VkCommandBuffer cmd, const glm::mat4& viewProj);

    // JPH::DebugRendererSimple interface
    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                      JPH::ColorArg inColor, ECastShadow inCastShadow) override;
    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString,
                    JPH::ColorArg inColor, float inHeight) override;

    // Enable/disable rendering
    void setEnabled(bool enabled) { this->enabled = enabled; }
    bool isEnabled() const { return enabled; }

    // Rendering options
    void setLineWidth(float width) { lineWidth = width; }
    float getLineWidth() const { return lineWidth; }

private:
    // Vertex format for debug rendering
    struct DebugVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    // Create graphics pipelines
    bool createPipelines();
    bool createBuffers();

    // Upload vertex data to GPU
    void uploadVertexData();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;

    // Line rendering pipeline
    VkPipelineLayout linePipelineLayout = VK_NULL_HANDLE;
    VkPipeline linePipeline = VK_NULL_HANDLE;

    // Triangle rendering pipeline (wireframe)
    VkPipeline trianglePipeline = VK_NULL_HANDLE;

    // Vertex buffers (double buffered for safety)
    static constexpr size_t MAX_LINES = 1000000;  // 1M lines
    static constexpr size_t MAX_TRIANGLES = 100000;  // 100K triangles

    VkBuffer lineVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation lineVertexAllocation = VK_NULL_HANDLE;
    void* lineVertexMapped = nullptr;

    VkBuffer triangleVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation triangleVertexAllocation = VK_NULL_HANDLE;
    void* triangleVertexMapped = nullptr;

    // CPU-side vertex accumulation
    std::vector<DebugVertex> lineVertices;
    std::vector<DebugVertex> triangleVertices;

    // State
    bool enabled = false;
    bool initialized = false;
    float lineWidth = 1.0f;

    // Push constants for view-projection matrix
    struct PushConstants {
        glm::mat4 viewProj;
    };
};

#endif // JPH_DEBUG_RENDERER
