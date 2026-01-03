#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include "SDFConfig.h"
#include "InitContext.h"

/**
 * SDFAtlas - Runtime management of Signed Distance Field textures
 *
 * Manages a 3D texture array containing SDFs for multiple meshes.
 * Each mesh's SDF is stored as a layer in the array.
 *
 * GPU Layout:
 * - 3D Texture Array: resolution³ × numEntries
 * - Format: R16F (signed distance in local units)
 * - Entry buffer: per-entry transforms for world-space lookup
 */
class SDFAtlas {
public:
    // Per-entry metadata for GPU lookup
    struct SDFEntry {
        glm::vec4 boundsMin;      // xyz = world min, w = unused
        glm::vec4 boundsMax;      // xyz = world max, w = unused
        glm::vec4 invScale;       // xyz = 1/(max-min), w = atlas layer index
        glm::mat4 worldToLocal;   // Transform from world to local UV space
    };

    // Instance data for rendering (per-object placement in world)
    struct SDFInstance {
        uint32_t entryIndex;      // Index into SDFEntry array
        glm::mat4 transform;      // World transform of this instance
    };

    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue transferQueue;
        std::string sdfPath;           // Path to SDF files
        SDFConfig config;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    static std::unique_ptr<SDFAtlas> create(const InitInfo& info);
    static std::unique_ptr<SDFAtlas> create(const InitContext& ctx, const SDFConfig& config);

    ~SDFAtlas();

    // Non-copyable
    SDFAtlas(const SDFAtlas&) = delete;
    SDFAtlas& operator=(const SDFAtlas&) = delete;

    /**
     * Load SDF for a mesh from file.
     * Returns entry index, or -1 on failure.
     * File format: raw R16F data, resolution³ voxels.
     */
    int loadSDF(const std::string& meshName);

    /**
     * Get entry index for a previously loaded mesh.
     * Returns -1 if not loaded.
     */
    int getEntryIndex(const std::string& meshName) const;

    /**
     * Update instance buffer with current frame's visible instances.
     * Call before SDF-AO rendering.
     */
    void updateInstances(const std::vector<SDFInstance>& instances);

    // GPU resource accessors
    VkImageView getAtlasView() const { return atlasView_; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }
    VkBuffer getEntryBuffer() const { return entryBuffer_; }
    VkBuffer getInstanceBuffer() const { return instanceBuffer_; }
    uint32_t getInstanceCount() const { return instanceCount_; }
    uint32_t getEntryCount() const { return static_cast<uint32_t>(entries_.size()); }

    const SDFConfig& getConfig() const { return config_; }

private:
    SDFAtlas() = default;
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createAtlasTexture();
    bool createBuffers();
    bool uploadSDFData(uint32_t layerIndex, const void* data, size_t dataSize);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    std::string sdfPath_;
    SDFConfig config_;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // 3D texture array for SDF data
    VkImage atlasImage_ = VK_NULL_HANDLE;
    VkImageView atlasView_ = VK_NULL_HANDLE;
    VmaAllocation atlasAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> sampler_;

    // Entry metadata buffer (GPU)
    VkBuffer entryBuffer_ = VK_NULL_HANDLE;
    VmaAllocation entryAllocation_ = VK_NULL_HANDLE;
    std::vector<SDFEntry> entries_;

    // Instance buffer (GPU, updated per frame)
    VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
    VmaAllocation instanceAllocation_ = VK_NULL_HANDLE;
    uint32_t instanceCount_ = 0;
    uint32_t maxInstances_ = 1024;

    // Mesh name to entry index mapping
    std::unordered_map<std::string, int> meshToEntry_;

    // Current layer allocation
    uint32_t nextLayerIndex_ = 0;
};
