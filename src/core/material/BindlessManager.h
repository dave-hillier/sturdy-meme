#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "../vulkan/VmaBuffer.h"
#include "TextureRegistry.h"
#include <optional>
#include <vector>
#include <cstdint>

class VulkanContext;
class MaterialRegistry;

/**
 * GPU-side material data matching the GLSL MaterialData struct.
 * std430 layout, 48 bytes per material.
 */
struct GPUMaterialData {
    uint32_t albedoIndex;
    uint32_t normalIndex;
    uint32_t roughnessIndex;
    uint32_t metallicIndex;
    uint32_t aoIndex;
    uint32_t heightIndex;
    uint32_t emissiveIndex;
    uint32_t _pad0;
    float roughness;
    float metallic;
    float emissiveStrength;
    float alphaCutoff;
};
static_assert(sizeof(GPUMaterialData) == 48, "GPUMaterialData must be 48 bytes (std430)");

/**
 * BindlessManager - Manages bindless descriptor sets and material GPU buffer
 *
 * Owns:
 *   Set 1: Bindless texture array (sampler2D textures[])
 *   Set 2: Material SSBO (MaterialData materials[])
 *
 * Lifecycle:
 *   1. init() - Create layouts and allocate descriptor sets
 *   2. updateTextureDescriptors() - Write texture array from TextureRegistry
 *   3. uploadMaterialData() - Upload material data to GPU SSBO
 *   4. Bind sets 1 and 2 during rendering
 */
class BindlessManager {
public:
    static constexpr uint32_t TEXTURE_SET_INDEX = 1;
    static constexpr uint32_t MATERIAL_SET_INDEX = 2;
    static constexpr uint32_t DEFAULT_MAX_TEXTURES = 4096;
    static constexpr uint32_t DEFAULT_MAX_MATERIALS = 512;

    BindlessManager() = default;
    ~BindlessManager() = default;

    // Non-copyable, non-movable
    BindlessManager(const BindlessManager&) = delete;
    BindlessManager& operator=(const BindlessManager&) = delete;

    /**
     * Initialize descriptor set layouts, pools, and allocate sets.
     * maxTextures: upper bound on texture array size (capped to device limit)
     * framesInFlight: number of concurrent frames (typically 3)
     */
    bool init(VulkanContext& context, uint32_t maxTextures, uint32_t framesInFlight);

    /**
     * Write/update the bindless texture array descriptor from the TextureRegistry.
     * Only writes entries that have changed (checks dirty flag).
     */
    void updateTextureDescriptors(vk::Device device, const TextureRegistry& registry,
                                  uint32_t frameIndex);

    /**
     * Upload material data to the GPU SSBO for the given frame.
     */
    void uploadMaterialData(vk::Device device, const MaterialRegistry& registry, uint32_t frameIndex);

    /**
     * Bind the bindless descriptor sets (sets 1 and 2) to the command buffer.
     */
    void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
              vk::PipelineBindPoint bindPoint, uint32_t frameIndex) const;

    // Layout accessors for pipeline layout creation
    vk::DescriptorSetLayout getTextureSetLayout() const {
        return textureSetLayout_ ? **textureSetLayout_ : vk::DescriptorSetLayout{};
    }
    vk::DescriptorSetLayout getMaterialSetLayout() const {
        return materialSetLayout_ ? **materialSetLayout_ : vk::DescriptorSetLayout{};
    }

    bool isInitialized() const { return initialized_; }

    uint32_t getMaxTextures() const { return maxTextures_; }
    uint32_t getMaxMaterials() const { return maxMaterials_; }

    void cleanup();

private:
    bool createTextureSetLayout(const vk::raii::Device& device);
    bool createMaterialSetLayout(const vk::raii::Device& device);
    bool createDescriptorPool(const vk::raii::Device& device, uint32_t framesInFlight);
    bool allocateDescriptorSets(const vk::raii::Device& device, uint32_t framesInFlight);
    bool createMaterialBuffers(VmaAllocator allocator, uint32_t framesInFlight);

    // Descriptor set layouts
    std::optional<vk::raii::DescriptorSetLayout> textureSetLayout_;
    std::optional<vk::raii::DescriptorSetLayout> materialSetLayout_;

    // Descriptor pool (update-after-bind capable)
    std::optional<vk::raii::DescriptorPool> descriptorPool_;

    // Per-frame descriptor sets and buffers
    std::vector<vk::DescriptorSet> textureDescSets_;   // [frameIndex]
    std::vector<vk::DescriptorSet> materialDescSets_;   // [frameIndex]
    std::vector<VmaBuffer> materialBuffers_;             // [frameIndex]
    std::vector<void*> materialBufferMaps_;              // [frameIndex] persistent map

    uint32_t maxTextures_ = DEFAULT_MAX_TEXTURES;
    uint32_t maxMaterials_ = DEFAULT_MAX_MATERIALS;
    uint32_t framesInFlight_ = 0;
    bool initialized_ = false;
};
