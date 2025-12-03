#include "TerrainCBT.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    // Get bit field offset for a node at ceiling (maxDepth) level
    uint32_t cbt_NodeBitID_BitField_CPU(uint32_t nodeId, int nodeDepth, int maxDepth) {
        uint32_t ceilNodeId = nodeId << (maxDepth - nodeDepth);
        return (2u << maxDepth) + ceilNodeId;
    }

    // Set a single bit in the bitfield (leaf node marker)
    void cbt_HeapWrite_BitField_CPU(std::vector<uint32_t>& heap, uint32_t nodeId, int nodeDepth, int maxDepth) {
        uint32_t bitID = cbt_NodeBitID_BitField_CPU(nodeId, nodeDepth, maxDepth);
        uint32_t heapIndex = bitID >> 5;
        uint32_t localBit = bitID & 31u;
        if (heapIndex < heap.size()) {
            heap[heapIndex] |= (1u << localBit);
        }
    }

    // Node bit ID for sum reduction tree
    uint32_t cbt_NodeBitID_CPU(uint32_t id, int depth, int maxDepth) {
        uint32_t tmp1 = 2u << depth;
        uint32_t tmp2 = uint32_t(1 + maxDepth - depth);
        return tmp1 + id * tmp2;
    }

    int cbt_NodeBitSize_CPU(int depth, int maxDepth) {
        return maxDepth - depth + 1;
    }

    // Read a value from the heap at a specific node position
    uint32_t cbt_HeapRead_CPU(const std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth) {
        uint32_t bitOffset = cbt_NodeBitID_CPU(id, depth, maxDepth);
        int bitCount = cbt_NodeBitSize_CPU(depth, maxDepth);

        uint32_t heapIndex = bitOffset >> 5;
        uint32_t localBitOffset = bitOffset & 31u;

        uint32_t bitCountLSB = std::min(32u - localBitOffset, (uint32_t)bitCount);
        uint32_t bitCountMSB = (uint32_t)bitCount - bitCountLSB;

        uint32_t maskLSB = (1u << bitCountLSB) - 1u;
        uint32_t lsb = (heap[heapIndex] >> localBitOffset) & maskLSB;

        uint32_t msb = 0;
        if (bitCountMSB > 0 && heapIndex + 1 < heap.size()) {
            uint32_t maskMSB = (1u << bitCountMSB) - 1u;
            msb = heap[heapIndex + 1] & maskMSB;
        }

        return lsb | (msb << bitCountLSB);
    }

    // Write a value to the heap at a specific node position
    void cbt_HeapWrite_CPU(std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth, uint32_t value) {
        uint32_t bitOffset = cbt_NodeBitID_CPU(id, depth, maxDepth);
        int bitCount = cbt_NodeBitSize_CPU(depth, maxDepth);

        uint32_t heapIndex = bitOffset >> 5;
        uint32_t localBitOffset = bitOffset & 31u;

        uint32_t bitCountLSB = std::min(32u - localBitOffset, (uint32_t)bitCount);
        uint32_t bitCountMSB = (uint32_t)bitCount - bitCountLSB;

        // Clear and set LSB part
        uint32_t maskLSB = ~(((1u << bitCountLSB) - 1u) << localBitOffset);
        heap[heapIndex] = (heap[heapIndex] & maskLSB) | ((value & ((1u << bitCountLSB) - 1u)) << localBitOffset);

        // If value spans two words, write MSB part
        if (bitCountMSB > 0 && heapIndex + 1 < heap.size()) {
            uint32_t maskMSB = ~((1u << bitCountMSB) - 1u);
            heap[heapIndex + 1] = (heap[heapIndex + 1] & maskMSB) | (value >> bitCountLSB);
        }
    }

    // Compute sum reduction from leaf nodes up to root
    void cbt_ComputeSumReduction_CPU(std::vector<uint32_t>& heap, int maxDepth, int leafDepth) {
        for (int depth = leafDepth - 1; depth >= 0; --depth) {
            uint32_t minNodeID = 1u << depth;
            uint32_t maxNodeID = 2u << depth;

            for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
                uint32_t leftChild = nodeID << 1;
                uint32_t rightChild = leftChild | 1u;

                uint32_t leftValue = cbt_HeapRead_CPU(heap, leftChild, depth + 1, maxDepth);
                uint32_t rightValue = cbt_HeapRead_CPU(heap, rightChild, depth + 1, maxDepth);

                cbt_HeapWrite_CPU(heap, nodeID, depth, maxDepth, leftValue + rightValue);
            }
        }
    }
}

uint32_t TerrainCBT::calculateBufferSize(int maxDepth) {
    // For max depth D, the bitfield needs 2^(D-1) bits = 2^(D-4) bytes
    // Plus the sum reduction tree overhead
    uint64_t bitfieldBytes = 1ull << (maxDepth - 1);
    // Add some extra for alignment and sum reduction, cap at 256MB
    // (maxDepth 28 requires ~128MB for 1m resolution on 16km terrain)
    return static_cast<uint32_t>(std::min(bitfieldBytes * 2, (uint64_t)256 * 1024 * 1024));
}

bool TerrainCBT::init(const InitInfo& info) {
    maxDepth = info.maxDepth;
    bufferSize = calculateBufferSize(maxDepth);

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (vmaCreateBuffer(info.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create CBT buffer");
        return false;
    }

    // Initialize CBT with triangles covering the terrain quad
    std::vector<uint32_t> initData(bufferSize / sizeof(uint32_t), 0);
    int initDepth = info.initDepth;

    // heap[0] stores (1 << maxDepth) as a marker for the max depth
    initData[0] = (1u << maxDepth);

    // Step 1: Set bitfield bits for all leaf nodes at initDepth
    uint32_t minNodeID = 1u << initDepth;
    uint32_t maxNodeID = 2u << initDepth;

    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cbt_HeapWrite_BitField_CPU(initData, nodeID, initDepth, maxDepth);
    }

    // Step 2: Initialize leaf node counts to 1
    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cbt_HeapWrite_CPU(initData, nodeID, initDepth, maxDepth, 1);
    }

    // Step 3: Compute sum reduction upward from initDepth to depth 0
    cbt_ComputeSumReduction_CPU(initData, maxDepth, initDepth);

    // Verify: root should have correct count
    uint32_t rootCount = cbt_HeapRead_CPU(initData, 1, 0, maxDepth);
    SDL_Log("CBT root count after init: %u", rootCount);

    // Map the CBT buffer and write initialization data
    void* data;
    if (vmaMapMemory(info.allocator, allocation, &data) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map CBT buffer for initialization");
        return false;
    }
    memcpy(data, initData.data(), bufferSize);
    vmaUnmapMemory(info.allocator, allocation);

    SDL_Log("CBT initialized with %u triangles at depth %d, max depth %d",
            (maxNodeID - minNodeID), initDepth, maxDepth);

    return true;
}

void TerrainCBT::destroy(VmaAllocator allocator) {
    if (buffer) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}
