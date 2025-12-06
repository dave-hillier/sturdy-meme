#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstring>

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
 */
struct GlobalBufferManager {
    // Per-frame buffer sets
    BufferUtils::PerFrameBufferSet uniformBuffers;
    BufferUtils::PerFrameBufferSet lightBuffers;
    BufferUtils::PerFrameBufferSet boneMatricesBuffers;
    BufferUtils::PerFrameBufferSet snowBuffers;         // Snow UBO (binding 14)
    BufferUtils::PerFrameBufferSet cloudShadowBuffers;  // Cloud shadow UBO (binding 15)

    // Configuration
    uint32_t framesInFlight = 0;
    uint32_t maxBoneMatrices = 128;  // Maximum bone matrices per frame

    bool init(VmaAllocator allocator, uint32_t frameCount, uint32_t maxBones = 128) {
        framesInFlight = frameCount;
        maxBoneMatrices = maxBones;

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

    void destroy(VmaAllocator allocator) {
        BufferUtils::destroyBuffers(allocator, uniformBuffers);
        BufferUtils::destroyBuffers(allocator, lightBuffers);
        BufferUtils::destroyBuffers(allocator, boneMatricesBuffers);
        BufferUtils::destroyBuffers(allocator, snowBuffers);
        BufferUtils::destroyBuffers(allocator, cloudShadowBuffers);
    }

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
            size_t copySize = std::min(matrices.size(), static_cast<size_t>(maxBoneMatrices)) * sizeof(glm::mat4);
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
            info.range = sizeof(glm::mat4) * maxBoneMatrices;
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

    // Direct buffer accessors (for legacy code that needs VkBuffer directly)
    VkBuffer getUniformBuffer(uint32_t frameIndex) const {
        return frameIndex < uniformBuffers.buffers.size() ? uniformBuffers.buffers[frameIndex] : VK_NULL_HANDLE;
    }

    VkBuffer getLightBuffer(uint32_t frameIndex) const {
        return frameIndex < lightBuffers.buffers.size() ? lightBuffers.buffers[frameIndex] : VK_NULL_HANDLE;
    }

    VkBuffer getBoneMatricesBuffer(uint32_t frameIndex) const {
        return frameIndex < boneMatricesBuffers.buffers.size() ? boneMatricesBuffers.buffers[frameIndex] : VK_NULL_HANDLE;
    }

    VkBuffer getSnowBuffer(uint32_t frameIndex) const {
        return frameIndex < snowBuffers.buffers.size() ? snowBuffers.buffers[frameIndex] : VK_NULL_HANDLE;
    }

    VkBuffer getCloudShadowBuffer(uint32_t frameIndex) const {
        return frameIndex < cloudShadowBuffers.buffers.size() ? cloudShadowBuffers.buffers[frameIndex] : VK_NULL_HANDLE;
    }
};
