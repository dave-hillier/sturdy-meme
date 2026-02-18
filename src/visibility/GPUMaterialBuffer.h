#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>
#include <memory>

#include "VisibilityBuffer.h"  // for GPUMaterial
#include "vulkan/VmaBuffer.h"
#include "vulkan/VmaBufferFactory.h"

class MaterialRegistry;

/**
 * GPUMaterialBuffer - Manages GPU-side material data for the visibility buffer resolve.
 *
 * Uploads GPUMaterial structs to a storage buffer that the resolve compute shader
 * reads to determine per-pixel material properties (base color, roughness, metallic,
 * texture indices, etc.).
 *
 * Usage:
 *   1. create() - Initialize with allocator
 *   2. uploadMaterials() - Upload material data from MaterialRegistry or manual list
 *   3. getBuffer()/getBufferSize()/getMaterialCount() - Bind to resolve descriptor set
 */
class GPUMaterialBuffer {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GPUMaterialBuffer(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator = VK_NULL_HANDLE;
        uint32_t maxMaterials = 256;
    };

    static std::unique_ptr<GPUMaterialBuffer> create(const InitInfo& info);

    ~GPUMaterialBuffer() = default;

    GPUMaterialBuffer(const GPUMaterialBuffer&) = delete;
    GPUMaterialBuffer& operator=(const GPUMaterialBuffer&) = delete;

    /**
     * Upload materials from a vector of GPUMaterial.
     * Returns true on success.
     */
    bool uploadMaterials(const std::vector<GPUMaterial>& materials);

    /**
     * Upload materials from MaterialRegistry.
     * Creates GPUMaterial entries from each registered material definition.
     * Texture indices default to UINT32_MAX (no texture) unless a texture
     * array is being used and indices are provided.
     */
    bool uploadFromRegistry(const MaterialRegistry& registry);

    /**
     * Upload materials with texture array indices from VisibilityBuffer.
     * Populates albedoTexIndex from the texture array layer mapping.
     */
    bool uploadFromRegistry(const MaterialRegistry& registry,
                            const VisibilityBuffer& visBuf);

    /**
     * Set or update a single material at the given index.
     */
    bool setMaterial(uint32_t index, const GPUMaterial& material);

    // Buffer accessors
    VkBuffer getBuffer() const { return buffer_.get(); }
    VkDeviceSize getBufferSize() const { return maxMaterials_ * sizeof(GPUMaterial); }
    uint32_t getMaterialCount() const { return materialCount_; }
    uint32_t getMaxMaterials() const { return maxMaterials_; }

private:
    bool initInternal(const InitInfo& info);

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t maxMaterials_ = 0;
    uint32_t materialCount_ = 0;

    VmaBuffer buffer_;
    void* mappedPtr_ = nullptr;
};
