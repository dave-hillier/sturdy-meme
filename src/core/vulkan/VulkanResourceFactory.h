#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include "VulkanRAII.h"

/**
 * VulkanResourceFactory - Static factory methods for common Vulkan resource creation
 *
 * Centralizes creation of standard Vulkan resources (command pools, sync objects,
 * depth buffers, framebuffers) that follow predictable patterns.
 *
 * Design principles:
 * - All methods are static (no instance state)
 * - Returns structs for multi-resource creation
 * - Consistent error handling via bool return
 */
class VulkanResourceFactory {
public:
    // ========================================================================
    // Resource Structs
    // ========================================================================

    /**
     * Synchronization primitives for frame-in-flight rendering
     */
    struct SyncResources {
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;

        void destroy(VkDevice device);
    };

    /**
     * Depth buffer resources (image, allocation, view, sampler)
     */
    struct DepthResources {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        ManagedSampler sampler;
        VkFormat format = VK_FORMAT_D32_SFLOAT;

        void destroy(VkDevice device, VmaAllocator allocator);
    };

    /**
     * Render pass configuration for standard swapchain presentation
     */
    struct RenderPassConfig {
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkImageLayout finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkImageLayout finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bool clearColor = true;
        bool clearDepth = true;
        bool storeDepth = true;  // Store for Hi-Z pyramid generation
        bool depthOnly = false;  // If true, no color attachment (for shadow maps)
    };

    /**
     * Configuration for depth array image creation (shadow maps, etc.)
     */
    struct DepthArrayConfig {
        VkExtent2D extent;
        VkFormat format = VK_FORMAT_D32_SFLOAT;
        uint32_t arrayLayers = 1;
        bool cubeCompatible = false;  // For point light shadow cubemaps
        bool createSampler = true;    // Create comparison sampler for shadow mapping
    };

    /**
     * Depth array resources (image, allocation, views, sampler)
     */
    struct DepthArrayResources {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView arrayView = VK_NULL_HANDLE;      // View of all layers (for shader sampling)
        std::vector<VkImageView> layerViews;         // Per-layer views (for rendering)
        ManagedSampler sampler;

        void destroy(VkDevice device, VmaAllocator allocator);
    };

    // ========================================================================
    // Command Pool & Buffers
    // ========================================================================

    /**
     * Create a command pool for the specified queue family
     */
    static bool createCommandPool(
        VkDevice device,
        uint32_t queueFamilyIndex,
        VkCommandPoolCreateFlags flags,
        VkCommandPool& outPool);

    /**
     * Allocate primary command buffers from a pool
     */
    static bool createCommandBuffers(
        VkDevice device,
        VkCommandPool pool,
        uint32_t count,
        std::vector<VkCommandBuffer>& outBuffers);

    // ========================================================================
    // Synchronization
    // ========================================================================

    /**
     * Create semaphores and fences for frame synchronization
     */
    static bool createSyncResources(
        VkDevice device,
        uint32_t framesInFlight,
        SyncResources& outResources);

    // ========================================================================
    // Depth Buffer
    // ========================================================================

    /**
     * Create depth buffer with image, view, and sampler
     * Sampler is configured for Hi-Z pyramid generation (nearest filtering)
     */
    static bool createDepthResources(
        VkDevice device,
        VmaAllocator allocator,
        VkExtent2D extent,
        VkFormat format,
        DepthResources& outResources);

    /**
     * Create depth image and view only (no sampler) - for resize operations
     * where sampler is preserved
     */
    static bool createDepthImageAndView(
        VkDevice device,
        VmaAllocator allocator,
        VkExtent2D extent,
        VkFormat format,
        VkImage& outImage,
        VmaAllocation& outAllocation,
        VkImageView& outView);

    // ========================================================================
    // Framebuffers
    // ========================================================================

    /**
     * Create framebuffers for each swapchain image view
     */
    static bool createFramebuffers(
        VkDevice device,
        VkRenderPass renderPass,
        const std::vector<VkImageView>& swapchainImageViews,
        VkImageView depthImageView,
        VkExtent2D extent,
        std::vector<VkFramebuffer>& outFramebuffers);

    /**
     * Destroy framebuffers
     */
    static void destroyFramebuffers(
        VkDevice device,
        std::vector<VkFramebuffer>& framebuffers);

    // ========================================================================
    // Render Pass
    // ========================================================================

    /**
     * Create a standard render pass for swapchain presentation with depth
     * If config.depthOnly is true, creates a depth-only render pass (for shadow maps)
     */
    static bool createRenderPass(
        VkDevice device,
        const RenderPassConfig& config,
        VkRenderPass& outRenderPass);

    // ========================================================================
    // Depth Array Resources (for shadow maps)
    // ========================================================================

    /**
     * Create depth array image with array view and per-layer views
     * Suitable for CSM shadow cascades, point light cubemaps, spot light arrays
     */
    static bool createDepthArrayResources(
        VkDevice device,
        VmaAllocator allocator,
        const DepthArrayConfig& config,
        DepthArrayResources& outResources);

    /**
     * Create framebuffers for depth-only rendering (shadow maps)
     */
    static bool createDepthOnlyFramebuffers(
        VkDevice device,
        VkRenderPass renderPass,
        const std::vector<VkImageView>& depthImageViews,
        VkExtent2D extent,
        std::vector<VkFramebuffer>& outFramebuffers);

    // ========================================================================
    // Buffer Factories
    // ========================================================================

    /**
     * Create a staging buffer (host-visible, for CPU->GPU transfers)
     */
    static bool createStagingBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a vertex buffer (device-local)
     */
    static bool createVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create an index buffer (device-local)
     */
    static bool createIndexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a uniform buffer (host-visible, mapped for CPU updates)
     */
    static bool createUniformBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a storage buffer (device-local, GPU-only)
     */
    static bool createStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a storage buffer with host read access (for GPU->CPU readback)
     */
    static bool createStorageBufferHostReadable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a storage buffer with host write access (for CPU->GPU uploads)
     */
    static bool createStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a readback buffer (host-visible, for GPU->CPU transfers)
     */
    static bool createReadbackBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a vertex+storage buffer (device-local, for compute-generated vertices)
     */
    static bool createVertexStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a vertex+storage buffer with host write access (for CPU->GPU uploads with compute access)
     */
    static bool createVertexStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create an index buffer with host write access (for CPU->GPU uploads)
     */
    static bool createIndexBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create an indirect draw/dispatch buffer (device-local)
     */
    static bool createIndirectBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    /**
     * Create a dynamic vertex buffer with host write access (for per-frame CPU updates)
     */
    static bool createDynamicVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer);

    // ========================================================================
    // Sampler Factories
    // ========================================================================

    /**
     * Create nearest-filtering sampler with clamp-to-edge (depth/integer textures)
     */
    static bool createSamplerNearestClamp(VkDevice device, ManagedSampler& outSampler);

    /**
     * Create linear-filtering sampler with clamp-to-edge
     */
    static bool createSamplerLinearClamp(VkDevice device, ManagedSampler& outSampler);

    /**
     * Create linear-filtering sampler with repeat (standard textures)
     */
    static bool createSamplerLinearRepeat(VkDevice device, ManagedSampler& outSampler);

    /**
     * Create linear-filtering sampler with repeat and anisotropy (terrain/high-quality textures)
     */
    static bool createSamplerLinearRepeatAnisotropic(VkDevice device, float maxAnisotropy, ManagedSampler& outSampler);

    /**
     * Create shadow map comparison sampler
     */
    static bool createSamplerShadowComparison(VkDevice device, ManagedSampler& outSampler);
};
