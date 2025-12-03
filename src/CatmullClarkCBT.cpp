#include "CatmullClarkCBT.h"
#include <SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    // Get bit field offset for a node at ceiling (maxDepth) level
    uint32_t cct_NodeBitID_BitField_CPU(uint32_t nodeId, int nodeDepth, int maxDepth, int faceCount) {
        uint32_t ceilNodeId = nodeId << (maxDepth - nodeDepth);
        return (2u * faceCount << maxDepth) + ceilNodeId;
    }

    // Set a single bit in the bitfield (leaf node marker)
    void cct_HeapWrite_BitField_CPU(std::vector<uint32_t>& heap, uint32_t nodeId, int nodeDepth, int maxDepth, int faceCount) {
        uint32_t bitID = cct_NodeBitID_BitField_CPU(nodeId, nodeDepth, maxDepth, faceCount);
        uint32_t heapIndex = bitID >> 5;
        uint32_t localBit = bitID & 31u;
        if (heapIndex < heap.size()) {
            heap[heapIndex] |= (1u << localBit);
        }
    }

    // Node bit ID for sum reduction tree
    uint32_t cct_NodeBitID_CPU(uint32_t id, int depth, int maxDepth, int faceCount) {
        uint32_t tmp1 = 2u * faceCount << depth;
        uint32_t tmp2 = uint32_t(1 + maxDepth - depth);
        return tmp1 + id * tmp2;
    }

    int cct_NodeBitSize_CPU(int depth, int maxDepth) {
        return maxDepth - depth + 1;
    }

    // Read a value from the heap at a specific node position
    uint32_t cct_HeapRead_CPU(const std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth, int faceCount) {
        uint32_t bitOffset = cct_NodeBitID_CPU(id, depth, maxDepth, faceCount);
        int bitCount = cct_NodeBitSize_CPU(depth, maxDepth);

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
    void cct_HeapWrite_CPU(std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth, int faceCount, uint32_t value) {
        uint32_t bitOffset = cct_NodeBitID_CPU(id, depth, maxDepth, faceCount);
        int bitCount = cct_NodeBitSize_CPU(depth, maxDepth);

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
    void cct_ComputeSumReduction_CPU(std::vector<uint32_t>& heap, int maxDepth, int faceCount, int leafDepth) {
        for (int depth = leafDepth - 1; depth >= 0; --depth) {
            uint32_t minNodeID = faceCount << depth;
            uint32_t maxNodeID = faceCount << (depth + 1);

            for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
                uint32_t leftChild = nodeID << 1;
                uint32_t rightChild = leftChild | 1u;

                uint32_t leftValue = cct_HeapRead_CPU(heap, leftChild, depth + 1, maxDepth, faceCount);
                uint32_t rightValue = cct_HeapRead_CPU(heap, rightChild, depth + 1, maxDepth, faceCount);

                cct_HeapWrite_CPU(heap, nodeID, depth, maxDepth, faceCount, leftValue + rightValue);
            }
        }
    }
}

uint32_t CatmullClarkCBT::calculateBufferSize(int maxDepth, int faceCount) {
    // For max depth D and F faces, the bitfield needs F * 2^D bits
    // Plus the sum reduction tree overhead
    uint64_t bitfieldBits = (uint64_t)faceCount << maxDepth;
    uint64_t bitfieldBytes = (bitfieldBits + 7) / 8;

    // Add overhead for sum reduction tree (roughly 2x bitfield size)
    uint64_t totalBytes = bitfieldBytes * 3;

    // Cap at 64MB
    return static_cast<uint32_t>(std::min(totalBytes, (uint64_t)64 * 1024 * 1024));
}

bool CatmullClarkCBT::init(const InitInfo& info) {
    maxDepth = info.maxDepth;
    faceCount = info.faceCount;
    bufferSize = calculateBufferSize(maxDepth, faceCount);

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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create Catmull-Clark CBT buffer");
        return false;
    }

    // Initialize CBT with all base mesh faces at depth 0
    std::vector<uint32_t> initData(bufferSize / sizeof(uint32_t), 0);

    // initData[0] stores (1 << maxDepth) as a marker for the max depth
    initData[0] = (1u << maxDepth);

    // Initialize all root bisectors (one per face, starting at depth 0)
    int initDepth = 0;  // Start with just the base faces
    uint32_t minNodeID = faceCount << initDepth;
    uint32_t maxNodeID = faceCount << (initDepth + 1);

    // Step 1: Set bitfield bits for all root nodes
    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cct_HeapWrite_BitField_CPU(initData, nodeID, initDepth, maxDepth, faceCount);
    }

    // Step 2: Initialize root node counts to 1
    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cct_HeapWrite_CPU(initData, nodeID, initDepth, maxDepth, faceCount, 1);
    }

    // Step 3: Compute sum reduction upward from initDepth to depth 0
    if (initDepth > 0) {
        cct_ComputeSumReduction_CPU(initData, maxDepth, faceCount, initDepth);
    }

    // Map the CBT buffer and write initialization data
    void* data;
    if (vmaMapMemory(info.allocator, allocation, &data) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map Catmull-Clark CBT buffer for initialization");
        return false;
    }
    memcpy(data, initData.data(), bufferSize);
    vmaUnmapMemory(info.allocator, allocation);

    SDL_Log("Catmull-Clark CBT initialized with %d base faces, max depth %d", faceCount, maxDepth);

    return true;
}

void CatmullClarkCBT::destroy(VmaAllocator allocator) {
    if (buffer) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}
