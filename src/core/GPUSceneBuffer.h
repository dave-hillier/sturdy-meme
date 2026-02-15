#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

#include "vulkan/VmaBuffer.h"
#include "vulkan/VmaBufferFactory.h"
#include "PerFrameBuffer.h"
#include "RenderableBuilder.h"
#include "shaders/bindings.h"

// Maximum objects supported for GPU-driven rendering
constexpr size_t MAX_GPU_SCENE_OBJECTS = 8192;

// Per-object data for GPU frustum culling
// Must match GPUCullObjectData in scene_cull.comp
struct alignas(16) GPUCullObjectData {
    glm::vec4 boundingSphere;   // xyz = center (world space), w = radius
    glm::vec4 aabbMin;          // xyz = min corner (world space), w = unused
    glm::vec4 aabbMax;          // xyz = max corner (world space), w = unused
    uint32_t objectIndex;       // Index into scene instance buffer
    uint32_t firstIndex;        // First index in global index buffer
    uint32_t indexCount;        // Number of indices
    int32_t vertexOffset;       // Vertex offset
};
static_assert(sizeof(GPUCullObjectData) == 64, "GPUCullObjectData size mismatch");

// Per-instance data for scene objects (must match SceneInstance in shader)
// This extends SceneInstanceData with additional cull/draw information
struct alignas(16) GPUSceneInstanceData {
    glm::mat4 model;              // 64 bytes, offset 0
    glm::vec4 materialParams;     // 16 bytes, offset 64 (roughness, metallic, emissiveIntensity, opacity)
    glm::vec4 emissiveColor;      // 16 bytes, offset 80 (rgb=color, a=unused)
    uint32_t pbrFlags;            // 4 bytes, offset 96
    float alphaTestThreshold;     // 4 bytes, offset 100
    float hueShift;               // 4 bytes, offset 104
    uint32_t materialId;          // 4 bytes, offset 108 (index into material buffer)
    // Total: 112 bytes per instance
};
static_assert(sizeof(GPUSceneInstanceData) == 112, "GPUSceneInstanceData size mismatch with shader");

// Indirect draw command (matches VkDrawIndexedIndirectCommand)
struct GPUDrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};
static_assert(sizeof(GPUDrawIndexedIndirectCommand) == 20, "GPUDrawIndexedIndirectCommand size mismatch");

// Mesh batch for indirect rendering - groups objects by mesh+material
struct GPUMeshBatch {
    const class Mesh* mesh;
    MaterialId materialId;
    uint32_t firstObject;         // Index into cull object array
    uint32_t objectCount;         // Number of objects in this batch
};

/**
 * GPUSceneBuffer - Manages GPU buffers for GPU-driven scene rendering
 *
 * This class handles:
 * 1. Scene instance data (transforms + material params) for shaders
 * 2. Cull object data (bounds) for compute culling
 * 3. Indirect draw commands for vkCmdDrawIndexedIndirectCount
 * 4. Draw count for variable-length indirect draws
 *
 * Usage:
 *   1. init(allocator, frameCount) - Initialize once at startup
 *   2. beginFrame(frameIndex) - Start building frame data
 *   3. addObject(renderable) - Add each scene object
 *   4. finalize() - Upload to GPU and compute batches
 *   5. Use getters for descriptor binding and indirect rendering
 */
class GPUSceneBuffer {
public:
    GPUSceneBuffer() = default;
    ~GPUSceneBuffer() = default;

    // Non-copyable
    GPUSceneBuffer(const GPUSceneBuffer&) = delete;
    GPUSceneBuffer& operator=(const GPUSceneBuffer&) = delete;

    // Movable
    GPUSceneBuffer(GPUSceneBuffer&&) = default;
    GPUSceneBuffer& operator=(GPUSceneBuffer&&) = default;

    // Initialize buffers (call once at startup)
    bool init(VmaAllocator allocator, uint32_t frameCount);

    // Cleanup (call before shutdown)
    void cleanup();

    // Begin a new frame (clears previous frame's data)
    void beginFrame(uint32_t frameIndex);

    // Add an object to the current frame
    // Returns the object index, or -1 if buffer is full
    int32_t addObject(const Renderable& renderable);

    // Finalize the frame: upload to GPU
    // Call after all addObject() calls for the frame
    void finalize();

    // Reset indirect draw count to zero (call before culling pass)
    void resetDrawCount(vk::CommandBuffer cmd);

    // Get buffer accessors
    VkBuffer getInstanceBuffer(uint32_t frameIndex) const { return instanceBuffers_.buffers[frameIndex]; }
    VkBuffer getCullObjectBuffer() const { return cullObjectBuffer_.get(); }
    VkBuffer getIndirectBuffer(uint32_t frameIndex) const { return indirectBuffers_.buffers[frameIndex]; }
    VkBuffer getDrawCountBuffer(uint32_t frameIndex) const { return drawCountBuffers_.buffers[frameIndex]; }

    // Get object counts
    uint32_t getObjectCount() const { return static_cast<uint32_t>(instances_.size()); }
    uint32_t getVisibleCount(uint32_t frameIndex) const;

    // Get batches for indirect rendering (after finalize)
    const std::vector<GPUMeshBatch>& getBatches() const { return batches_; }

    // Check if GPU-driven path should be used (enough objects to benefit)
    bool shouldUseGPUDriven() const { return instances_.size() >= 32; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t frameCount_ = 0;
    uint32_t currentFrame_ = 0;

    // Per-frame instance buffers (SSBO for shader access)
    BufferUtils::PerFrameBufferSet instanceBuffers_;

    // Single cull object buffer (updated when scene changes)
    VmaBuffer cullObjectBuffer_;

    // Per-frame indirect draw command buffers
    BufferUtils::PerFrameBufferSet indirectBuffers_;

    // Per-frame draw count buffers (for vkCmdDrawIndexedIndirectCount)
    BufferUtils::PerFrameBufferSet drawCountBuffers_;

    // CPU-side staging data for current frame
    std::vector<GPUSceneInstanceData> instances_;
    std::vector<GPUCullObjectData> cullObjects_;

    // Mesh batches for indirect rendering
    std::vector<GPUMeshBatch> batches_;

    // Track if cull data needs upload
    bool cullDataDirty_ = true;
};
