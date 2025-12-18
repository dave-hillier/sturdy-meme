#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

#include "TreeGenerator.h"
#include "Mesh.h"
#include "Texture.h"
#include "UBOs.h"
#include "DescriptorManager.h"
#include "core/VulkanRAII.h"
#include <memory>

// Capture angle definition
struct CaptureAngle {
    float azimuth;      // Horizontal angle in degrees (0 = front, 90 = right, etc.)
    float elevation;    // Vertical angle in degrees (0 = side, 45 = angled, 90 = top)
    std::string name;   // Debug name for this angle
};

// Result of billboard generation
struct BillboardAtlas {
    std::vector<uint8_t> rgbaPixels;  // RGBA8 pixel data
    uint32_t width;                    // Atlas width in pixels
    uint32_t height;                   // Atlas height in pixels
    uint32_t cellWidth;                // Individual capture cell width
    uint32_t cellHeight;               // Individual capture cell height
    uint32_t columns;                  // Number of columns in atlas
    uint32_t rows;                     // Number of rows in atlas
    std::vector<CaptureAngle> angles;  // Angles for each cell (row-major order)
};

class BillboardCapture {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    /**
     * Factory: Create and initialize BillboardCapture.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<BillboardCapture> create(const InitInfo& info);

    ~BillboardCapture();

    // Non-copyable, non-movable
    BillboardCapture(const BillboardCapture&) = delete;
    BillboardCapture& operator=(const BillboardCapture&) = delete;
    BillboardCapture(BillboardCapture&&) = delete;
    BillboardCapture& operator=(BillboardCapture&&) = delete;

    // Generate billboard atlas from tree meshes
    // Returns true on success, fills out the atlas struct
    bool generateAtlas(
        const Mesh& branchMesh,
        const Mesh& leafMesh,
        const TreeParameters& treeParams,
        const Texture& barkColorTex,
        const Texture& barkNormalTex,
        const Texture& barkAOTex,
        const Texture& barkRoughnessTex,
        const Texture& leafTex,
        uint32_t captureResolution,   // Resolution per capture (e.g., 512)
        BillboardAtlas& outAtlas
    );

    // Save atlas to PNG file
    static bool saveAtlasToPNG(const BillboardAtlas& atlas, const std::string& filepath);

    // Get the standard 17 capture angles
    static std::vector<CaptureAngle> getStandardAngles();

private:
    BillboardCapture() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    // Create offscreen render target
    bool createRenderTarget(uint32_t width, uint32_t height);
    void destroyRenderTarget();

    // Create render pass for offscreen rendering
    bool createRenderPass();

    // Create pipeline for billboard capture (no fog)
    bool createPipeline();

    // Create descriptor set layout and sets
    bool createDescriptorSetLayout();
    bool createDescriptorSets();

    // Create UBO buffer
    bool createUniformBuffer();

    // Calculate orthographic projection that fits the tree
    glm::mat4 calculateOrthoProjection(const Mesh& branchMesh, const Mesh& leafMesh, float padding = 0.1f);

    // Calculate view matrix for a capture angle
    glm::mat4 calculateViewMatrix(const CaptureAngle& angle, const glm::vec3& center, float distance);

    // Calculate bounding sphere of meshes
    void calculateBoundingSphere(const Mesh& branchMesh, const Mesh& leafMesh,
                                  glm::vec3& outCenter, float& outRadius);

    // Render tree to offscreen target
    bool renderCapture(
        const Mesh& branchMesh,
        const Mesh& leafMesh,
        const TreeParameters& treeParams,
        const glm::mat4& view,
        const glm::mat4& proj,
        const Texture& barkColorTex,
        const Texture& barkNormalTex,
        const Texture& barkAOTex,
        const Texture& barkRoughnessTex,
        const Texture& leafTex
    );

    // Read pixels from render target
    bool readPixels(std::vector<uint8_t>& outPixels);

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::string shaderPath;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Offscreen render target
    VkImage colorImage = VK_NULL_HANDLE;
    VmaAllocation colorAllocation = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    ManagedRenderPass renderPass_;
    ManagedFramebuffer framebuffer_;
    ManagedSampler sampler_;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    // Staging buffer for readback (persistent)
    ManagedBuffer stagingBuffer_;

    // Pipeline
    ManagedDescriptorSetLayout descriptorSetLayout_;
    ManagedPipelineLayout pipelineLayout_;
    ManagedPipeline solidPipeline_;
    ManagedPipeline leafPipeline_;

    // Descriptor sets
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // UBO (persistent, mapped)
    ManagedBuffer uboBuffer_;
    void* uboMapped = nullptr;

    static constexpr VkFormat COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
};
