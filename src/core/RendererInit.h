#pragma once

#include "InitContext.h"
#include "VulkanContext.h"
#include "DescriptorManager.h"
#include "CoreResources.h"

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// Forward declarations to avoid circular dependencies
class PostProcessSystem;
class BloomSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class GrassSystem;
class WindSystem;
class WeatherSystem;
class LeafSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class CloudShadowSystem;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class TerrainSystem;
class RockSystem;
class CatmullClarkSystem;
class HiZSystem;
class TreeEditSystem;
class DebugLineSystem;
class ShadowSystem;
class MaterialRegistry;
class SkinnedMeshRenderer;
class RendererSystems;
struct TerrainConfig;
struct EnvironmentSettings;

/**
 * WaterSubsystems - Groups all water-related systems for easier initialization
 */
struct WaterSubsystems {
    WaterSystem& system;
    WaterDisplacement& displacement;
    FlowMapGenerator& flowMapGenerator;
    FoamBuffer& foamBuffer;
    RendererSystems& rendererSystems;  // For SSR factory creation
    WaterTileCull& tileCull;
    WaterGBuffer& gBuffer;
};

/**
 * RendererInit - Helper class for building InitContext and managing subsystem initialization
 *
 * This centralizes the creation of InitContext and provides utilities for
 * initializing subsystems with consistent resource wiring.
 *
 * Design principles:
 * - Static methods take specific system references rather than the whole Renderer
 * - InitContext provides common Vulkan resources
 * - Additional parameters for specific requirements (render passes, etc.)
 */
class RendererInit {
public:
    /**
     * Build an InitContext from VulkanContext and common resources.
     * This is the single source of truth for creating the shared init context.
     */
    static InitContext buildContext(
        const VulkanContext& vulkanContext,
        VkCommandPool commandPool,
        DescriptorManager::Pool* descriptorPool,
        const std::string& resourcePath,
        uint32_t framesInFlight
    ) {
        InitContext ctx{};
        ctx.device = vulkanContext.getDevice();
        ctx.physicalDevice = vulkanContext.getPhysicalDevice();
        ctx.allocator = vulkanContext.getAllocator();
        ctx.graphicsQueue = vulkanContext.getGraphicsQueue();
        ctx.commandPool = commandPool;
        ctx.descriptorPool = descriptorPool;
        ctx.shaderPath = resourcePath + "/shaders";
        ctx.resourcePath = resourcePath;
        ctx.framesInFlight = framesInFlight;
        ctx.extent = vulkanContext.getSwapchainExtent();
        return ctx;
    }

    /**
     * Update extent in an existing InitContext (e.g., after resize)
     */
    static void updateExtent(InitContext& ctx, VkExtent2D newExtent) {
        ctx.extent = newExtent;
    }

    /**
     * Create a modified InitContext with different extent (for systems that need different resolution)
     */
    static InitContext withExtent(const InitContext& ctx, VkExtent2D newExtent) {
        InitContext modified = ctx;
        modified.extent = newExtent;
        return modified;
    }

    /**
     * Create a modified InitContext with different shader path (rare, for testing)
     */
    static InitContext withShaderPath(const InitContext& ctx, const std::string& shaderPath) {
        InitContext modified = ctx;
        modified.shaderPath = shaderPath;
        return modified;
    }

    // ========================================================================
    // Subsystem initialization methods
    // ========================================================================

    /**
     * Initialize post-processing systems (PostProcessSystem, BloomSystem)
     * Uses factory pattern to create PostProcessSystem.
     * Should be called early to get HDR render pass for other systems
     */
    static bool initPostProcessing(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass finalRenderPass,
        VkFormat swapchainImageFormat
    );

    /**
     * Initialize snow subsystems (SnowMaskSystem, VolumetricSnowSystem)
     * Creates both systems via factory and stores them in RendererSystems
     */
    static bool initSnowSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    // Overload using CoreResources
    static bool initSnowSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr
    ) {
        return initSnowSubsystems(systems, ctx, hdr.renderPass);
    }

    /**
     * Initialize grass and wind systems (GrassSystem, WindSystem)
     * Also connects environment settings to grass and leaf systems
     * Creates WindSystem via factory and stores it in RendererSystems
     */
    static bool initGrassSubsystem(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass,
        VkRenderPass shadowRenderPass,
        uint32_t shadowMapSize
    );

    // Overload using CoreResources
    static bool initGrassSubsystem(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr,
        const ShadowResources& shadow
    ) {
        return initGrassSubsystem(systems, ctx,
                                   hdr.renderPass, shadow.renderPass, shadow.mapSize);
    }

    /**
     * Initialize weather-related systems (WeatherSystem, LeafSystem)
     * Creates WeatherSystem via factory and stores it in RendererSystems
     */
    static bool initWeatherSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    // Overload using CoreResources
    static bool initWeatherSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr
    ) {
        return initWeatherSubsystems(systems, ctx, hdr.renderPass);
    }

    /**
     * Initialize atmosphere/fog systems (FroxelSystem, AtmosphereLUTSystem, CloudShadowSystem)
     * Computes initial atmosphere LUTs and connects froxel to post-process
     * Creates FroxelSystem via factory and stores it in RendererSystems
     */
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkImageView shadowMapView,
        VkSampler shadowMapSampler,
        const std::vector<VkBuffer>& lightBuffers
    );

    // Overload using CoreResources
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const ShadowResources& shadow,
        const std::vector<VkBuffer>& lightBuffers
    ) {
        return initAtmosphereSubsystems(systems, ctx, shadow.cascadeView, shadow.sampler, lightBuffers);
    }

    /**
     * Initialize all water-related systems
     */
    static bool initWaterSubsystems(
        WaterSubsystems& water,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass,
        const ShadowSystem& shadowSystem,
        const TerrainSystem& terrainSystem,
        const TerrainConfig& terrainConfig,
        const PostProcessSystem& postProcessSystem,
        VkSampler depthSampler
    );

    /**
     * Create water descriptor sets after all water systems are initialized
     */
    static bool createWaterDescriptorSets(
        WaterSubsystems& water,
        const std::vector<VkBuffer>& uniformBuffers,
        size_t uniformBufferSize,
        ShadowSystem& shadowSystem,
        const TerrainSystem& terrainSystem,
        const PostProcessSystem& postProcessSystem,
        VkSampler depthSampler
    );

    /**
     * Initialize Hi-Z occlusion culling system via factory
     * Returns true even if Hi-Z fails (it's optional)
     */
    static bool initHiZSystem(
        RendererSystems& systems,
        const InitContext& ctx,
        VkFormat depthFormat,
        VkImageView hdrDepthView,
        VkSampler depthSampler
    );

    // Overload using HDRResources (still needs depthFormat and depthSampler)
    static bool initHiZSystem(
        RendererSystems& systems,
        const InitContext& ctx,
        VkFormat depthFormat,
        const HDRResources& hdr,
        VkSampler depthSampler
    ) {
        return initHiZSystem(systems, ctx, depthFormat, hdr.depthView, depthSampler);
    }

    /**
     * Create debug line system for physics visualization (factory pattern)
     */
    static std::unique_ptr<DebugLineSystem> createDebugLineSystem(
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    // Overload using HDRResources
    static std::unique_ptr<DebugLineSystem> createDebugLineSystem(
        const InitContext& ctx,
        const HDRResources& hdr
    ) {
        return createDebugLineSystem(ctx, hdr.renderPass);
    }

    /**
     * Update cloud shadow bindings across all descriptor sets
     * Called after CloudShadowSystem is initialized
     */
    static void updateCloudShadowBindings(
        VkDevice device,
        MaterialRegistry& materialRegistry,
        const std::vector<VkDescriptorSet>& rockDescriptorSets,
        SkinnedMeshRenderer& skinnedMeshRenderer,
        VkImageView cloudShadowView,
        VkSampler cloudShadowSampler
    );
};
