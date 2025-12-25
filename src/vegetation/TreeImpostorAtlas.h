#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>

#include "TreeOptions.h"
#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"

// Legacy impostor atlas configuration (17-view discrete)
// Layout: 2 rows x 9 columns = 18 cells (17 used, 1 unused)
// Row 0: 8 horizon views (0-315 degrees) + 1 top-down
// Row 1: 8 elevated views (45 degrees elevation) + 1 unused
struct ImpostorAtlasConfig {
    static constexpr int HORIZONTAL_ANGLES = 8;       // Views around the tree (every 45 degrees)
    static constexpr int VERTICAL_LEVELS = 2;         // Horizon + 45 degree elevation
    static constexpr int CELLS_PER_ROW = 9;           // 8 angles + 1 (top-down or unused)
    static constexpr int TOTAL_CELLS = 17;            // 8 + 8 + 1 top-down
    static constexpr int CELL_SIZE = 256;             // Pixels per cell (increased from 128)
    static constexpr int ATLAS_WIDTH = CELLS_PER_ROW * CELL_SIZE;   // 2304 pixels
    static constexpr int ATLAS_HEIGHT = VERTICAL_LEVELS * CELL_SIZE; // 512 pixels
};

// Octahedral impostor atlas configuration
// Uses hemi-octahedral mapping for continuous view coverage
// Grid is NxN where each cell is a captured view direction
struct OctahedralAtlasConfig {
    static constexpr int GRID_SIZE = 8;               // 8x8 = 64 views (good balance of quality vs memory)
    static constexpr int CELL_SIZE = 256;             // Pixels per cell
    static constexpr int ATLAS_WIDTH = GRID_SIZE * CELL_SIZE;   // 2048 pixels
    static constexpr int ATLAS_HEIGHT = GRID_SIZE * CELL_SIZE;  // 2048 pixels (square)
    static constexpr int TOTAL_CELLS = GRID_SIZE * GRID_SIZE;   // 64 views
};

// A single tree archetype's impostor data
struct TreeImpostorArchetype {
    std::string name;           // e.g., "oak_large", "pine_medium"
    std::string treeType;       // e.g., "oak", "pine"
    float boundingSphereRadius; // For billboard sizing (half of max dimension)
    float centerHeight;         // Height of tree center above base (for billboard offset)
    float treeHeight;           // Actual tree height (maxBounds.y - minBounds.y)
    float baseOffset;           // Offset from mesh origin to tree base (minBounds.y)

    // Atlas textures (owned by TreeImpostorAtlas)
    VkImageView albedoAlphaView = VK_NULL_HANDLE;
    VkImageView normalDepthAOView = VK_NULL_HANDLE;

    // Index into the atlas arrays
    uint32_t atlasIndex = 0;
};

// LOD settings with hysteresis support
struct TreeLODSettings {
    // Distance thresholds (used when useScreenSpaceError = false)
    float fullDetailDistance = 250.0f;     // Full geometry below this
    float impostorDistance = 50000.0f;     // Impostors visible up to this distance (very far)

    // Hysteresis (prevents flickering at LOD boundaries)
    float hysteresis = 5.0f;               // Dead zone for LOD transitions

    // Blending characteristics
    float blendRange = 10.0f;              // Distance over which to blend LODs
    float blendExponent = 1.0f;            // Blend curve (1.0 = linear)

    // Screen-space error LOD (Phase 4)
    bool useScreenSpaceError = true;       // Use screen-space error instead of distance
    float errorThresholdFull = 2.0f;       // Max screen error for full detail (pixels)
    float errorThresholdImpostor = 8.0f;   // Max screen error for impostor (pixels)
    float errorThresholdCull = 32.0f;      // Screen error beyond which to cull (sub-pixel)

    // Impostor settings
    bool enableImpostors = true;
    float impostorBrightness = 1.0f;       // Brightness adjustment for impostors
    float normalStrength = 1.0f;           // How much normals affect lighting

    // Octahedral impostor mode (Phase 6)
    bool useOctahedralMapping = true;      // Use octahedral vs legacy 17-view atlas
    bool enableFrameBlending = true;       // Blend between 3 nearest frames for smooth transitions

    // Debug settings
    bool enableDebugElevation = false;     // Override elevation angle for testing
    float debugElevation = 0.0f;           // Debug elevation angle in degrees (-90 to 90)
    bool debugShowCellIndex = false;       // Color-code impostors by cell index

    // Seasonal effects (global for all impostors)
    float autumnHueShift = 0.0f;           // 0 = summer green, 1 = full autumn colors
};

class TreeImpostorAtlas {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxArchetypes = 16;  // Maximum different tree types
    };

    static std::unique_ptr<TreeImpostorAtlas> create(const InitInfo& info);
    ~TreeImpostorAtlas();

    // Non-copyable, non-movable
    TreeImpostorAtlas(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas& operator=(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas(TreeImpostorAtlas&&) = delete;
    TreeImpostorAtlas& operator=(TreeImpostorAtlas&&) = delete;

    // Generate impostor atlas for a tree archetype
    // Returns archetype index, or -1 on failure
    int32_t generateArchetype(
        const std::string& name,
        const TreeOptions& options,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        VkImageView barkAlbedo,
        VkImageView barkNormal,
        VkImageView leafAlbedo,
        VkSampler sampler);

    // Get archetype by name
    const TreeImpostorArchetype* getArchetype(const std::string& name) const;
    const TreeImpostorArchetype* getArchetype(uint32_t index) const;

    // Get number of archetypes
    size_t getArchetypeCount() const { return archetypes_.size(); }

    // Get atlas array textures for binding (single array covers all archetypes)
    // Returns octahedral or legacy atlas based on current settings
    VkImageView getAlbedoAtlasArrayView() const;
    VkImageView getNormalAtlasArrayView() const;
    VkSampler getAtlasSampler() const { return atlasSampler_.get(); }

    // Get octahedral-specific atlas (for explicit access)
    VkImageView getOctaAlbedoAtlasArrayView() const { return octaAlbedoArrayView_.get(); }
    VkImageView getOctaNormalAtlasArrayView() const { return octaNormalArrayView_.get(); }

    // Check if octahedral atlas is available
    bool hasOctahedralAtlas() const { return octaAlbedoArrayImage_ != VK_NULL_HANDLE; }

    // Legacy per-archetype access (for preview/debug)
    VkImageView getAlbedoAtlasView(uint32_t archetypeIndex) const;
    VkImageView getNormalAtlasView(uint32_t archetypeIndex) const;

    // LOD settings
    TreeLODSettings& getLODSettings() { return lodSettings_; }
    const TreeLODSettings& getLODSettings() const { return lodSettings_; }

    // Get atlas image for UI preview (lazy-initializes ImGui descriptor on first call)
    VkImageView getPreviewImageView(uint32_t archetypeIndex) const;
    VkDescriptorSet getPreviewDescriptorSet(uint32_t archetypeIndex);
    VkDescriptorSet getOctahedralPreviewDescriptorSet(uint32_t archetypeIndex);

private:
    TreeImpostorAtlas() = default;
    bool initInternal(const InitInfo& info);

    bool createRenderPass();
    bool createCapturePipeline();
    bool createAtlasArrayTextures();  // Create the shared array textures (legacy)
    bool createOctahedralAtlasArrayTextures();  // Create octahedral array textures
    bool createAtlasResources(uint32_t archetypeIndex);  // Create per-layer framebuffer (legacy)
    bool createOctahedralAtlasResources(uint32_t archetypeIndex);  // Octahedral per-layer
    bool createSampler();
    bool createPreviewDescriptorSets();

    // Octahedral rendering helpers
    void renderOctahedralCell(
        VkCommandBuffer cmd,
        int cellX, int cellY,
        glm::vec3 viewDirection,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        float horizontalRadius,
        float boundingSphereRadius,
        float halfHeight,
        float centerHeight,
        float baseY,
        VkDescriptorSet branchDescSet,
        VkDescriptorSet leafDescSet);

    // Hemi-octahedral encoding/decoding (matching GLSL)
    static glm::vec2 hemiOctaEncode(glm::vec3 dir);
    static glm::vec3 hemiOctaDecode(glm::vec2 uv);

    // Render tree from a specific viewing angle to atlas cell
    void renderToCell(
        VkCommandBuffer cmd,
        int cellX, int cellY,
        float azimuth,           // Horizontal angle (0-360)
        float elevation,         // Vertical angle (0 = horizon, 90 = top-down)
        const Mesh& branchMesh,
        const std::vector<LeafInstanceGPU>& leafInstances,
        float horizontalRadius,      // Half of max horizontal dimension (X/Z)
        float boundingSphereRadius,  // Full 3D bounding sphere radius for depth clipping
        float halfHeight,            // Half of tree height (for billboard height)
        float centerHeight,          // Height of tree center above origin
        float baseY,                 // Y coordinate of tree base (for asymmetric projection)
        VkDescriptorSet branchDescSet,
        VkDescriptorSet leafDescSet);

    bool createLeafCapturePipeline();
    bool createLeafQuadMesh();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;

    // Render pass for capturing impostors (renders to G-buffer)
    ManagedRenderPass captureRenderPass_;

    // Pipeline for capturing tree geometry to G-buffer
    ManagedPipeline branchCapturePipeline_;
    ManagedPipeline leafCapturePipeline_;
    ManagedPipelineLayout capturePipelineLayout_;
    ManagedPipelineLayout leafCapturePipelineLayout_;
    ManagedDescriptorSetLayout captureDescriptorSetLayout_;
    ManagedDescriptorSetLayout leafCaptureDescriptorSetLayout_;

    // Leaf quad mesh for capture
    VkBuffer leafQuadVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadVertexAllocation_ = VK_NULL_HANDLE;
    VkBuffer leafQuadIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t leafQuadIndexCount_ = 0;

    // Per-archetype atlas textures
    struct AtlasTextures {
        VkImage albedoAlphaImage = VK_NULL_HANDLE;
        VmaAllocation albedoAlphaAllocation = VK_NULL_HANDLE;
        ManagedImageView albedoAlphaView;

        VkImage normalDepthAOImage = VK_NULL_HANDLE;
        VmaAllocation normalDepthAOAllocation = VK_NULL_HANDLE;
        ManagedImageView normalDepthAOView;

        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        ManagedImageView depthView;

        ManagedFramebuffer framebuffer;

        // Preview descriptor for ImGui
        VkDescriptorSet previewDescriptorSet = VK_NULL_HANDLE;
    };
    std::vector<AtlasTextures> atlasTextures_;

    // Archetype data
    std::vector<TreeImpostorArchetype> archetypes_;

    // Texture array for all archetypes (shared across all archetypes)
    VkImage albedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation albedoArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView albedoArrayView_;  // View of entire array for shader binding

    VkImage normalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation normalArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView normalArrayView_;  // View of entire array for shader binding

    uint32_t maxArchetypes_ = 16;  // Maximum layers in the array
    uint32_t currentArchetypeCount_ = 0;

    // Shared sampler for atlas textures
    ManagedSampler atlasSampler_;

    // Capture descriptor sets (reused)
    std::vector<VkDescriptorSet> captureDescriptorSets_;

    // LOD settings
    TreeLODSettings lodSettings_;

    // Leaf instance buffer for capture (temporary)
    VkBuffer leafCaptureBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafCaptureAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize leafCaptureBufferSize_ = 0;

    // Octahedral atlas resources
    VkImage octaAlbedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaAlbedoArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView octaAlbedoArrayView_;

    VkImage octaNormalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaNormalArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView octaNormalArrayView_;

    // Per-archetype octahedral atlas (depth buffers and framebuffers)
    struct OctaAtlasTextures {
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        ManagedImageView albedoView;   // View into octaAlbedoArrayImage_
        ManagedImageView normalView;   // View into octaNormalArrayImage_
        ManagedImageView depthView;
        ManagedFramebuffer framebuffer;
        VkDescriptorSet previewDescriptorSet = VK_NULL_HANDLE;
    };
    std::vector<OctaAtlasTextures> octaAtlasTextures_;
};
