#pragma once

#include "VmaBuffer.h"

// ============================================================================
// Buffer Builder - Fluent API for creating VMA buffers
// ============================================================================

class BufferBuilder {
public:
    explicit BufferBuilder(VmaAllocator allocator)
        : allocator_(allocator) {
        allocInfo_.usage = VMA_MEMORY_USAGE_AUTO;
    }

    // Size
    BufferBuilder& setSize(vk::DeviceSize size) {
        bufferInfo_.setSize(size);
        return *this;
    }

    // Usage flags
    BufferBuilder& asVertex() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eVertexBuffer);
        return *this;
    }

    BufferBuilder& asIndex() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eIndexBuffer);
        return *this;
    }

    BufferBuilder& asUniform() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eUniformBuffer);
        return *this;
    }

    BufferBuilder& asStorage() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eStorageBuffer);
        return *this;
    }

    BufferBuilder& asIndirect() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eIndirectBuffer);
        return *this;
    }

    BufferBuilder& asTransferSrc() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eTransferSrc);
        return *this;
    }

    BufferBuilder& asTransferDst() {
        bufferInfo_.setUsage(bufferInfo_.usage | vk::BufferUsageFlagBits::eTransferDst);
        return *this;
    }

    // Memory access patterns
    BufferBuilder& hostVisible() {
        allocInfo_.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
        return *this;
    }

    BufferBuilder& hostReadable() {
        allocInfo_.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
        return *this;
    }

    BufferBuilder& deviceLocal() {
        allocInfo_.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        return *this;
    }

    // Build
    bool build(VmaBuffer& outBuffer) const {
        auto info = bufferInfo_;
        info.setSharingMode(vk::SharingMode::eExclusive);
        return VmaBuffer::create(allocator_, info, allocInfo_, outBuffer);
    }

private:
    VmaAllocator allocator_;
    vk::BufferCreateInfo bufferInfo_{};
    VmaAllocationCreateInfo allocInfo_{};
};

// ============================================================================
// Buffer Factory - Convenience functions for common buffer types
// ============================================================================

namespace VmaBufferFactory {

inline bool createStagingBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asTransferSrc().hostVisible().build(outBuffer);
}

inline bool createVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asVertex().asTransferDst().build(outBuffer);
}

inline bool createIndexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asIndex().asTransferDst().build(outBuffer);
}

inline bool createUniformBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asUniform().hostVisible().build(outBuffer);
}

inline bool createStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asStorage().asTransferDst().asTransferSrc().deviceLocal().build(outBuffer);
}

inline bool createStorageBufferHostReadable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asStorage().asTransferDst().asTransferSrc().hostReadable().build(outBuffer);
}

inline bool createStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asStorage().asTransferDst().asTransferSrc().hostVisible().build(outBuffer);
}

inline bool createReadbackBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asTransferDst().hostReadable().build(outBuffer);
}

inline bool createVertexStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asVertex().asStorage().asTransferDst().deviceLocal().build(outBuffer);
}

inline bool createVertexStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asVertex().asStorage().asTransferDst().hostVisible().build(outBuffer);
}

inline bool createIndexBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asIndex().asTransferDst().hostVisible().build(outBuffer);
}

inline bool createIndirectBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asIndirect().asStorage().asTransferDst().deviceLocal().build(outBuffer);
}

inline bool createDynamicVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    return BufferBuilder(allocator).setSize(size).asVertex().asTransferDst().hostVisible().build(outBuffer);
}

} // namespace VmaBufferFactory
