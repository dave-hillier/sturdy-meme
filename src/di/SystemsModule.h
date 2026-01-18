#pragma once

#include <fruit/fruit.h>
#include <memory>
#include <functional>

// Forward declarations for system groups
struct AtmosphereSystemGroup;
struct VegetationSystemGroup;
struct WaterSystemGroup;
struct SnowSystemGroup;
struct GeometrySystemGroup;

// Forward declarations for systems
class PostProcessSystem;
class BloomSystem;
class BilateralGridSystem;
class ShadowSystem;
class TerrainSystem;
class SceneManager;
class GlobalBufferManager;
class HiZSystem;
class SkinnedMeshRenderer;
class Profiler;
class DebugLineSystem;
class UBOBuilder;
class TimeSystem;
class CelestialCalculator;
struct EnvironmentSettings;

// Forward declarations for infrastructure
class VulkanContext;
class DescriptorInfrastructure;
struct InitContext;

namespace di {

/**
 * Configuration for systems module
 */
struct SystemsConfig {
    std::string resourcePath;
    bool enableTerrain = true;
    bool enableWater = true;
    bool enableVegetation = true;

    // Terrain configuration overrides
    uint32_t terrainMaxDepth = 20;
    float terrainSize = 16384.0f;
};

/**
 * PostProcessBundle - Groups core post-processing systems
 *
 * Contains:
 * - PostProcessSystem (HDR, tonemapping)
 * - BloomSystem (bloom effects)
 * - BilateralGridSystem (bilateral filtering)
 */
struct PostProcessBundle {
    std::unique_ptr<PostProcessSystem> postProcess;
    std::unique_ptr<BloomSystem> bloom;
    std::unique_ptr<BilateralGridSystem> bilateralGrid;

    // Move constructor for Fruit compatibility
    PostProcessBundle() = default;
    PostProcessBundle(PostProcessBundle&&) = default;
    PostProcessBundle& operator=(PostProcessBundle&&) = default;
};

/**
 * CoreSystemsBundle - Groups Tier-1 core systems
 *
 * Contains:
 * - PostProcessBundle
 * - ShadowSystem
 * - TerrainSystem
 * - HiZSystem
 */
struct CoreSystemsBundle {
    PostProcessBundle postProcess;
    std::unique_ptr<ShadowSystem> shadow;
    std::unique_ptr<TerrainSystem> terrain;
    std::unique_ptr<HiZSystem> hiZ;

    CoreSystemsBundle() = default;
    CoreSystemsBundle(CoreSystemsBundle&&) = default;
    CoreSystemsBundle& operator=(CoreSystemsBundle&&) = default;
};

/**
 * InfrastructureBundle - Groups infrastructure systems
 *
 * Contains:
 * - SceneManager
 * - GlobalBufferManager
 * - SkinnedMeshRenderer
 * - Profiler
 * - DebugLineSystem
 * - UBOBuilder
 * - TimeSystem
 * - CelestialCalculator
 * - EnvironmentSettings
 */
struct InfrastructureBundle {
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<GlobalBufferManager> globalBuffers;
    std::unique_ptr<SkinnedMeshRenderer> skinnedMesh;
    std::unique_ptr<Profiler> profiler;
    std::unique_ptr<DebugLineSystem> debugLine;
    std::unique_ptr<UBOBuilder> uboBuilder;
    std::unique_ptr<TimeSystem> time;
    std::unique_ptr<CelestialCalculator> celestial;
    std::unique_ptr<EnvironmentSettings> environmentSettings;

    InfrastructureBundle() = default;
    InfrastructureBundle(InfrastructureBundle&&) = default;
    InfrastructureBundle& operator=(InfrastructureBundle&&) = default;
};

/**
 * SystemsModule - Factory for creating system bundles via DI
 *
 * This module provides the bridge between Fruit DI and the existing
 * Bundle/CreateDeps patterns. It creates systems in the correct order
 * and handles cross-system wiring.
 */
class SystemsModule {
public:
    /**
     * Create a PostProcessBundle using the existing createWithDependencies pattern.
     */
    static PostProcessBundle createPostProcessBundle(
        const InitContext& initCtx,
        VkRenderPass swapchainRenderPass,
        VkFormat swapchainImageFormat
    );

    /**
     * Create a CoreSystemsBundle with all Tier-1 systems.
     */
    static CoreSystemsBundle createCoreSystems(
        const InitContext& initCtx,
        VulkanContext& vulkanContext,
        DescriptorInfrastructure& descriptorInfra,
        const SystemsConfig& config
    );

    /**
     * Create an InfrastructureBundle with scene and support systems.
     */
    static InfrastructureBundle createInfrastructure(
        const InitContext& initCtx,
        VulkanContext& vulkanContext,
        DescriptorInfrastructure& descriptorInfra,
        const SystemsConfig& config
    );

    /**
     * Get the Fruit component for systems module.
     */
    static fruit::Component<
        fruit::Required<VulkanContext, InitContext, DescriptorInfrastructure, SystemsConfig>,
        PostProcessBundle,
        CoreSystemsBundle,
        InfrastructureBundle
    > getComponent();
};

/**
 * Provider for SystemsConfig
 */
fruit::Component<SystemsConfig> getSystemsConfigComponent(SystemsConfig config);

} // namespace di
