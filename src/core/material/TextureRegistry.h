#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

/**
 * TextureRegistry - Central registry for bindless texture array management
 *
 * Assigns persistent integer indices to texture views + samplers. These indices
 * are used by shaders to sample from a global bindless texture array.
 *
 * Well-known placeholder indices:
 *   0 = white (1,1,1,1)  - default albedo/roughness/metallic/AO
 *   1 = black (0,0,0,1)  - default emissive/height
 *   2 = flat normal (0.5, 0.5, 1.0, 1.0)
 *
 * Usage:
 *   TextureRegistry registry;
 *   auto handle = registry.registerTexture(view, sampler);
 *   // Later in shader: texture(globalTextures[handle.index], uv)
 */
class TextureRegistry {
public:
    struct Handle {
        uint32_t index = UINT32_MAX;
        bool isValid() const { return index != UINT32_MAX; }
    };

    static constexpr uint32_t PLACEHOLDER_WHITE  = 0;
    static constexpr uint32_t PLACEHOLDER_BLACK  = 1;
    static constexpr uint32_t PLACEHOLDER_NORMAL = 2;
    static constexpr uint32_t FIRST_USER_INDEX   = 3;

    TextureRegistry() = default;

    /**
     * Initialize with placeholder textures that occupy indices 0-2.
     * Must be called before any registerTexture() calls.
     */
    void init(VkImageView whiteView, VkSampler whiteSampler,
              VkImageView blackView, VkSampler blackSampler,
              VkImageView normalView, VkSampler normalSampler);

    /**
     * Register a texture and get a persistent handle.
     * Returns a handle with the array index for bindless access.
     */
    Handle registerTexture(VkImageView view, VkSampler sampler);

    /**
     * Unregister a texture, freeing its slot for reuse.
     * The caller must ensure no in-flight frames reference this index.
     */
    void unregisterTexture(Handle handle);

    /**
     * Get image view and sampler for a given index (for descriptor writes).
     */
    VkImageView getImageView(uint32_t index) const;
    VkSampler getSampler(uint32_t index) const;

    /**
     * Total number of registered entries (including placeholders and free slots).
     * This is the size needed for the descriptor array allocation.
     */
    uint32_t getArraySize() const { return static_cast<uint32_t>(entries_.size()); }

    /**
     * Number of active (non-free) texture entries.
     */
    uint32_t getActiveCount() const { return activeCount_; }

    /**
     * Check if the registry has pending changes since last descriptor update.
     */
    bool isDirty() const { return dirty_; }

    /**
     * Clear the dirty flag after updating descriptors.
     */
    void clearDirty() { dirty_ = false; }

    /**
     * Check if initialized with placeholder textures.
     */
    bool isInitialized() const { return initialized_; }

private:
    struct Entry {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool active = false;
    };

    std::vector<Entry> entries_;
    std::vector<uint32_t> freeList_;
    uint32_t activeCount_ = 0;
    bool dirty_ = false;
    bool initialized_ = false;
};
