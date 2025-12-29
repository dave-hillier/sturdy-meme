#pragma once

#include "InitContext.h"
#include "DescriptorManager.h"
#include "CoreResources.h"

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// Forward declarations to avoid circular dependencies
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
class ShadowSystem;
class MaterialRegistry;
class SkinnedMeshRenderer;
class RendererSystems;
class PostProcessSystem;
struct TerrainConfig;

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
 * RendererInit - Cross-cutting initialization helpers
 *
 * Contains initialization logic that spans multiple unrelated systems.
 *
 * For grouped system creation, use the system's own factory methods:
 * - PostProcessSystem::createWithDependencies()
 * - SnowMaskSystem::createWithDependencies()
 * - GrassSystem::createWithDependencies()
 * - WeatherSystem::createWithDependencies()
 *
 * For InitContext creation, use InitContext::build().
 *
 * Design principles:
 * - Only include methods that touch multiple unrelated systems
 * - Grouped system init belongs in the primary system's factory method
 */
class RendererInit {
public:
    // ========================================================================
    // Complex cross-cutting initialization (touches 3+ unrelated systems)
    // ========================================================================

    /**
     * Initialize atmosphere/fog systems (FroxelSystem, AtmosphereLUTSystem, CloudShadowSystem)
     * Computes initial atmosphere LUTs and connects froxel to post-process.
     * This is cross-cutting because it wires together Froxel, Atmosphere, PostProcess, and Cloud systems.
     */
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkImageView shadowMapView,
        VkSampler shadowMapSampler,
        const std::vector<VkBuffer>& lightBuffers
    );

    // Overload using ShadowResources
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const ShadowResources& shadow,
        const std::vector<VkBuffer>& lightBuffers
    ) {
        return initAtmosphereSubsystems(systems, ctx, shadow.cascadeView, shadow.sampler, lightBuffers);
    }

    /**
     * Initialize all water-related systems.
     * This is cross-cutting because it wires together Water, Terrain, Shadow, and PostProcess.
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

    // ========================================================================
    // Cross-cutting descriptor updates (touch multiple unrelated systems)
    // ========================================================================

    /**
     * Update cloud shadow bindings across all descriptor sets.
     * This is cross-cutting because it touches MaterialRegistry, Rock, Detritus, and SkinnedMesh.
     */
    static void updateCloudShadowBindings(
        VkDevice device,
        MaterialRegistry& materialRegistry,
        const std::vector<VkDescriptorSet>& rockDescriptorSets,
        const std::vector<VkDescriptorSet>& detritusDescriptorSets,
        SkinnedMeshRenderer& skinnedMeshRenderer,
        VkImageView cloudShadowView,
        VkSampler cloudShadowSampler
    );
};
