#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstring>

#include "BufferUtils.h"
#include "Light.h"

// Forward declarations
struct UniformBufferObject;

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

        return true;
    }

    void destroy(VmaAllocator allocator) {
        BufferUtils::destroyBuffers(allocator, uniformBuffers);
        BufferUtils::destroyBuffers(allocator, lightBuffers);
        BufferUtils::destroyBuffers(allocator, boneMatricesBuffers);
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
};
