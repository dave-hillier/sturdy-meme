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

// Legacy impostor atlas configuration (17 discrete views)
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

// Octahedral impostor atlas configuration (continuous view mapping)
// Uses octahedral projection for smooth interpolation across all view angles
// Single square texture with hemispherical coverage
struct OctahedralAtlasConfig {
    static constexpr int RESOLUTION = 512;            // Square texture resolution
    static constexpr int ATLAS_WIDTH = RESOLUTION;
    static constexpr int ATLAS_HEIGHT = RESOLUTION;

    // Capture settings: number of samples per axis for rendering
    // More samples = better quality but slower generation
    static constexpr int CAPTURE_SAMPLES = 32;        // 32x32 = 1024 view directions

    // Elevation range for capture (degrees)
    static constexpr float MIN_ELEVATION = 0.0f;      // Horizon
    static constexpr float MAX_ELEVATION = 90.0f;     // Top-down
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
    bool useOctahedralMapping = true;      // Use octahedral atlas (Phase 6) vs legacy 17-view

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

    // Get legacy atlas array textures for binding (17-view discrete atlas)
    VkImageView getAlbedoAtlasArrayView() const { return albedoArrayView_.get(); }
    VkImageView getNormalAtlasArrayView() const { return normalArrayView_.get(); }
    VkSampler getAtlasSampler() const { return atlasSampler_.get(); }

    // Get octahedral atlas array textures (continuous octahedral mapping)
    VkImageView getOctAlbedoAtlasArrayView() const { return octAlbedoArrayView_.get(); }
    VkImageView getOctNormalAtlasArrayView() const { return octNormalArrayView_.get(); }

    // Legacy per-archetype access (for preview/debug)
    VkImageView getAlbedoAtlasView(uint32_t archetypeIndex) const;
    VkImageView getNormalAtlasView(uint32_t archetypeIndex) const;

    // LOD settings
    TreeLODSettings& getLODSettings() { return lodSettings_; }
    const TreeLODSettings& getLODSettings() const { return lodSettings_; }

    // Get atlas image for UI preview (lazy-initializes ImGui descriptor on first call)
    VkImageView getPreviewImageView(uint32_t archetypeIndex) const;
    VkDescriptorSet getPreviewDescriptorSet(uint32_t archetypeIndex);

    // Get octahedral atlas preview (lazy-initializes ImGui descriptor on first call)
    VkDescriptorSet getOctPreviewDescriptorSet(uint32_t archetypeIndex);

private:
    TreeImpostorAtlas() = default;
    bool initInternal(const InitInfo& info);

    bool createRenderPass();
    bool createCapturePipeline();
    bool createAtlasArrayTextures();  // Create the shared legacy array textures
    bool createOctahedralAtlasArrayTextures();  // Create the octahedral array textures
    bool createAtlasResources(uint32_t archetypeIndex);  // Create per-layer framebuffer (legacy)
    bool createOctahedralAtlasResources(uint32_t archetypeIndex);  // Create per-layer framebuffer (octahedral)
    bool createSampler();
    bool createPreviewDescriptorSets();

    // Render tree from a specific viewing angle to legacy atlas cell
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

    // Render tree to octahedral atlas from a specific view direction
    void renderToOctahedral(
        VkCommandBuffer cmd,
        float azimuth,           // Horizontal angle (0-360)
        float elevation,         // Vertical angle (0 = horizon, 90 = top-down)
        const Mesh& branchMesh,
        const std::vector<LeafInstanceGPU>& leafInstances,
        float horizontalRadius,
        float boundingSphereRadius,
        float halfHeight,
        float centerHeight,
        float baseY,
        VkDescriptorSet branchDescSet,
        VkDescriptorSet leafDescSet);

    // Helper to compute octahedral UV from direction
    static glm::vec2 octahedralEncode(glm::vec3 dir);

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

    // Legacy texture array for all archetypes (17-view discrete atlas)
    VkImage albedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation albedoArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView albedoArrayView_;  // View of entire array for shader binding

    VkImage normalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation normalArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView normalArrayView_;  // View of entire array for shader binding

    // Octahedral texture array for all archetypes (continuous octahedral mapping)
    VkImage octAlbedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octAlbedoArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView octAlbedoArrayView_;

    VkImage octNormalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octNormalArrayAllocation_ = VK_NULL_HANDLE;
    ManagedImageView octNormalArrayView_;

    // Octahedral atlas per-archetype resources (framebuffers for capture)
    struct OctahedralAtlasTextures {
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        ManagedImageView depthView;
        ManagedImageView albedoLayerView;   // Per-layer view for framebuffer
        ManagedImageView normalLayerView;   // Per-layer view for framebuffer
        ManagedFramebuffer framebuffer;

        // Preview descriptor for ImGui
        VkDescriptorSet previewDescriptorSet = VK_NULL_HANDLE;
    };
    std::vector<OctahedralAtlasTextures> octAtlasTextures_;

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
};
