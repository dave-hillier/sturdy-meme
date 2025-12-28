#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <optional>

#include "TreeOptions.h"
#include "CullCommon.h"
#include "DescriptorManager.h"

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
    float fullDetailDistance = TreeLODConstants::FULL_DETAIL_DISTANCE;
    float impostorDistance = 50000.0f;     // Impostors visible up to this distance (very far)

    // Hysteresis (prevents flickering at LOD boundaries)
    float hysteresis = TreeLODConstants::HYSTERESIS;

    // Blending characteristics
    float blendRange = TreeLODConstants::BLEND_RANGE;
    float blendExponent = 1.0f;            // Blend curve (1.0 = linear)

    // Screen-space error LOD
    // Screen error is HIGH when close (object large on screen), LOW when far (object small)
    // Logic: close (high error) = full geometry, far (low error) = impostor/cull
    bool useScreenSpaceError = true;       // Use screen-space error instead of distance
    float errorThresholdFull = TreeLODConstants::ERROR_THRESHOLD_FULL;
    float errorThresholdImpostor = TreeLODConstants::ERROR_THRESHOLD_IMPOSTOR;
    float errorThresholdCull = TreeLODConstants::ERROR_THRESHOLD_CULL;

    // Reduced Detail LOD (LOD1) - intermediate between full geometry and impostor
    // When enabled, trees at medium distance use simplified geometry with fewer, larger leaves
    bool enableReducedDetailLOD = false;   // Enable LOD1 (reduced geometry)
    float errorThresholdReduced = TreeLODConstants::ERROR_THRESHOLD_REDUCED;  // Screen error for LOD1
    float reducedDetailDistance = TreeLODConstants::REDUCED_DETAIL_DISTANCE;  // Distance for LOD1 (non-SSE mode)
    float reducedDetailLeafScale = TreeLODConstants::REDUCED_LEAF_SCALE;      // Leaf size multiplier (default 2x)
    float reducedDetailLeafDensity = TreeLODConstants::REDUCED_LEAF_DENSITY;  // Fraction of leaves (default 50%)

    // Impostor settings
    bool enableImpostors = true;
    float impostorBrightness = 1.0f;       // Brightness adjustment for impostors
    float normalStrength = 1.0f;           // How much normals affect lighting
    bool enableFrameBlending = true;       // Blend between 3 nearest frames for smooth transitions

    // Seasonal effects (global for all impostors)
    float autumnHueShift = 0.0f;           // 0 = summer green, 1 = full autumn colors

    // Shadow cascade settings
    // Controls which cascades render full geometry vs impostors only
    struct ShadowSettings {
        // Cascade >= geometryCascadeCutoff uses impostors only (no branches/leaves)
        // Default: cascades 0-2 get geometry, cascade 3 gets impostors only
        uint32_t geometryCascadeCutoff = 3;

        // Cascade >= leafCascadeCutoff skips leaf shadows entirely
        // Default: cascade 3 has no leaf shadows (impostor shadows only)
        uint32_t leafCascadeCutoff = 3;

        // Whether to use cascade-aware shadow LOD
        bool enableCascadeLOD = true;
    } shadow;
};

class TreeImpostorAtlas {
public:
    struct InitInfo {
        const vk::raii::Device* raiiDevice;  // vulkan-hpp RAII device
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
    VkImageView getAlbedoAtlasArrayView() const { return octaAlbedoArrayView_ ? **octaAlbedoArrayView_ : VK_NULL_HANDLE; }
    VkImageView getNormalAtlasArrayView() const { return octaNormalArrayView_ ? **octaNormalArrayView_ : VK_NULL_HANDLE; }
    VkSampler getAtlasSampler() const { return atlasSampler_ ? **atlasSampler_ : VK_NULL_HANDLE; }

    // LOD settings
    TreeLODSettings& getLODSettings() { return lodSettings_; }
    const TreeLODSettings& getLODSettings() const { return lodSettings_; }

    // Get atlas image for UI preview (lazy-initializes ImGui descriptor on first call)
    VkDescriptorSet getPreviewDescriptorSet(uint32_t archetypeIndex);
    VkDescriptorSet getNormalPreviewDescriptorSet(uint32_t archetypeIndex);

private:
    TreeImpostorAtlas() = default;
    bool initInternal(const InitInfo& info);

    bool createRenderPass();
    bool createCapturePipeline();
    bool createAtlasArrayTextures();
    bool createAtlasResources(uint32_t archetypeIndex);
    bool createSampler();

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

    bool createLeafCapturePipeline();
    bool createLeafQuadMesh();

    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;

    // Render pass for capturing impostors (renders to G-buffer)
    std::optional<vk::raii::RenderPass> captureRenderPass_;

    // Pipeline for capturing tree geometry to G-buffer
    std::optional<vk::raii::Pipeline> branchCapturePipeline_;
    std::optional<vk::raii::Pipeline> leafCapturePipeline_;
    std::optional<vk::raii::PipelineLayout> capturePipelineLayout_;
    std::optional<vk::raii::PipelineLayout> leafCapturePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> captureDescriptorSetLayout_;
    std::optional<vk::raii::DescriptorSetLayout> leafCaptureDescriptorSetLayout_;

    // Leaf quad mesh for capture
    VkBuffer leafQuadVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadVertexAllocation_ = VK_NULL_HANDLE;
    VkBuffer leafQuadIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t leafQuadIndexCount_ = 0;

    // Archetype data
    std::vector<TreeImpostorArchetype> archetypes_;

    // Texture array for all archetypes (shared across all archetypes)
    VkImage octaAlbedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaAlbedoArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaAlbedoArrayView_;

    VkImage octaNormalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaNormalArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaNormalArrayView_;

    uint32_t maxArchetypes_ = 16;  // Maximum layers in the array

    // Shared sampler for atlas textures
    std::optional<vk::raii::Sampler> atlasSampler_;

    // Capture descriptor sets (reused)
    std::vector<VkDescriptorSet> captureDescriptorSets_;

    // LOD settings
    TreeLODSettings lodSettings_;

    // Leaf instance buffer for capture (temporary)
    VkBuffer leafCaptureBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafCaptureAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize leafCaptureBufferSize_ = 0;

    // Per-archetype atlas (depth buffers and framebuffers)
    struct AtlasTextures {
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        std::optional<vk::raii::ImageView> albedoView;   // View into octaAlbedoArrayImage_
        std::optional<vk::raii::ImageView> normalView;   // View into octaNormalArrayImage_
        std::optional<vk::raii::ImageView> depthView;
        std::optional<vk::raii::Framebuffer> framebuffer;
        VkDescriptorSet previewDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet normalPreviewDescriptorSet = VK_NULL_HANDLE;
    };
    std::vector<AtlasTextures> atlasTextures_;
};
