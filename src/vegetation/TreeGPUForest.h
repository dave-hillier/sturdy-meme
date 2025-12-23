#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include "core/DescriptorManager.h"
#include "core/BufferUtils.h"
#include "core/VulkanRAII.h"

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
    // Triple-buffered to match MAX_FRAMES_IN_FLIGHT = 3
    static constexpr uint32_t BUFFER_SET_COUNT = 3;

    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        std::string resourcePath;  // Path to resources (shaders, etc.)
        uint32_t maxTreeCount = 1000000;  // Default 1M trees
        uint32_t maxFullDetailTrees = 5000;  // Budget for full detail
        uint32_t maxImpostorTrees = 200000;  // Max visible impostors
    };

    static std::unique_ptr<TreeGPUForest> create(const InitInfo& info);
    ~TreeGPUForest();

    // Non-copyable
    TreeGPUForest(const TreeGPUForest&) = delete;
    TreeGPUForest& operator=(const TreeGPUForest&) = delete;

    // Height sampling function type (returns Y for given X, Z)
    using HeightFunction = std::function<float(float, float)>;

    // Initialize tree positions (procedural or from data)
    void generateProceduralForest(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   uint32_t treeCount, uint32_t seed = 12345);

    // Generate procedural forest with terrain height sampling
    void generateProceduralForest(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   uint32_t treeCount, const HeightFunction& getHeight, uint32_t seed = 12345);

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

    // Get buffers for rendering (returns the READ buffer set - previous frame's compute output)
    VkBuffer getFullDetailInstanceBuffer() const { return fullDetailBuffer_.buffer; }
    VkBuffer getImpostorInstanceBuffer() const { return impostorBuffers_.buffers[readBufferSet_]; }
    VkBuffer getIndirectBuffer() const { return indirectBuffers_.buffers[readBufferSet_]; }

    // Advance buffer sets after frame is complete (call at end of frame)
    // Matches ParticleSystem pattern: render reads from where compute wrote, compute moves to next
    void advanceBufferSet() {
        readBufferSet_ = writeBufferSet_;  // Read from where compute just wrote
        writeBufferSet_ = (writeBufferSet_ + 1) % BUFFER_SET_COUNT;  // Compute moves to next
    }

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
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;

    // Compute pipeline (RAII managed)
    ManagedPipeline cullPipeline_;
    ManagedPipelineLayout cullPipelineLayout_;
    ManagedDescriptorSetLayout descriptorSetLayout_;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Static buffers (single allocation)
    BufferUtils::SingleBuffer sourceBuffer_;        // Tree source data (static)
    BufferUtils::SingleBuffer clusterBuffer_;       // Cluster bounds
    BufferUtils::SingleBuffer clusterVisBuffer_;    // Cluster visibility (updated each frame)
    uint32_t* clusterVisMapped_ = nullptr;
    BufferUtils::SingleBuffer treeClusterMapBuffer_; // Tree -> cluster index mapping
    BufferUtils::SingleBuffer fullDetailBuffer_;    // Output: full detail instances

    // Triple-buffered output buffers (compute writes to one, graphics reads from another)
    BufferUtils::DoubleBufferedBufferSet impostorBuffers_;  // Output: impostor instances
    BufferUtils::DoubleBufferedBufferSet indirectBuffers_;  // Indirect draw commands

    uint32_t writeBufferSet_ = 0;  // Compute writes to this set
    uint32_t readBufferSet_ = 0;   // Graphics reads from this set (starts at 0, first frame reads compute output via barrier)

    // Per-frame uniform buffer
    BufferUtils::PerFrameBufferSet uniformBuffers_;  // Forest uniforms

    // Staging buffer for readback
    BufferUtils::SingleBuffer stagingBuffer_;

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
