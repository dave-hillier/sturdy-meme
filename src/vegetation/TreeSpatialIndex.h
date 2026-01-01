#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>

// CPU-side cell structure
struct TreeCell {
    glm::vec3 boundsMin;        // AABB minimum
    glm::vec3 boundsMax;        // AABB maximum
    uint32_t firstTreeIndex;    // Index into sorted tree buffer
    uint32_t treeCount;         // Number of trees in this cell
    int32_t cellX;              // Grid X coordinate
    int32_t cellZ;              // Grid Z coordinate
};

// GPU cell data (packed for compute shader culling - 32 bytes)
// GLSL: vec4 boundsMinAndFirst, vec4 boundsMaxAndCount
struct TreeCellGPU {
    glm::vec4 boundsMinAndFirst;    // xyz = boundsMin, w = firstTreeIndex as float bits
    glm::vec4 boundsMaxAndCount;    // xyz = boundsMax, w = treeCount as float bits
};
static_assert(sizeof(TreeCellGPU) == 32, "TreeCellGPU must be 32 bytes for std430 layout");

// Sorted tree entry (for GPU - indices into original tree arrays)
struct SortedTreeEntry {
    uint32_t originalTreeIndex;     // Index into original tree data
    uint32_t cellIndex;             // Which cell this tree belongs to
};

/**
 * Spatial index for tree instances using a uniform grid.
 *
 * Divides the world into cells of configurable size. Each cell stores:
 * - Axis-aligned bounding box (AABB)
 * - Index range into a sorted tree buffer
 * - Tree count
 *
 * This enables hierarchical culling:
 * 1. First cull cells against frustum (1000s of cells)
 * 2. Then only process trees in visible cells
 */
class TreeSpatialIndex {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        float cellSize = 64.0f;     // World units per cell side
        float worldSize = 4096.0f;  // Total world size (for grid allocation)
        uint32_t maxFramesInFlight = 3;  // For triple-buffering GPU buffers
    };

    /**
     * Factory: Create and initialize TreeSpatialIndex.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TreeSpatialIndex> create(const InitInfo& info);
    ~TreeSpatialIndex();

    // Non-copyable, non-movable
    TreeSpatialIndex(const TreeSpatialIndex&) = delete;
    TreeSpatialIndex& operator=(const TreeSpatialIndex&) = delete;
    TreeSpatialIndex(TreeSpatialIndex&&) = delete;
    TreeSpatialIndex& operator=(TreeSpatialIndex&&) = delete;

    /**
     * Rebuild the spatial index from tree transforms.
     * Call when trees are added/removed/moved.
     *
     * The originalTreeIndex stored will be the index into this transforms array,
     * which should match the TreeCullData buffer ordering.
     *
     * @param transforms Model matrices for each tree (position extracted from column 3)
     * @param scales Scale factor for each tree (for estimating tree height bounds)
     */
    void rebuild(const std::vector<glm::mat4>& transforms,
                 const std::vector<float>& scales);

    /**
     * Upload cell and sorted tree data to GPU buffers.
     * Call after rebuild() to make data available for shaders.
     */
    bool uploadToGPU();

    // Accessors for GPU buffers (frame-indexed to prevent race conditions)
    VkBuffer getCellBuffer(uint32_t frameIndex) const {
        return cellBuffers_[frameIndex % maxFramesInFlight_];
    }
    VkDeviceSize getCellBufferSize() const { return cellBufferSize_; }

    VkBuffer getSortedTreeBuffer(uint32_t frameIndex) const {
        return sortedTreeBuffers_[frameIndex % maxFramesInFlight_];
    }
    VkDeviceSize getSortedTreeBufferSize() const { return sortedTreeBufferSize_; }

    // Accessors for cell data
    uint32_t getCellCount() const { return static_cast<uint32_t>(cells_.size()); }
    uint32_t getNonEmptyCellCount() const { return nonEmptyCellCount_; }
    float getCellSize() const { return cellSize_; }
    int32_t getGridDimension() const { return gridDimension_; }

    // Get the sorted tree indices (for CPU-side access)
    const std::vector<SortedTreeEntry>& getSortedTrees() const { return sortedTrees_; }

    // Get original index from sorted index
    uint32_t getOriginalTreeIndex(uint32_t sortedIndex) const {
        return sortedTrees_[sortedIndex].originalTreeIndex;
    }

    // Check if buffers are valid
    bool isValid() const {
        return !cellBuffers_.empty() && cellBuffers_[0] != VK_NULL_HANDLE &&
               !sortedTreeBuffers_.empty() && sortedTreeBuffers_[0] != VK_NULL_HANDLE;
    }

private:
    TreeSpatialIndex() = default;
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Calculate cell coordinates for a world position
    void worldToCell(const glm::vec3& worldPos, int32_t& cellX, int32_t& cellZ) const;

    // Get 1D cell index from 2D coordinates
    uint32_t getCellIndex(int32_t cellX, int32_t cellZ) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    float cellSize_ = 64.0f;
    float worldSize_ = 4096.0f;
    int32_t gridDimension_ = 0;         // Number of cells per side
    int32_t gridOffset_ = 0;            // Offset to handle negative coordinates
    uint32_t maxFramesInFlight_ = 3;    // Number of frames for triple-buffering

    // CPU-side data
    std::vector<TreeCell> cells_;               // All cells in the grid
    std::vector<TreeCellGPU> cellsGPU_;        // GPU-format cell data
    std::vector<SortedTreeEntry> sortedTrees_; // Trees sorted by cell
    uint32_t nonEmptyCellCount_ = 0;            // Number of cells with trees

    // GPU buffers (triple-buffered to prevent race conditions when updating
    // while frames are in-flight - each frame uses its own copy)
    std::vector<VkBuffer> cellBuffers_;
    std::vector<VmaAllocation> cellAllocations_;
    VkDeviceSize cellBufferSize_ = 0;

    std::vector<VkBuffer> sortedTreeBuffers_;
    std::vector<VmaAllocation> sortedTreeAllocations_;
    VkDeviceSize sortedTreeBufferSize_ = 0;
};
