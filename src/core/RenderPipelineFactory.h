#pragma once

#include "RenderPipeline.h"
#include "PerformanceToggles.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class RendererSystems;

/**
 * RenderPipelineFactory - Decouples render pipeline setup from Renderer
 *
 * This factory encapsulates all the lambda captures and system references
 * needed to configure the render pipeline stages. By moving this logic
 * to a separate compilation unit, we reduce Renderer.cpp's include
 * dependencies from ~40 headers to just this factory.
 *
 * The factory maintains a reference to the pipeline state it needs to
 * access during rendering (terrainEnabled, currentFrame, etc.) through
 * the PipelineState struct.
 *
 * Usage:
 *   RenderPipelineFactory::PipelineState state{...};
 *   RenderPipelineFactory::setupPipeline(pipeline, systems, state);
 */
class RenderPipelineFactory {
public:
    /**
     * State needed by pipeline lambdas that is owned by Renderer
     * This avoids tight coupling to Renderer's internal fields
     */
    struct PipelineState {
        // Feature toggles
        bool* terrainEnabled = nullptr;
        bool* physicsDebugEnabled = nullptr;

        // Frame state
        uint32_t* currentFrame = nullptr;

        // Cached state for debug rendering
        glm::mat4* lastViewProj = nullptr;

        // Graphics pipeline for scene rendering
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    };

    /**
     * Configure the render pipeline with all stages and lambdas
     * Must be called after all subsystems are initialized
     */
    static void setupPipeline(
        RenderPipeline& pipeline,
        RendererSystems& systems,
        const PipelineState& state,
        std::function<void(VkCommandBuffer, uint32_t)> recordSceneObjectsFn
    );

    /**
     * Sync performance toggles to pipeline enable states
     */
    static void syncToggles(
        RenderPipeline& pipeline,
        const PerformanceToggles& toggles
    );
};
