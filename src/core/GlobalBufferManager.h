#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstring>
#include <memory>

#include "BufferUtils.h"
#include "Light.h"
#include "UBOs.h"

/**
 * GlobalBufferManager - Manages per-frame shared GPU buffers
 *
 * Consolidates uniform buffer, light buffer (SSBO), and bone matrices
 * buffer management that was scattered throughout Renderer.
 *
 * Uses the existing BufferUtils patterns for consistency.
 *
 * Usage:
 *   auto buffers = GlobalBufferManager::create(allocator, frameCount);
 *   if (!buffers) { handle error; }
 */
class GlobalBufferManager {
public:
    /**
     * Factory: Create and initialize buffer manager.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GlobalBufferManager> create(VmaAllocator allocator, uint32_t frameCount,
                                                        uint32_t maxBones = 128) {
        auto manager = std::unique_ptr<GlobalBufferManager>(new GlobalBufferManager());
        if (!manager->initInternal(allocator, frameCount, maxBones)) {
            return nullptr;
        }
        return manager;
    }

    ~GlobalBufferManager() {
        cleanup();
    }

    // Non-copyable, non-movable (stored via unique_ptr)
    GlobalBufferManager(GlobalBufferManager&&) = delete;
    GlobalBufferManager& operator=(GlobalBufferManager&&) = delete;
    GlobalBufferManager(const GlobalBufferManager&) = delete;
    GlobalBufferManager& operator=(const GlobalBufferManager&) = delete;

    // Per-frame buffer sets (public for descriptor binding)
    BufferUtils::PerFrameBufferSet uniformBuffers;
    BufferUtils::PerFrameBufferSet lightBuffers;
    BufferUtils::PerFrameBufferSet boneMatricesBuffers;
    BufferUtils::PerFrameBufferSet snowBuffers;         // Snow UBO (binding 14)
    BufferUtils::PerFrameBufferSet cloudShadowBuffers;  // Cloud shadow UBO (binding 15)

    // Configuration accessors
    uint32_t getFramesInFlight() const { return framesInFlight_; }
    uint32_t getMaxBoneMatrices() const { return maxBoneMatrices_; }

private:
    GlobalBufferManager() = default;

    bool initInternal(VmaAllocator allocator, uint32_t frameCount, uint32_t maxBones) {
        allocator_ = allocator;
        framesInFlight_ = frameCount;
        maxBoneMatrices_ = maxBones;

        // Create uniform buffers
        bool success = BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(frameCount)
            .setSize(sizeof(UniformBufferObject))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(uniformBuffers);

        if (!success) {
            return false;
        }

        // Create light buffers (SSBO)
        success = BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(frameCount)
            .setSize(sizeof(LightBuffer))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .build(lightBuffers);

        if (!success) {
            BufferUtils::destroyBuffers(allocator, uniformBuffers);
            return false;
        }

        // Create bone matrices buffers
        success = BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(frameCount)
            .setSize(sizeof(glm::mat4) * maxBones)
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .build(boneMatricesBuffers);

        if (!success) {
            BufferUtils::destroyBuffers(allocator, uniformBuffers);
            BufferUtils::destroyBuffers(allocator, lightBuffers);
            return false;
        }

        // Create snow UBO buffers (binding 14)
        success = BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(frameCount)
            .setSize(sizeof(SnowUBO))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(snowBuffers);

        if (!success) {
            BufferUtils::destroyBuffers(allocator, uniformBuffers);
            BufferUtils::destroyBuffers(allocator, lightBuffers);
            BufferUtils::destroyBuffers(allocator, boneMatricesBuffers);
            return false;
        }

        // Create cloud shadow UBO buffers (binding 15)
        success = BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(frameCount)
            .setSize(sizeof(CloudShadowUBO))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .build(cloudShadowBuffers);

        if (!success) {
            BufferUtils::destroyBuffers(allocator, uniformBuffers);
            BufferUtils::destroyBuffers(allocator, lightBuffers);
            BufferUtils::destroyBuffers(allocator, boneMatricesBuffers);
            BufferUtils::destroyBuffers(allocator, snowBuffers);
            return false;
        }

        return true;
    }

    void cleanup() {
        if (!allocator_) return;  // Not initialized
        BufferUtils::destroyBuffers(allocator_, uniformBuffers);
        BufferUtils::destroyBuffers(allocator_, lightBuffers);
        BufferUtils::destroyBuffers(allocator_, boneMatricesBuffers);
        BufferUtils::destroyBuffers(allocator_, snowBuffers);
        BufferUtils::destroyBuffers(allocator_, cloudShadowBuffers);
        allocator_ = nullptr;
    }

public:
    // Update the main UBO for a frame
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo) {
        if (frameIndex < uniformBuffers.mappedPointers.size()) {
            std::memcpy(uniformBuffers.mappedPointers[frameIndex], &ubo, sizeof(ubo));
        }
    }

    // Update light buffer for a frame
    void updateLightBuffer(uint32_t frameIndex, const LightBuffer& buffer) {
        if (frameIndex < lightBuffers.mappedPointers.size()) {
            std::memcpy(lightBuffers.mappedPointers[frameIndex], &buffer, sizeof(buffer));
        }
    }

    // Update bone matrices for a frame
    void updateBoneMatrices(uint32_t frameIndex, const std::vector<glm::mat4>& matrices) {
        if (frameIndex < boneMatricesBuffers.mappedPointers.size() && !matrices.empty()) {
            size_t copySize = std::min(matrices.size(), static_cast<size_t>(maxBoneMatrices_)) * sizeof(glm::mat4);
            std::memcpy(boneMatricesBuffers.mappedPointers[frameIndex], matrices.data(), copySize);
        }
    }

    // Update snow UBO for a frame
    void updateSnowBuffer(uint32_t frameIndex, const SnowUBO& snowUbo) {
        if (frameIndex < snowBuffers.mappedPointers.size()) {
            std::memcpy(snowBuffers.mappedPointers[frameIndex], &snowUbo, sizeof(snowUbo));
        }
    }

    // Update cloud shadow UBO for a frame
    void updateCloudShadowBuffer(uint32_t frameIndex, const CloudShadowUBO& cloudShadowUbo) {
        if (frameIndex < cloudShadowBuffers.mappedPointers.size()) {
            std::memcpy(cloudShadowBuffers.mappedPointers[frameIndex], &cloudShadowUbo, sizeof(cloudShadowUbo));
        }
    }

    // Descriptor buffer info accessors
    VkDescriptorBufferInfo getUniformBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < uniformBuffers.buffers.size()) {
            info.buffer = uniformBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(UniformBufferObject);
        }
        return info;
    }

    VkDescriptorBufferInfo getLightBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < lightBuffers.buffers.size()) {
            info.buffer = lightBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(LightBuffer);
        }
        return info;
    }

    VkDescriptorBufferInfo getBoneMatricesBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < boneMatricesBuffers.buffers.size()) {
            info.buffer = boneMatricesBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(glm::mat4) * maxBoneMatrices_;
        }
        return info;
    }

    VkDescriptorBufferInfo getSnowBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < snowBuffers.buffers.size()) {
            info.buffer = snowBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(SnowUBO);
        }
        return info;
    }

    VkDescriptorBufferInfo getCloudShadowBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < cloudShadowBuffers.buffers.size()) {
            info.buffer = cloudShadowBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(CloudShadowUBO);
        }
        return info;
    }

private:
    VmaAllocator allocator_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t maxBoneMatrices_ = 128;
};
