#pragma once

#include "VmaBuffer.h"

// ============================================================================
// Buffer Factory Functions
// ============================================================================

namespace VmaBufferFactory {

inline bool createStagingBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createUniformBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBufferHostReadable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createReadbackBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndexBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndirectBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndirectBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createDynamicVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

} // namespace VmaBufferFactory
