#include "TreeSpatialIndex.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <unordered_map>
#include <cmath>

std::unique_ptr<TreeSpatialIndex> TreeSpatialIndex::create(const InitInfo& info) {
    auto index = std::unique_ptr<TreeSpatialIndex>(new TreeSpatialIndex());
    if (!index->initInternal(info)) {
        return nullptr;
    }
    return index;
}

TreeSpatialIndex::~TreeSpatialIndex() {
    cleanup();
}

bool TreeSpatialIndex::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    cellSize_ = info.cellSize;
    worldSize_ = info.worldSize;

    // Calculate grid dimensions
    // Add 1 to handle edge cases at world boundaries
    gridDimension_ = static_cast<int32_t>(std::ceil(worldSize_ / cellSize_)) + 1;
    gridOffset_ = gridDimension_ / 2; // Offset to handle negative coordinates

    // Pre-allocate cell array (all cells, even empty ones)
    uint32_t totalCells = static_cast<uint32_t>(gridDimension_ * gridDimension_);
    cells_.resize(totalCells);

    // Initialize all cells with their grid coordinates
    for (int32_t z = 0; z < gridDimension_; ++z) {
        for (int32_t x = 0; x < gridDimension_; ++x) {
            uint32_t idx = getCellIndex(x - gridOffset_, z - gridOffset_);
            cells_[idx].cellX = x - gridOffset_;
            cells_[idx].cellZ = z - gridOffset_;
            cells_[idx].treeCount = 0;
            cells_[idx].firstTreeIndex = 0;

            // Initialize bounds to world space position of cell
            float worldX = (x - gridOffset_) * cellSize_;
            float worldZ = (z - gridOffset_) * cellSize_;
            cells_[idx].boundsMin = glm::vec3(worldX, -1000.0f, worldZ);
            cells_[idx].boundsMax = glm::vec3(worldX + cellSize_, 1000.0f, worldZ + cellSize_);
        }
    }

    SDL_Log("TreeSpatialIndex: Initialized %dx%d grid (%.1fm cells, %.1fm world)",
            gridDimension_, gridDimension_, cellSize_, worldSize_);
    return true;
}

void TreeSpatialIndex::cleanup() {
    if (cellBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, cellBuffer_, cellAllocation_);
        cellBuffer_ = VK_NULL_HANDLE;
    }
    if (sortedTreeBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, sortedTreeBuffer_, sortedTreeAllocation_);
        sortedTreeBuffer_ = VK_NULL_HANDLE;
    }
}

void TreeSpatialIndex::worldToCell(const glm::vec3& worldPos, int32_t& cellX, int32_t& cellZ) const {
    cellX = static_cast<int32_t>(std::floor(worldPos.x / cellSize_));
    cellZ = static_cast<int32_t>(std::floor(worldPos.z / cellSize_));
}

uint32_t TreeSpatialIndex::getCellIndex(int32_t cellX, int32_t cellZ) const {
    // Convert cell coordinates to array index
    int32_t x = cellX + gridOffset_;
    int32_t z = cellZ + gridOffset_;

    // Clamp to valid range
    x = std::max(0, std::min(x, gridDimension_ - 1));
    z = std::max(0, std::min(z, gridDimension_ - 1));

    return static_cast<uint32_t>(z * gridDimension_ + x);
}

void TreeSpatialIndex::rebuild(const std::vector<glm::mat4>& transforms,
                                const std::vector<float>& scales) {
    if (transforms.empty()) {
        sortedTrees_.clear();
        nonEmptyCellCount_ = 0;

        // Reset all cells
        for (auto& cell : cells_) {
            cell.treeCount = 0;
            cell.firstTreeIndex = 0;
        }
        return;
    }

    // Structure to track which trees belong to each cell
    struct TreeCellAssignment {
        uint32_t treeIndex;
        uint32_t cellIndex;
    };
    std::vector<TreeCellAssignment> assignments;
    assignments.reserve(transforms.size());

    // Reset all cells
    for (auto& cell : cells_) {
        cell.treeCount = 0;
        cell.firstTreeIndex = 0;
        cell.boundsMin.y = std::numeric_limits<float>::max();
        cell.boundsMax.y = std::numeric_limits<float>::lowest();
    }

    // Assign each tree to a cell and update bounds
    for (size_t i = 0; i < transforms.size(); ++i) {
        // Extract position from the model matrix (column 3)
        glm::vec3 position = glm::vec3(transforms[i][3]);
        float scale = (i < scales.size()) ? scales[i] : 1.0f;

        int32_t cellX, cellZ;
        worldToCell(position, cellX, cellZ);

        uint32_t cellIdx = getCellIndex(cellX, cellZ);
        assignments.push_back({static_cast<uint32_t>(i), cellIdx});

        TreeCell& cell = cells_[cellIdx];
        cell.treeCount++;

        // Update Y bounds based on tree position
        // Assume trees have some vertical extent (scale-dependent)
        float treeHeight = scale * 15.0f; // Approximate tree height
        cell.boundsMin.y = std::min(cell.boundsMin.y, position.y);
        cell.boundsMax.y = std::max(cell.boundsMax.y, position.y + treeHeight);
    }

    // Sort assignments by cell index for contiguous tree ranges
    std::sort(assignments.begin(), assignments.end(),
              [](const TreeCellAssignment& a, const TreeCellAssignment& b) {
                  return a.cellIndex < b.cellIndex;
              });

    // Build sorted tree list and update cell firstTreeIndex
    sortedTrees_.clear();
    sortedTrees_.reserve(transforms.size());

    uint32_t currentCellIdx = UINT32_MAX;
    uint32_t treeIdx = 0;
    nonEmptyCellCount_ = 0;

    for (const auto& assignment : assignments) {
        if (assignment.cellIndex != currentCellIdx) {
            // New cell - update its firstTreeIndex
            currentCellIdx = assignment.cellIndex;
            cells_[currentCellIdx].firstTreeIndex = treeIdx;
            nonEmptyCellCount_++;
        }

        SortedTreeEntry entry;
        entry.originalTreeIndex = assignment.treeIndex;
        entry.cellIndex = assignment.cellIndex;
        sortedTrees_.push_back(entry);
        treeIdx++;
    }

    // Fix cells with no trees to have reasonable bounds
    for (auto& cell : cells_) {
        if (cell.treeCount == 0) {
            cell.boundsMin.y = 0.0f;
            cell.boundsMax.y = 0.0f;
        }
    }

    SDL_Log("TreeSpatialIndex: Rebuilt with %zu trees across %u non-empty cells",
            transforms.size(), nonEmptyCellCount_);
}

bool TreeSpatialIndex::uploadToGPU() {
    // Clean up old buffers
    cleanup();

    if (cells_.empty() || sortedTrees_.empty()) {
        SDL_Log("TreeSpatialIndex: No data to upload");
        return true; // Not an error, just nothing to upload
    }

    // Convert cells to GPU format
    cellsGPU_.clear();
    cellsGPU_.reserve(cells_.size());

    for (const auto& cell : cells_) {
        TreeCellGPU gpuCell;
        gpuCell.boundsMinAndFirst = glm::vec4(
            cell.boundsMin,
            *reinterpret_cast<const float*>(&cell.firstTreeIndex)
        );
        gpuCell.boundsMaxAndCount = glm::vec4(
            cell.boundsMax,
            *reinterpret_cast<const float*>(&cell.treeCount)
        );
        cellsGPU_.push_back(gpuCell);
    }

    // Create cell buffer
    cellBufferSize_ = cellsGPU_.size() * sizeof(TreeCellGPU);

    auto cellBufferInfo = vk::BufferCreateInfo{}
        .setSize(cellBufferSize_)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&cellBufferInfo), &allocInfo,
                        &cellBuffer_, &cellAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create cell buffer");
        return false;
    }

    // Create sorted tree buffer
    sortedTreeBufferSize_ = sortedTrees_.size() * sizeof(SortedTreeEntry);

    auto sortedBufferInfo = vk::BufferCreateInfo{}
        .setSize(sortedTreeBufferSize_)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&sortedBufferInfo), &allocInfo,
                        &sortedTreeBuffer_, &sortedTreeAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create sorted tree buffer");
        cleanup();
        return false;
    }

    // Upload data using staging buffers
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    // Stage and upload cell data
    VkBuffer cellStagingBuffer;
    VmaAllocation cellStagingAllocation;
    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&cellBufferInfo), &stagingAllocInfo,
                        &cellStagingBuffer, &cellStagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create cell staging buffer");
        cleanup();
        return false;
    }

    void* mappedData;
    vmaMapMemory(allocator_, cellStagingAllocation, &mappedData);
    memcpy(mappedData, cellsGPU_.data(), cellBufferSize_);
    vmaUnmapMemory(allocator_, cellStagingAllocation);

    // Stage and upload sorted tree data
    VkBuffer treeStagingBuffer;
    VmaAllocation treeStagingAllocation;
    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&sortedBufferInfo), &stagingAllocInfo,
                        &treeStagingBuffer, &treeStagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create tree staging buffer");
        vmaDestroyBuffer(allocator_, cellStagingBuffer, cellStagingAllocation);
        cleanup();
        return false;
    }

    vmaMapMemory(allocator_, treeStagingAllocation, &mappedData);
    memcpy(mappedData, sortedTrees_.data(), sortedTreeBufferSize_);
    vmaUnmapMemory(allocator_, treeStagingAllocation);

    // Note: In a real implementation, we'd need to submit copy commands here
    // For now, we'll use host-visible memory for simplicity
    // This should be refactored to use proper staging buffer copies

    // Re-create buffers with CPU_TO_GPU usage for now (simpler but less optimal)
    vmaDestroyBuffer(allocator_, cellStagingBuffer, cellStagingAllocation);
    vmaDestroyBuffer(allocator_, treeStagingBuffer, treeStagingAllocation);
    cleanup();

    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&cellBufferInfo), &stagingAllocInfo,
                        &cellBuffer_, &cellAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create cell buffer (CPU_TO_GPU)");
        return false;
    }

    vmaMapMemory(allocator_, cellAllocation_, &mappedData);
    memcpy(mappedData, cellsGPU_.data(), cellBufferSize_);
    vmaUnmapMemory(allocator_, cellAllocation_);

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&sortedBufferInfo), &stagingAllocInfo,
                        &sortedTreeBuffer_, &sortedTreeAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSpatialIndex: Failed to create tree buffer (CPU_TO_GPU)");
        cleanup();
        return false;
    }

    vmaMapMemory(allocator_, sortedTreeAllocation_, &mappedData);
    memcpy(mappedData, sortedTrees_.data(), sortedTreeBufferSize_);
    vmaUnmapMemory(allocator_, sortedTreeAllocation_);

    SDL_Log("TreeSpatialIndex: Uploaded %zu cells (%.2f KB) and %zu sorted trees (%.2f KB)",
            cellsGPU_.size(), cellBufferSize_ / 1024.0f,
            sortedTrees_.size(), sortedTreeBufferSize_ / 1024.0f);

    return true;
}
