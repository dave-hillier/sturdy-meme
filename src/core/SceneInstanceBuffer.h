#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "vulkan/VmaResources.h"
#include "RenderableBuilder.h"
#include "shaders/bindings.h"

// Maximum instances per frame (can be increased if needed)
constexpr size_t MAX_SCENE_INSTANCES = 4096;

// Per-instance data for scene objects (must match SceneInstance in shader)
// Layout: std430 (tightly packed with alignment rules)
// alignas(16) required for SIMD operations on glm::mat4
struct alignas(16) SceneInstanceData {
    glm::mat4 model;              // 64 bytes, offset 0
    glm::vec4 materialParams;     // 16 bytes, offset 64 (roughness, metallic, emissiveIntensity, opacity)
    glm::vec4 emissiveColor;      // 16 bytes, offset 80 (rgb=color, a=unused)
    uint32_t pbrFlags;            // 4 bytes, offset 96
    float alphaTestThreshold;     // 4 bytes, offset 100
    float _pad0;                  // 4 bytes, offset 104
    float _pad1;                  // 4 bytes, offset 108
    // Total: 112 bytes per instance
};
static_assert(sizeof(SceneInstanceData) == 112, "SceneInstanceData size mismatch with shader");

// Key for batching: objects with same material+mesh can be instanced together
struct InstanceBatchKey {
    MaterialId materialId;
    const class Mesh* mesh;

    bool operator==(const InstanceBatchKey& other) const {
        return materialId == other.materialId && mesh == other.mesh;
    }
};

struct InstanceBatchKeyHash {
    size_t operator()(const InstanceBatchKey& key) const {
        return std::hash<MaterialId>()(key.materialId) ^
               (std::hash<const void*>()(key.mesh) << 1);
    }
};

// A batch of instances sharing the same material and mesh
struct InstanceBatch {
    MaterialId materialId;
    const class Mesh* mesh;
    uint32_t firstInstance;   // Index into instance buffer
    uint32_t instanceCount;   // Number of instances in this batch
};

/**
 * SceneInstanceBuffer - Manages GPU buffer for instanced scene object rendering
 *
 * Batches scene objects by (materialId, mesh) to enable instanced drawing.
 * Per-frame double-buffering ensures safe updates while GPU reads previous frame.
 *
 * Usage:
 *   1. beginFrame(frameIndex) - Start new frame
 *   2. addInstance(renderable) for each scene object
 *   3. finalize() - Uploads data and builds batches
 *   4. getBatches() - Returns batches for instanced rendering
 *   5. getBuffer(frameIndex) - Get VkBuffer for descriptor binding
 */
class SceneInstanceBuffer {
public:
    SceneInstanceBuffer() = default;
    ~SceneInstanceBuffer() = default;

    // Initialize buffers (call once at startup)
    bool init(VmaAllocator allocator, uint32_t frameCount);

    // Cleanup (call before shutdown)
    void cleanup();

    // Begin a new frame (clears previous frame's instance data)
    void beginFrame(uint32_t frameIndex);

    // Add an instance to the current frame
    // Returns the instance index, or -1 if buffer is full
    int32_t addInstance(const Renderable& renderable);

    // Finalize the frame: upload to GPU and build batches
    // Call after all addInstance() calls for the frame
    void finalize();

    // Get the batches for instanced rendering (call after finalize)
    const std::vector<InstanceBatch>& getBatches() const { return batches_; }

    // Get the GPU buffer for the current frame (for descriptor binding)
    VkBuffer getBuffer(uint32_t frameIndex) const {
        return instanceBuffers_[frameIndex].get();
    }

    // Get instance count for current frame
    uint32_t getInstanceCount() const { return static_cast<uint32_t>(instances_.size()); }

    // Check if instancing is enabled and worth using
    bool shouldUseInstancing() const { return batches_.size() < instances_.size(); }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t frameCount_ = 0;
    uint32_t currentFrame_ = 0;

    // Per-frame GPU buffers (double/triple buffered)
    std::vector<VmaBuffer> instanceBuffers_;

    // CPU-side staging data for current frame
    std::vector<SceneInstanceData> instances_;

    // Batches for current frame (computed in finalize)
    std::vector<InstanceBatch> batches_;

    // Mapping from batch key to indices in instances_ vector
    std::unordered_map<InstanceBatchKey, std::vector<size_t>, InstanceBatchKeyHash> batchMap_;
};
