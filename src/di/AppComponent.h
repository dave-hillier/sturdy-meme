#pragma once

#include <fruit/fruit.h>
#include <SDL3/SDL.h>
#include <string>
#include <memory>

#include "CoreModule.h"
#include "InfrastructureModule.h"
#include "SystemsModule.h"

// Forward declarations
class VulkanContext;
class RenderingInfrastructure;
class DescriptorInfrastructure;
class RendererSystems;
struct InitContext;

namespace di {

/**
 * AppConfig - Top-level application configuration
 *
 * Combines all module configurations into a single struct
 * that can be used to configure the entire DI container.
 */
struct AppConfig {
    // Window configuration
    SDL_Window* window = nullptr;

    // Paths
    std::string resourcePath;

    // Frame configuration
    uint32_t framesInFlight = 3;

    // Threading
    uint32_t threadCount = 0;  // 0 = auto-detect

    // Feature toggles
    bool enableTerrain = true;
    bool enableWater = true;
    bool enableVegetation = true;

    // Terrain settings
    uint32_t terrainMaxDepth = 20;
    float terrainSize = 16384.0f;

    /**
     * Build CoreConfig from AppConfig
     */
    CoreConfig toCoreConfig() const {
        return CoreConfig{
            .window = window,
            .resourcePath = resourcePath,
            .framesInFlight = framesInFlight
        };
    }

    /**
     * Build SystemsConfig from AppConfig
     */
    SystemsConfig toSystemsConfig() const {
        return SystemsConfig{
            .resourcePath = resourcePath,
            .enableTerrain = enableTerrain,
            .enableWater = enableWater,
            .enableVegetation = enableVegetation,
            .terrainMaxDepth = terrainMaxDepth,
            .terrainSize = terrainSize
        };
    }
};

/**
 * AppComponent - Top-level Fruit component for the application
 *
 * This component combines all modules and provides the complete
 * dependency graph for the rendering engine.
 *
 * Usage:
 * ```cpp
 * AppConfig config;
 * config.window = window;
 * config.resourcePath = "/path/to/resources";
 *
 * fruit::Injector<VulkanContext, RendererSystems> injector(
 *     getAppComponent(config)
 * );
 *
 * VulkanContext& vulkanContext = injector.get<VulkanContext&>();
 * RendererSystems& systems = injector.get<RendererSystems&>();
 * ```
 */
fruit::Component<
    VulkanContext,
    InitContext,
    RenderingInfrastructure,
    DescriptorInfrastructure,
    PostProcessBundle,
    CoreSystemsBundle,
    InfrastructureBundle
> getAppComponent(const AppConfig& config);

/**
 * Simplified component that provides just the core infrastructure
 * without all the rendering systems. Useful for testing.
 */
fruit::Component<
    VulkanContext,
    InitContext,
    RenderingInfrastructure,
    DescriptorInfrastructure
> getCoreAppComponent(const AppConfig& config);

} // namespace di
