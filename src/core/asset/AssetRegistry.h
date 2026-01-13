#pragma once

#include "../Texture.h"
#include "../Mesh.h"
#include "../ShaderLoader.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>

/**
 * AssetRegistry - Centralized asset management with deduplication and caching
 *
 * The AssetRegistry provides:
 * - Path-based deduplication: Loading the same path twice returns the same shared_ptr
 * - Shared ownership: Assets use std::shared_ptr for automatic lifetime management
 * - Thread-safe loading: Mutex-protected for async loading compatibility
 *
 * Usage:
 *   AssetRegistry registry;
 *   registry.init(initContext);
 *
 *   // Load texture (second call returns cached shared_ptr)
 *   auto tex = registry.loadTexture("assets/textures/brick.png");
 *   auto tex2 = registry.loadTexture("assets/textures/brick.png"); // Same shared_ptr
 *
 *   // Use the texture
 *   VkImageView view = tex->getImageView();
 *
 *   // Texture is automatically freed when all shared_ptrs are released
 */
class AssetRegistry {
public:
    // Configuration for texture loading
    struct TextureLoadConfig {
        bool useSRGB = true;
        bool generateMipmaps = true;
        bool enableAnisotropy = true;
    };

    // Configuration for mesh creation
    struct MeshConfig {
        enum class Type {
            Cube,
            Plane,
            Sphere,
            Cylinder,
            Capsule,
            Disc,
            Rock,
            Custom
        };

        Type type = Type::Cube;

        // Plane/Disc parameters
        float width = 1.0f;
        float depth = 1.0f;
        float radius = 1.0f;

        // Sphere/Cylinder/Capsule parameters
        float height = 1.0f;
        int stacks = 16;
        int slices = 32;
        int segments = 32;

        // Rock parameters
        int subdivisions = 3;
        uint32_t seed = 0;
        float roughness = 0.3f;
        float asymmetry = 0.2f;

        // Disc UV scale
        float uvScale = 1.0f;
    };

    AssetRegistry() = default;
    ~AssetRegistry();

    // Non-copyable, non-movable (owns GPU resources)
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) = delete;
    AssetRegistry& operator=(AssetRegistry&&) = delete;

    /**
     * Initialize the registry with Vulkan context.
     * Must be called before any asset loading.
     */
    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              VmaAllocator allocator, VkCommandPool commandPool,
              VkQueue queue);

    /**
     * Cleanup all loaded assets.
     * Call before destroying Vulkan context.
     */
    void cleanup();

    // ========================================================================
    // Texture Management
    // ========================================================================

    /**
     * Load a texture from file with deduplication.
     * If the texture is already loaded, returns the cached shared_ptr.
     */
    std::shared_ptr<Texture> loadTexture(const std::string& path,
                                         const TextureLoadConfig& config = {});

    /**
     * Create a solid color texture (not path-cached).
     */
    std::shared_ptr<Texture> createSolidColorTexture(uint8_t r, uint8_t g,
                                                      uint8_t b, uint8_t a,
                                                      const std::string& name = "");

    /**
     * Register an externally-created texture for path-based lookup.
     */
    void registerTexture(std::shared_ptr<Texture> texture, const std::string& name);

    /**
     * Get texture by path/name. Returns nullptr if not found or expired.
     */
    std::shared_ptr<Texture> getTexture(const std::string& path);

    // ========================================================================
    // Mesh Management
    // ========================================================================

    /**
     * Create a procedural mesh.
     */
    std::shared_ptr<Mesh> createMesh(const MeshConfig& config,
                                     const std::string& name = "");

    /**
     * Create a mesh from custom geometry.
     */
    std::shared_ptr<Mesh> createCustomMesh(const std::vector<Vertex>& vertices,
                                           const std::vector<uint32_t>& indices,
                                           const std::string& name = "");

    /**
     * Register an externally-created mesh.
     */
    void registerMesh(std::shared_ptr<Mesh> mesh, const std::string& name);

    /**
     * Get mesh by name. Returns nullptr if not found or expired.
     */
    std::shared_ptr<Mesh> getMesh(const std::string& name);

    // ========================================================================
    // Shader Management
    // ========================================================================

    /**
     * Load a shader module from file with caching.
     */
    vk::ShaderModule loadShader(const std::string& path);

    /**
     * Get shader module by path. Returns VK_NULL_HANDLE if not found.
     */
    vk::ShaderModule getShader(const std::string& path) const;

    // ========================================================================
    // Statistics and Debugging
    // ========================================================================

    struct Stats {
        size_t textureCount = 0;
        size_t meshCount = 0;
        size_t shaderCount = 0;
        size_t textureCacheHits = 0;
        size_t shaderCacheHits = 0;
    };

    Stats getStats() const;

    /**
     * Remove expired weak references from caches.
     * Call periodically to free memory from caches pointing to destroyed assets.
     */
    void pruneExpiredEntries();

private:
    // Vulkan context
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    // Asset caches - use weak_ptr to allow automatic cleanup when not referenced
    std::unordered_map<std::string, std::weak_ptr<Texture>> textureCache_;
    std::unordered_map<std::string, std::weak_ptr<Mesh>> meshCache_;
    std::unordered_map<std::string, vk::ShaderModule> shaderCache_;

    // Statistics
    mutable size_t textureCacheHits_ = 0;
    mutable size_t shaderCacheHits_ = 0;

    // Thread safety
    mutable std::mutex mutex_;
};
