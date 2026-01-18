#pragma once

#include <fruit/fruit.h>

/**
 * Fruit DI Annotations for the rendering engine
 *
 * Annotations are used to distinguish between different instances of the same type
 * and to define scopes for dependency injection.
 */
namespace di {

// ============================================================================
// Scope annotations
// ============================================================================

/**
 * Singleton scope - one instance per application lifetime
 * Used for: VulkanContext, TaskScheduler, AssetRegistry
 */
struct SingletonScope {};

/**
 * Per-frame scope - resources that are per-frame indexed
 * Used for: Command buffers, per-frame UBOs
 */
struct PerFrameScope {};

// ============================================================================
// Resource type annotations
// ============================================================================

/**
 * Annotation for Vulkan device handle
 */
struct VulkanDeviceAnnotation {};
using VulkanDeviceRef = fruit::Annotated<VulkanDeviceAnnotation, VkDevice>;

/**
 * Annotation for VMA allocator
 */
struct VmaAllocatorAnnotation {};
using VmaAllocatorRef = fruit::Annotated<VmaAllocatorAnnotation, VmaAllocator>;

/**
 * Annotation for graphics queue
 */
struct GraphicsQueueAnnotation {};
using GraphicsQueueRef = fruit::Annotated<GraphicsQueueAnnotation, VkQueue>;

/**
 * Annotation for command pool
 */
struct CommandPoolAnnotation {};
using CommandPoolRef = fruit::Annotated<CommandPoolAnnotation, VkCommandPool>;

/**
 * Annotation for shader path
 */
struct ShaderPathAnnotation {};
using ShaderPathRef = fruit::Annotated<ShaderPathAnnotation, std::string>;

/**
 * Annotation for resource path
 */
struct ResourcePathAnnotation {};
using ResourcePathRef = fruit::Annotated<ResourcePathAnnotation, std::string>;

/**
 * Annotation for frames in flight count
 */
struct FramesInFlightAnnotation {};
using FramesInFlightRef = fruit::Annotated<FramesInFlightAnnotation, uint32_t>;

/**
 * Annotation for swapchain extent
 */
struct SwapchainExtentAnnotation {};
using SwapchainExtentRef = fruit::Annotated<SwapchainExtentAnnotation, VkExtent2D>;

// ============================================================================
// System annotations (for distinguishing system instances)
// ============================================================================

/**
 * Annotation for rock scatter system
 */
struct RocksSystemAnnotation {};

/**
 * Annotation for detritus scatter system
 */
struct DetritusSystemAnnotation {};

} // namespace di
