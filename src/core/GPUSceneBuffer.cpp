#include "GPUSceneBuffer.h"
#include "Mesh.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

bool GPUSceneBuffer::init(VmaAllocator allocator, uint32_t frameCount) {
    allocator_ = allocator;
    frameCount_ = frameCount;

    // Pre-allocate CPU staging
    instances_.reserve(MAX_GPU_SCENE_OBJECTS);
    cullObjects_.reserve(MAX_GPU_SCENE_OBJECTS);
    batches_.reserve(256);

    // Create per-frame instance buffers (SSBO)
    VkDeviceSize instanceBufferSize = sizeof(GPUSceneInstanceData) * MAX_GPU_SCENE_OBJECTS;
    bool success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(instanceBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(instanceBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create instance buffers");
        cleanup();
        return false;
    }

    // Create cull object buffer (single, updated when scene changes)
    VkDeviceSize cullBufferSize = sizeof(GPUCullObjectData) * MAX_GPU_SCENE_OBJECTS;
    if (!VmaBufferFactory::createStorageBufferHostWritable(allocator, cullBufferSize, cullObjectBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create cull object buffer");
        cleanup();
        return false;
    }

    // Create per-frame indirect draw buffers
    VkDeviceSize indirectBufferSize = sizeof(GPUDrawIndexedIndirectCommand) * MAX_GPU_SCENE_OBJECTS;
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(indirectBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(0)  // GPU-only
        .build(indirectBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create indirect buffers");
        cleanup();
        return false;
    }

    // Create per-frame draw count buffers
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(sizeof(uint32_t))
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(drawCountBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create draw count buffers");
        cleanup();
        return false;
    }

    SDL_Log("GPUSceneBuffer: Initialized with %u frames, max %zu objects",
            frameCount, MAX_GPU_SCENE_OBJECTS);
    return true;
}

void GPUSceneBuffer::cleanup() {
    BufferUtils::destroyBuffers(allocator_, drawCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, indirectBuffers_);
    cullObjectBuffer_.reset();
    BufferUtils::destroyBuffers(allocator_, instanceBuffers_);

    instances_.clear();
    cullObjects_.clear();
    batches_.clear();
}

void GPUSceneBuffer::beginFrame(uint32_t frameIndex) {
    currentFrame_ = frameIndex;
    instances_.clear();
    cullObjects_.clear();
    batches_.clear();
    cullDataDirty_ = true;
}

int32_t GPUSceneBuffer::addObject(const Renderable& renderable) {
    if (instances_.size() >= MAX_GPU_SCENE_OBJECTS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Max objects reached (%zu)", MAX_GPU_SCENE_OBJECTS);
        return -1;
    }

    if (!renderable.mesh) {
        return -1;
    }

    uint32_t objectIndex = static_cast<uint32_t>(instances_.size());

    // Build instance data
    GPUSceneInstanceData instance{};
    instance.model = renderable.transform;
    instance.materialParams = glm::vec4(
        renderable.roughness,
        renderable.metallic,
        renderable.emissiveIntensity,
        renderable.opacity
    );
    instance.emissiveColor = glm::vec4(renderable.emissiveColor, 1.0f);
    instance.pbrFlags = renderable.pbrFlags;
    instance.alphaTestThreshold = renderable.alphaTestThreshold;
    instance.hueShift = renderable.hueShift;
    instance._pad1 = 0.0f;

    instances_.push_back(instance);

    // Build cull data
    const AABB& localBounds = renderable.mesh->getBounds();
    AABB worldBounds = localBounds.transformed(renderable.transform);

    GPUCullObjectData cullData{};
    glm::vec3 center = worldBounds.getCenter();
    glm::vec3 extents = worldBounds.getExtents();
    float radius = glm::length(extents);

    cullData.boundingSphere = glm::vec4(center, radius);
    cullData.aabbMin = glm::vec4(worldBounds.min, 0.0f);
    cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
    cullData.objectIndex = objectIndex;
    cullData.firstIndex = 0;
    cullData.indexCount = renderable.mesh->getIndexCount();
    cullData.vertexOffset = 0;

    cullObjects_.push_back(cullData);

    return static_cast<int32_t>(objectIndex);
}

void GPUSceneBuffer::finalize() {
    if (instances_.empty()) {
        return;
    }

    // Upload instance data to current frame's buffer
    void* mapped = instanceBuffers_.mappedPointers[currentFrame_];
    if (mapped) {
        memcpy(mapped, instances_.data(), instances_.size() * sizeof(GPUSceneInstanceData));
    }

    // Upload cull data (only if changed)
    if (cullDataDirty_) {
        void* cullMapped = cullObjectBuffer_.map();
        if (cullMapped) {
            memcpy(cullMapped, cullObjects_.data(), cullObjects_.size() * sizeof(GPUCullObjectData));
            cullObjectBuffer_.unmap();
        }
        cullDataDirty_ = false;
    }
}

void GPUSceneBuffer::resetDrawCount(vk::CommandBuffer cmd) {
    // Fill draw count buffer with zero
    cmd.fillBuffer(drawCountBuffers_.buffers[currentFrame_], 0, sizeof(uint32_t), 0);
}

uint32_t GPUSceneBuffer::getVisibleCount(uint32_t frameIndex) const {
    if (drawCountBuffers_.mappedPointers.empty()) {
        return 0;
    }
    return *static_cast<uint32_t*>(drawCountBuffers_.mappedPointers[frameIndex]);
}
