// DeferredTerrainObjects.cpp - Deferred terrain object generation
// Handles scene objects, trees, and detritus creation after terrain is ready

#include "DeferredTerrainObjects.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "TreeRenderer.h"
#include "ScatterSystem.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include <SDL3/SDL.h>

std::unique_ptr<DeferredTerrainObjects> DeferredTerrainObjects::create(const Config& config) {
    auto instance = std::make_unique<DeferredTerrainObjects>(ConstructToken{});
    if (!instance->initInternal(config)) {
        return nullptr;
    }
    return instance;
}

bool DeferredTerrainObjects::initInternal(const Config& config) {
    config_ = config;
    return true;
}

bool DeferredTerrainObjects::tryGenerate(
    SceneManager* sceneManager,
    TreeSystem* tree,
    TreeLODSystem* treeLOD,
    ImpostorCullSystem* impostorCull,
    TreeRenderer* treeRenderer,
    ScatterSystem* rocks,
    std::unique_ptr<ScatterSystem>& detritus,
    bool terrainReady
) {
    // Already generated - nothing to do
    if (generated_) {
        return false;
    }

    // Wait for terrain to be ready
    if (!terrainReady) {
        return false;
    }

    // Mark as generating (for progress tracking if needed)
    generating_ = true;

    SDL_Log("DeferredTerrainObjects: Terrain ready, generating scene and vegetation content...");

    // First, create scene objects (player, crates, etc.) now that terrain heights are available
    if (sceneManager) {
        SceneBuilder& builder = sceneManager->getSceneBuilder();
        if (!builder.hasRenderables()) {
            builder.createRenderablesDeferred();
            SDL_Log("DeferredTerrainObjects: Scene objects created");
        }
    }

    // Create vegetation content generator
    VegetationContentGenerator::Config vegConfig;
    vegConfig.resourcePath = config_.resourcePath;
    vegConfig.getTerrainHeight = config_.getTerrainHeight;
    vegConfig.terrainSize = config_.terrainSize;

    VegetationContentGenerator vegGen(vegConfig);

    // Generate trees if tree system is available
    if (tree) {
        // Generate demo trees
        vegGen.generateDemoTrees(*tree, config_.sceneOrigin);

        // Generate forest
        vegGen.generateForest(*tree, config_.forestCenter, config_.forestRadius, config_.maxTrees);

        // Generate impostor archetypes
        if (treeLOD) {
            vegGen.generateImpostorArchetypes(*tree, *treeLOD);
        }

        // Finalize tree systems
        vegGen.finalizeTreeSystems(
            *tree,
            treeLOD,
            impostorCull,
            treeRenderer,
            config_.uniformBuffers,
            config_.shadowView,
            config_.shadowSampler
        );

        // Invoke callback to create physics colliders for the generated trees
        if (onTreesGenerated_) {
            onTreesGenerated_(*tree);
        }

        SDL_Log("DeferredTerrainObjects: Tree generation complete");
    }

    // Create detritus system (fallen branches scattered near trees)
    if (tree && config_.device != VK_NULL_HANDLE) {
        VegetationContentGenerator::DetritusCreateInfo detritusInfo{
            config_.device,
            config_.allocator,
            config_.commandPool,
            config_.graphicsQueue,
            config_.physicalDevice
        };

        detritus = vegGen.createDetritusSystem(detritusInfo, *tree);

        // Create descriptor sets for detritus if we have a pool
        if (detritus && config_.descriptorPool && getCommonBindings_) {
            if (!detritus->createDescriptorSets(
                    config_.device,
                    *config_.descriptorPool,
                    config_.descriptorSetLayout,
                    config_.framesInFlight,
                    getCommonBindings_)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "DeferredTerrainObjects: Failed to create detritus descriptor sets");
            }
        }

        SDL_Log("DeferredTerrainObjects: Detritus generation complete");
    }

    // Mark as done
    generated_ = true;
    generating_ = false;

    SDL_Log("DeferredTerrainObjects: All vegetation content generated");
    return true;
}
