#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "InitContext.h"
#include "DescriptorManager.h"
#include "ImageBuilder.h"
#include "VmaImage.h"
#include "VmaBuffer.h"
#include "PerFrameBuffer.h"

class Mesh;
class Texture;
class MaterialRegistry;
struct Renderable;
struct ClusteredMesh;

// Packed vertex format for SSBO (matches PackedVertex in visbuf_resolve.comp)
struct VisBufPackedVertex {
    glm::vec4 positionAndU;  // xyz = position, w = texCoord.x
    glm::vec4 normalAndV;    // xyz = normal,   w = texCoord.y
    glm::vec4 tangent;       // xyzw = tangent (w = handedness)
    glm::vec4 color;         // vertex color
};

// Per-mesh tracking info for V-buffer global buffers
struct VisBufMeshInfo {
    uint32_t globalVertexOffset;
    uint32_t globalIndexOffset;
    uint32_t triangleOffset;  // = globalIndexOffset / 3
};

// GPU material data for the resolve shader (matches GPUMaterial in visbuf_resolve.comp)
struct GPUMaterial {
    glm::vec4 baseColor;            // RGB + alpha
    float roughness;
    float metallic;
    float normalScale;
    float aoStrength;
    uint32_t albedoTexIndex;        // UINT32_MAX = no texture
    uint32_t normalTexIndex;        // UINT32_MAX = no texture
    uint32_t roughnessMetallicTexIndex; // UINT32_MAX = no texture
    uint32_t flags;                 // reserved
};

// Resolve uniforms for the compute material resolve pass
struct alignas(16) VisBufResolveUniforms {
    glm::mat4 invViewProj;
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::vec4 cameraPosition;
    glm::vec4 screenParams;    // width, height, 1/width, 1/height
    glm::vec4 lightDirection;  // xyz = sun dir, w = intensity
    uint32_t instanceCount;
    uint32_t materialCount;
    uint32_t _pad1, _pad2;
};

// Push constants for the V-buffer rasterization pass
struct VisBufPushConstants {
    glm::mat4 model;
    uint32_t instanceId;
    uint32_t triangleOffset;
    float alphaTestThreshold;
    float _pad;
};

// Per-draw metadata for cluster indirect draws (matches GLSL DrawClusterInfo)
struct DrawClusterInfo {
    uint32_t instanceId;
    uint32_t triangleOffset;  // = cluster.firstIndex / 3
};

// Push constants for debug visualization
struct VisBufDebugPushConstants {
    uint32_t mode;  // 0=instance, 1=triangle, 2=mixed, 3=cluster, 4=cluster+instance, 5=depth
    float _pad0, _pad1, _pad2;
};

/**
 * VisibilityBuffer - GPU-driven visibility buffer rendering system
 *
 * Implements a two-phase rendering approach:
 * Phase 1 (rasterize): Render scene objects writing (instanceID, triangleID) to a uint target
 * Phase 2 (resolve):   Compute shader reads V-buffer, reconstructs attributes, evaluates materials
 *
 * The V-buffer target is a R32G32_UINT image (64-bit): R = instanceId, G = triangleId.
 * Full 32-bit range for both IDs — no bit-packing limits.
 *
 * Usage:
 *   1. create() - Initialize once at startup
 *   2. resize() - Handle window resize
 *   3. beginFrame() - Clear V-buffer
 *   4. recordRasterPass() - Record draw commands writing to V-buffer
 *   5. recordResolvePass() - Compute shader material evaluation
 *   6. getDebugPipeline() - Optional debug visualization
 */
class VisibilityBuffer {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit VisibilityBuffer(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkFormat depthFormat;
        const vk::raii::Device* raiiDevice = nullptr;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
    };

    static std::unique_ptr<VisibilityBuffer> create(const InitInfo& info);
    static std::unique_ptr<VisibilityBuffer> create(const InitContext& ctx, VkFormat depthFormat);

    ~VisibilityBuffer();

    // Non-copyable, non-movable
    VisibilityBuffer(const VisibilityBuffer&) = delete;
    VisibilityBuffer& operator=(const VisibilityBuffer&) = delete;
    VisibilityBuffer(VisibilityBuffer&&) = delete;
    VisibilityBuffer& operator=(VisibilityBuffer&&) = delete;

    void resize(VkExtent2D newExtent);

    // Get the V-buffer render pass (color=R32G32_UINT + depth)
    VkRenderPass getRenderPass() const { return renderPass_; }
    VkFramebuffer getFramebuffer() const { return framebuffer_; }
    VkExtent2D getExtent() const { return extent_; }

    // Get the V-buffer image view for reading (debug vis, resolve)
    VkImageView getVisibilityView() const { return visibilityView_; }
    VkImage getVisibilityImage() const { return visibilityImage_.get(); }

    // Rasterization pipeline for writing to V-buffer
    VkPipeline getRasterPipeline() const { return rasterPipeline_; }
    VkPipelineLayout getRasterPipelineLayout() const { return rasterPipelineLayout_; }

    // Debug visualization
    VkPipeline getDebugPipeline() const { return debugPipeline_; }
    VkPipelineLayout getDebugPipelineLayout() const { return debugPipelineLayout_; }

    // Resolve compute pipeline
    VkPipeline getResolvePipeline() const;
    VkPipelineLayout getResolvePipelineLayout() const;

    // Record transition of V-buffer to shader read after rasterization
    void transitionToShaderRead(VkCommandBuffer cmd);

    // Record transition of V-buffer to color attachment for rasterization
    void transitionToColorAttachment(VkCommandBuffer cmd);

    // Record clear of the V-buffer (writes 0 to indicate no geometry)
    void recordClear(VkCommandBuffer cmd);

    // External buffer references for resolve pass
    struct ResolveBuffers {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VkBuffer materialBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferSize = 0;
        VkDeviceSize indexBufferSize = 0;
        VkDeviceSize instanceBufferSize = 0;
        VkDeviceSize materialBufferSize = 0;
        uint32_t materialCount = 0;
        VkImageView textureArrayView = VK_NULL_HANDLE; // sampler2DArray
        VkSampler textureArraySampler = VK_NULL_HANDLE;
        VkImageView hdrDepthView = VK_NULL_HANDLE;    // HDR pass depth for depth comparison
        VkImage hdrDepthImage = VK_NULL_HANDLE;       // HDR depth image (for layout barriers)
        VkBuffer lightBuffer = VK_NULL_HANDLE;        // Dynamic light SSBO
        VkDeviceSize lightBufferSize = 0;
    };

    /**
     * Set external buffer references for the resolve pass.
     * Must be called before recordResolvePass(). Re-creates resolve
     * descriptor sets whenever buffers change.
     */
    void setResolveBuffers(const ResolveBuffers& buffers);

    // Update resolve uniforms
    void updateResolveUniforms(uint32_t frameIndex,
                                const glm::mat4& view, const glm::mat4& proj,
                                const glm::vec3& cameraPos,
                                const glm::vec3& sunDir, float sunIntensity,
                                uint32_t materialCount = 0);

    // Record the compute resolve pass (dispatches visbuf_resolve.comp)
    void recordResolvePass(VkCommandBuffer cmd, uint32_t frameIndex,
                           VkImageView hdrOutputView);

    // Bind the debug visualization descriptor set and record fullscreen draw
    void recordDebugVisualization(VkCommandBuffer cmd, uint32_t debugMode);

    // ====================================================================
    // Global vertex/index buffer management (for resolve pass)
    // ====================================================================

    /**
     * Build global vertex and index buffers from unique scene meshes.
     * Repacks Vertex data into PackedVertex format for the resolve shader.
     * Adjusts indices to reference global vertex offsets.
     * Call once when the scene is ready, or when meshes change.
     */
    bool buildGlobalBuffers(const std::vector<const Mesh*>& uniqueMeshes);

    /**
     * Build global vertex/index buffers from clustered mesh data.
     * Uses cluster vertex/index ordering so resolve triangleIds match cluster raster output.
     * meshId maps each ClusteredMesh back to its source Mesh pointer.
     */
    bool buildGlobalBuffersFromClusters(
        const std::vector<std::pair<const Mesh*, const ClusteredMesh*>>& meshClusters);

    /** Get mesh info (offsets) for a given mesh pointer. Returns nullptr if not found. */
    const VisBufMeshInfo* getMeshInfo(const Mesh* mesh) const;

    bool hasGlobalBuffers() const { return globalBuffersBuilt_; }
    VkBuffer getGlobalVertexBuffer() const { return globalVertexBuffer_.get(); }
    VkBuffer getGlobalIndexBuffer() const { return globalIndexBuffer_.get(); }
    VkDeviceSize getGlobalVertexBufferSize() const { return globalVertexBufferSize_; }
    VkDeviceSize getGlobalIndexBufferSize() const { return globalIndexBufferSize_; }

    // ====================================================================
    // Raster pass descriptor set management
    // ====================================================================

    /**
     * Create per-frame descriptor sets for the raster pass.
     * Binds UBO (binding 0) and placeholder diffuse texture (binding 1).
     * Call once after GlobalBufferManager is initialized.
     */
    bool createRasterDescriptorSets(const std::vector<VkBuffer>& uboBuffers,
                                     VkDeviceSize uboSize);

    VkDescriptorSet getRasterDescriptorSet(uint32_t frameIndex) const;
    bool hasRasterDescriptorSets() const { return !rasterDescSets_.empty(); }

    // ====================================================================
    // Material texture array (albedo textures packed into sampler2DArray)
    // ====================================================================

    /**
     * Build a 2D array texture from all material albedo textures.
     * Uses one-shot command buffer with blit for resizing.
     * Returns the texture-to-layer mapping for populating GPUMaterial indices.
     */
    bool buildMaterialTextureArray(const MaterialRegistry& registry);
    bool hasTextureArray() const { return textureArrayBuilt_; }
    VkImageView getTextureArrayView() const { return textureArrayView_; }
    VkSampler getTextureArraySampler() const;
    uint32_t getTextureLayerIndex(const Texture* tex) const;

    // ====================================================================
    // Cluster raster pipeline (indirect draws via SSBOs + gl_DrawID)
    // ====================================================================

    bool createClusterRasterPipeline();
    VkPipeline getClusterRasterPipeline() const { return clusterRasterPipeline_; }
    VkPipelineLayout getClusterRasterPipelineLayout() const { return clusterRasterPipelineLayout_; }
    bool hasClusterRasterPipeline() const { return clusterRasterPipeline_ != VK_NULL_HANDLE; }

    /**
     * Create cluster raster descriptor sets.
     * Binds: UBO, instance SSBO, draw info SSBO.
     */
    bool createClusterRasterDescriptorSets(
        const std::vector<VkBuffer>& uboBuffers, VkDeviceSize uboSize,
        const std::vector<VkBuffer>& instanceBuffers, VkDeviceSize instanceBufferSize,
        VkBuffer drawInfoBuffer, VkDeviceSize drawInfoBufferSize);

    VkDescriptorSet getClusterRasterDescriptorSet(uint32_t frameIndex) const;
    bool hasClusterRasterDescriptorSets() const { return !clusterRasterDescSets_.empty(); }

    // Get the depth image/view (shared with main HDR pass when V-buffer is active)
    VkImageView getDepthView() const { return depthView_; }
    VkImage getDepthImage() const { return depthImage_.get(); }

    // Statistics
    struct Stats {
        uint32_t rasterizedObjects;
        uint32_t resolvedPixels;
    };
    Stats getStats() const { return stats_; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createRenderTargets();
    void destroyRenderTargets();

    bool createRenderPass();
    void destroyRenderPass();

    bool createFramebuffer();
    void destroyFramebuffer();

    bool createRasterPipeline();
    void destroyRasterPipeline();

    bool createDebugPipeline();
    void destroyDebugPipeline();

    bool createResolvePipeline();
    void destroyResolvePipeline();

    bool createDescriptorSets();
    void destroyDescriptorSets();

    bool createResolveBuffers();
    void destroyResolveBuffers();

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    VkExtent2D extent_ = {0, 0};
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // V-buffer render target (R32G32_UINT — 64-bit: R=instanceId, G=triangleId)
    static constexpr VkFormat VISBUF_FORMAT = VK_FORMAT_R32G32_UINT;
    ManagedImage visibilityImage_;
    VkImageView visibilityView_ = VK_NULL_HANDLE;

    // Depth target (shared or owned)
    ManagedImage depthImage_;
    VkImageView depthView_ = VK_NULL_HANDLE;

    // Render pass + framebuffer
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // Rasterization pipeline (writes instance+triangle IDs)
    std::optional<vk::raii::DescriptorSetLayout> rasterDescSetLayout_;
    VkPipelineLayout rasterPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline rasterPipeline_ = VK_NULL_HANDLE;

    // Cluster rasterization pipeline (indirect draws, SSBO-based transforms)
    std::optional<vk::raii::DescriptorSetLayout> clusterRasterDescSetLayout_;
    VkPipelineLayout clusterRasterPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline clusterRasterPipeline_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> clusterRasterDescSets_;

    // Debug visualization pipeline (fullscreen quad)
    std::optional<vk::raii::DescriptorSetLayout> debugDescSetLayout_;
    VkPipelineLayout debugPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline debugPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet debugDescSet_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> nearestSampler_;

    // Resolve compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> resolveDescSetLayout_;
    VkPipelineLayout resolvePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline resolvePipeline_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> resolveDescSets_;

    // Resolve uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet resolveUniformBuffers_;

    // External buffer references for resolve
    ResolveBuffers resolveBuffers_;
    std::optional<vk::raii::Sampler> depthSampler_;
    std::optional<vk::raii::Sampler> textureSampler_;
    bool resolveDescSetsDirty_ = true;

    // Placeholder buffer for unbound SSBO descriptors (vertex/index/material when not yet wired)
    VmaBuffer placeholderBuffer_;
    static constexpr VkDeviceSize PLACEHOLDER_BUFFER_SIZE = 256;

    // Placeholder 1x1 image for unbound texture array descriptor
    ManagedImage placeholderTexImage_;
    VkImageView placeholderTexView_ = VK_NULL_HANDLE;

    // Global vertex/index buffers for resolve pass
    VmaBuffer globalVertexBuffer_;
    VmaBuffer globalIndexBuffer_;
    VkDeviceSize globalVertexBufferSize_ = 0;
    VkDeviceSize globalIndexBufferSize_ = 0;
    std::unordered_map<const Mesh*, VisBufMeshInfo> meshInfoMap_;
    bool globalBuffersBuilt_ = false;

    // Raster pass descriptor sets (per-frame: UBO + placeholder texture)
    std::vector<VkDescriptorSet> rasterDescSets_;

    // Material texture array (albedo textures as sampler2DArray)
    ManagedImage textureArrayImage_;
    VkImageView textureArrayView_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> textureArraySampler_;
    bool textureArrayBuilt_ = false;
    std::unordered_map<const Texture*, uint32_t> textureLayerMap_;

    // Queue/command pool for one-shot operations (texture array building)
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    Stats stats_ = {};
};
