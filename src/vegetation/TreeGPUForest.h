#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <memory>
#include "core/DescriptorManager.h"

struct TreeLODSettings;

// GPU-side tree source data (matches shader struct)
struct TreeSourceGPU {
    glm::vec4 positionScale;     // xyz = position, w = scale
    glm::vec4 rotationArchetype; // x = rotation, y = archetype, z = seed, w = unused
};
static_assert(sizeof(TreeSourceGPU) == 32, "TreeSourceGPU must be 32 bytes");

// GPU-side full detail output (matches shader struct)
struct TreeFullDetailGPU {
    glm::vec4 positionScale;     // xyz = position, w = scale
    glm::vec4 rotationBlend;     // x = rotation, y = blend, zw = unused
    uint32_t archetypeIndex;
    uint32_t treeIndex;
    glm::vec2 _pad;
};
static_assert(sizeof(TreeFullDetailGPU) == 48, "TreeFullDetailGPU must be 48 bytes");

// GPU-side impostor output (matches ImpostorInstanceGPU in TreeLODSystem.h)
// Layout matches vertex shader input attributes (locations 2-9)
struct TreeImpostorGPU {
    glm::vec3 position;          // location 2: world position
    float scale;                 // location 3: tree scale
    float rotation;              // location 4: Y-axis rotation
    uint32_t archetypeIndex;     // location 5: archetype for atlas lookup
    float blendFactor;           // location 6: LOD blend (0=full geo, 1=impostor)
    float hSize;                 // location 7: horizontal half-size (pre-scaled)
    float vSize;                 // location 8: vertical half-size (pre-scaled)
    float baseOffset;            // location 9: base offset (pre-scaled)
    float _padding;              // alignment padding
};
static_assert(sizeof(TreeImpostorGPU) == 44, "TreeImpostorGPU must match ImpostorInstanceGPU (44 bytes)");

// Forest uniforms (matches shader struct)
struct ForestUniformsGPU {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];

    float fullDetailDistance;
    float impostorStartDistance;
    float impostorEndDistance;
    float cullDistance;

    uint32_t fullDetailBudget;
    uint32_t totalTreeCount;
    uint32_t clusterCount;
    float clusterImpostorDist;

    glm::vec4 archetypeBounds[4];  // xyz = half-extents, w = baseOffset
};

// Cluster data for GPU (matches shader struct)
struct ClusterDataGPU {
    glm::vec4 centerRadius;      // xyz = center, w = radius
    glm::vec4 minBounds;         // xyz = min, w = tree count
    glm::vec4 maxBounds;         // xyz = max, w = first tree index
};
static_assert(sizeof(ClusterDataGPU) == 48, "ClusterDataGPU must be 48 bytes");

// Indirect draw commands (both use indexed draw for billboard mesh)
struct ForestIndirectCommands {
    VkDrawIndexedIndirectCommand fullDetailCmd;
    VkDrawIndexedIndirectCommand impostorCmd;
};

/**
 * GPU-driven forest system for rendering 1 million+ trees
 *
 * Uses compute shaders for:
 * - Hierarchical cluster culling
 * - Per-tree frustum and distance culling
 * - LOD selection (full detail vs impostor)
 * - Atomic output to instance buffers
 *
 * Rendering uses indirect draw commands populated by GPU
 */
class TreeGPUForest {
public:
    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        uint32_t maxTreeCount = 1000000;  // Default 1M trees
        uint32_t maxFullDetailTrees = 5000;  // Budget for full detail
        uint32_t maxImpostorTrees = 200000;  // Max visible impostors
    };

    static std::unique_ptr<TreeGPUForest> create(const InitInfo& info);
    ~TreeGPUForest();

    // Non-copyable
    TreeGPUForest(const TreeGPUForest&) = delete;
    TreeGPUForest& operator=(const TreeGPUForest&) = delete;

    // Initialize tree positions (procedural or from data)
    void generateProceduralForest(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   uint32_t treeCount, uint32_t seed = 12345);

    // Upload pre-generated tree data
    void uploadTreeData(const std::vector<TreeSourceGPU>& trees);

    // Set archetype bounds for impostor sizing
    void setArchetypeBounds(uint32_t archetype, const glm::vec3& halfExtents, float baseOffset);

    // Build cluster grid from current tree positions
    void buildClusterGrid(float cellSize);

    // Update cluster visibility (call before culling dispatch)
    void updateClusterVisibility(const glm::vec3& cameraPos,
                                  const std::array<glm::vec4, 6>& frustumPlanes,
                                  float clusterCullDistance,
                                  float clusterImpostorDistance);

    // Record compute commands for tree culling
    void recordCullingCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                               const glm::vec3& cameraPos,
                               const std::array<glm::vec4, 6>& frustumPlanes,
                               const TreeLODSettings& settings);

    // Get buffers for rendering
    VkBuffer getFullDetailInstanceBuffer() const { return fullDetailBuffer_; }
    VkBuffer getImpostorInstanceBuffer() const { return impostorBuffer_; }
    VkBuffer getIndirectBuffer() const { return indirectBuffer_; }

    // Get instance counts (read back from GPU - causes sync!)
    uint32_t readFullDetailCount();
    uint32_t readImpostorCount();

    // Stats
    uint32_t getTotalTreeCount() const { return currentTreeCount_; }
    uint32_t getClusterCount() const { return clusterCount_; }
    bool isReady() const { return initialized_; }

private:
    TreeGPUForest() = default;
    bool init(const InitInfo& info);
    bool createBuffers();
    bool createPipeline();
    bool createDescriptorSets();
    void cleanup();

    // Vulkan resources
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;

    // Compute pipeline
    VkPipeline cullPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cullPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> descriptorSets_{};

    // Buffers
    VkBuffer sourceBuffer_ = VK_NULL_HANDLE;        // Tree source data (static)
    VmaAllocation sourceAllocation_ = VK_NULL_HANDLE;

    VkBuffer clusterBuffer_ = VK_NULL_HANDLE;       // Cluster bounds
    VmaAllocation clusterAllocation_ = VK_NULL_HANDLE;

    VkBuffer clusterVisBuffer_ = VK_NULL_HANDLE;    // Cluster visibility (updated each frame)
    VmaAllocation clusterVisAllocation_ = VK_NULL_HANDLE;
    uint32_t* clusterVisMapped_ = nullptr;

    VkBuffer treeClusterMapBuffer_ = VK_NULL_HANDLE; // Tree -> cluster index mapping
    VmaAllocation treeClusterMapAllocation_ = VK_NULL_HANDLE;

    VkBuffer fullDetailBuffer_ = VK_NULL_HANDLE;    // Output: full detail instances
    VmaAllocation fullDetailAllocation_ = VK_NULL_HANDLE;

    VkBuffer impostorBuffer_ = VK_NULL_HANDLE;      // Output: impostor instances
    VmaAllocation impostorAllocation_ = VK_NULL_HANDLE;

    VkBuffer indirectBuffer_ = VK_NULL_HANDLE;      // Indirect draw commands
    VmaAllocation indirectAllocation_ = VK_NULL_HANDLE;

    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;       // Forest uniforms
    VmaAllocation uniformAllocation_ = VK_NULL_HANDLE;
    ForestUniformsGPU* uniformsMapped_ = nullptr;

    // Staging buffer for readback
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation_ = VK_NULL_HANDLE;

    // Configuration
    uint32_t maxTreeCount_ = 1000000;
    uint32_t maxFullDetailTrees_ = 5000;
    uint32_t maxImpostorTrees_ = 200000;

    // State
    uint32_t currentTreeCount_ = 0;
    uint32_t clusterCount_ = 0;
    bool initialized_ = false;

    // Cluster grid data (CPU side for visibility updates)
    struct ClusterInfo {
        glm::vec3 center;
        float radius;
        uint32_t treeCount;
        uint32_t firstTreeIndex;
    };
    std::vector<ClusterInfo> clusterInfos_;
    std::vector<uint32_t> treeToCluster_;  // Tree index -> cluster index

    // Archetype bounds
    std::array<glm::vec4, 4> archetypeBounds_{};
};
