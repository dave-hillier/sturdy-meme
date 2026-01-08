#include "SceneInstanceBuffer.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

bool SceneInstanceBuffer::init(VmaAllocator allocator, uint32_t frameCount) {
    allocator_ = allocator;
    frameCount_ = frameCount;

    instanceBuffers_.resize(frameCount);

    // Pre-allocate CPU staging
    instances_.reserve(MAX_SCENE_INSTANCES);

    // Create per-frame GPU buffers
    vk::DeviceSize bufferSize = sizeof(SceneInstanceData) * MAX_SCENE_INSTANCES;

    for (uint32_t i = 0; i < frameCount; ++i) {
        if (!VmaBufferFactory::createStorageBufferHostWritable(allocator, bufferSize, instanceBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "SceneInstanceBuffer: Failed to create instance buffer %u", i);
            cleanup();
            return false;
        }
    }

    SDL_Log("SceneInstanceBuffer: Initialized with %u frames, max %zu instances",
            frameCount, MAX_SCENE_INSTANCES);
    return true;
}

void SceneInstanceBuffer::cleanup() {
    instanceBuffers_.clear();
    instances_.clear();
    batches_.clear();
    batchMap_.clear();
}

void SceneInstanceBuffer::beginFrame(uint32_t frameIndex) {
    currentFrame_ = frameIndex;
    instances_.clear();
    batches_.clear();
    batchMap_.clear();
}

int32_t SceneInstanceBuffer::addInstance(const Renderable& renderable) {
    if (instances_.size() >= MAX_SCENE_INSTANCES) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneInstanceBuffer: Max instances reached (%zu)", MAX_SCENE_INSTANCES);
        return -1;
    }

    size_t instanceIndex = instances_.size();

    // Build instance data from renderable
    SceneInstanceData data{};
    data.model = renderable.transform;
    data.materialParams = glm::vec4(
        renderable.roughness,
        renderable.metallic,
        renderable.emissiveIntensity,
        renderable.opacity
    );
    data.emissiveColor = glm::vec4(renderable.emissiveColor, 1.0f);
    data.pbrFlags = renderable.pbrFlags;
    data.alphaTestThreshold = renderable.alphaTestThreshold;
    data._pad0 = 0.0f;
    data._pad1 = 0.0f;

    instances_.push_back(data);

    // Track for batching
    InstanceBatchKey key{renderable.materialId, renderable.mesh};
    batchMap_[key].push_back(instanceIndex);

    return static_cast<int32_t>(instanceIndex);
}

void SceneInstanceBuffer::finalize() {
    if (instances_.empty()) {
        return;
    }

    // Reorder instances to group by batch, and build batch metadata
    std::vector<SceneInstanceData> reorderedInstances;
    reorderedInstances.reserve(instances_.size());

    batches_.clear();
    batches_.reserve(batchMap_.size());

    for (auto& [key, indices] : batchMap_) {
        InstanceBatch batch{};
        batch.materialId = key.materialId;
        batch.mesh = key.mesh;
        batch.firstInstance = static_cast<uint32_t>(reorderedInstances.size());
        batch.instanceCount = static_cast<uint32_t>(indices.size());

        // Copy instances for this batch
        for (size_t idx : indices) {
            reorderedInstances.push_back(instances_[idx]);
        }

        batches_.push_back(batch);
    }

    // Replace with reordered data
    instances_ = std::move(reorderedInstances);

    // Upload to GPU
    VmaBuffer& buffer = instanceBuffers_[currentFrame_];
    void* mapped = buffer.map();
    if (mapped) {
        memcpy(mapped, instances_.data(), instances_.size() * sizeof(SceneInstanceData));
        buffer.unmap();
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SceneInstanceBuffer: Failed to map buffer for upload");
    }

    // Sort batches by material for even better coherence
    std::sort(batches_.begin(), batches_.end(), [](const InstanceBatch& a, const InstanceBatch& b) {
        return a.materialId < b.materialId;
    });
}
