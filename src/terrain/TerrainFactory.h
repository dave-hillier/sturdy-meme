#pragma once

#include "TerrainSystem.h"
#include "InitContext.h"
#include <string>
#include <memory>
#include <functional>

/**
 * TerrainFactory - Factory for creating and configuring TerrainSystem
 *
 * Encapsulates the complex configuration of TerrainSystem, providing sensible
 * defaults while allowing customization. Reduces coupling in Renderer initialization.
 *
 * Usage:
 *   TerrainFactory::Config config;
 *   config.resourcePath = resourcePath;
 *   config.hdrRenderPass = postProcess.getHDRRenderPass();
 *   config.shadowRenderPass = shadow.getShadowRenderPass();
 *   config.shadowMapSize = shadow.getShadowMapSize();
 *
 *   auto terrain = TerrainFactory::create(initCtx, config);
 */
class TerrainFactory {
public:
    // Callback invoked during long operations to yield to the UI
    using YieldCallback = std::function<void(float, const char*)>;

    /**
     * Configuration for terrain creation with sensible defaults.
     */
    struct Config {
        // Required resources
        VkRenderPass hdrRenderPass = VK_NULL_HANDLE;
        VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
        uint32_t shadowMapSize = 2048;
        std::string resourcePath;

        // Terrain geometry
        float size = 16384.0f;
        uint32_t maxDepth = 20;
        uint32_t minDepth = 5;
        float targetEdgePixels = 16.0f;
        float splitThreshold = 100.0f;
        float mergeThreshold = 50.0f;

        // Height scale: h=1 maps to this world height
        float heightScale = 235.0f;
        float seaLevel = 15.0f;

        // LOD tile streaming
        float tileLoadRadius = 2000.0f;
        float tileUnloadRadius = 3000.0f;

        // Virtual texturing
        bool useVirtualTexture = true;

        // Loading callback
        YieldCallback yieldCallback;  // Optional: yield during long operations
    };

    /**
     * Create and initialize a TerrainSystem with the given configuration.
     * Returns nullptr if initialization fails.
     */
    static std::unique_ptr<TerrainSystem> create(const InitContext& ctx, const Config& config);

    /**
     * Get the TerrainConfig used by a created system.
     * Useful for passing to other systems that need terrain parameters.
     */
    static TerrainConfig buildTerrainConfig(const Config& config);
};
