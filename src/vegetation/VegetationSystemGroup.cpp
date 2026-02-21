// VegetationSystemGroup.cpp - Self-initialization for vegetation systems

#include "VegetationSystemGroup.h"
#include "DisplacementSystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "ScatterSystem.h"
#include "ScatterSystemFactory.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "RendererSystems.h"
#include "ResizeCoordinator.h"
#include "core/InitInfoBuilder.h"
#include <SDL3/SDL.h>

void VegetationSystemGroup::Bundle::registerAll(RendererSystems& systems) {
    systems.registry().add<WindSystem>(std::move(wind));
    systems.registry().add<DisplacementSystem>(std::move(displacement));
    systems.registry().add<GrassSystem>(std::move(grass));
    systems.setRocks(std::move(rocks));  // Non-trivial: updates sceneCollection_
    if (tree) systems.registry().add<TreeSystem>(std::move(tree));
    if (treeRenderer) systems.registry().add<TreeRenderer>(std::move(treeRenderer));
    if (treeLOD) systems.registry().add<TreeLODSystem>(std::move(treeLOD));
    if (impostorCull) systems.registry().add<ImpostorCullSystem>(std::move(impostorCull));
}

bool VegetationSystemGroup::createAndRegister(const CreateDeps& deps, RendererSystems& systems) {
    auto bundle = createAll(deps);
    if (!bundle) return false;
    bundle->registerAll(systems);
    return true;
}

std::optional<VegetationSystemGroup::Bundle> VegetationSystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;
    const auto& ctx = deps.ctx;

    // 1. Create Grass and Wind via existing createWithDependencies
    {
        auto grassBundle = GrassSystem::createWithDependencies(
            ctx, deps.hdrRenderPass, deps.shadowRenderPass, deps.shadowMapSize);
        if (!grassBundle) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create GrassSystem");
            return std::nullopt;
        }
        bundle.grass = std::move(grassBundle->grass);
        bundle.wind = std::move(grassBundle->wind);
    }

    // 1b. Create DisplacementSystem
    {
        bundle.displacement = DisplacementSystem::create(ctx);
        if (!bundle.displacement) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create DisplacementSystem");
            return std::nullopt;
        }
        // Wire environment settings from wind to displacement
        bundle.displacement->setEnvironmentSettings(&bundle.wind->getEnvironmentSettings());
        // Wire displacement system to grass
        bundle.grass->setDisplacementSystem(bundle.displacement.get());
    }

    // 2. Create rock ScatterSystem with rock placement config
    {
        ScatterSystem::InitInfo info{};
        info.device = ctx.device;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.physicalDevice = ctx.physicalDevice;
        info.resourcePath = ctx.resourcePath;
        info.terrainSize = deps.terrainSize;
        info.getTerrainHeight = deps.getTerrainHeight;

        bundle.rocks = ScatterSystemFactory::createRocks(info, deps.rockConfig);
        if (!bundle.rocks) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create rock ScatterSystem");
            return std::nullopt;
        }
    }

    // 3. Create TreeSystem
    {
        TreeSystem::InitInfo info{};
        info.device = ctx.device;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.physicalDevice = ctx.physicalDevice;
        info.resourcePath = ctx.resourcePath;
        info.terrainSize = deps.terrainSize;
        info.getTerrainHeight = deps.getTerrainHeight;

        bundle.tree = TreeSystem::create(info);
        if (!bundle.tree) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create TreeSystem");
            return std::nullopt;
        }
    }

    // 4. Create TreeRenderer
    {
        TreeRenderer::InitInfo info = InitInfoBuilder::fromContext<TreeRenderer::InitInfo>(ctx);
        info.hdrRenderPass = vk::RenderPass(deps.hdrRenderPass);
        info.shadowRenderPass = vk::RenderPass(deps.shadowRenderPass);
        info.shadowMapSize = deps.shadowMapSize;

        bundle.treeRenderer = TreeRenderer::create(info);
        if (!bundle.treeRenderer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create TreeRenderer");
            return std::nullopt;
        }
    }

    // 5. Create TreeLODSystem (optional - failure is non-fatal)
    {
        TreeLODSystem::InitInfo info{};
        info.raiiDevice = ctx.raiiDevice;
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.hdrRenderPass = deps.hdrRenderPass;
        info.shadowRenderPass = deps.shadowRenderPass;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.descriptorPool = ctx.descriptorPool;
        info.extent = ctx.extent;
        info.resourcePath = ctx.resourcePath;
        info.maxFramesInFlight = ctx.framesInFlight;
        info.shadowMapSize = deps.shadowMapSize;

        bundle.treeLOD = TreeLODSystem::create(info);
        if (!bundle.treeLOD) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: TreeLODSystem creation failed (non-fatal)");
        }
    }

    // 6. Create ImpostorCullSystem (optional - failure is non-fatal)
    {
        ImpostorCullSystem::InitInfo info{};
        info.raiiDevice = ctx.raiiDevice;
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.descriptorPool = ctx.descriptorPool;
        info.extent = ctx.extent;
        info.resourcePath = ctx.resourcePath;
        info.maxFramesInFlight = ctx.framesInFlight;
        info.maxTrees = 100000;
        info.maxArchetypes = 16;

        bundle.impostorCull = ImpostorCullSystem::create(info);
        if (!bundle.impostorCull) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: ImpostorCullSystem creation failed (non-fatal)");
        }
    }

    SDL_Log("VegetationSystemGroup: All systems created successfully");
    return bundle;
}

void VegetationSystemGroup::registerResize(ResizeCoordinator& coord, RendererSystems& systems) {
    coord.registerWithExtent(systems.grass(), "GrassSystem");
}

void VegetationSystemGroup::registerTemporalSystems(RendererSystems& systems) {
    if (systems.impostorCull()) {
        systems.registerTemporalSystem(systems.impostorCull());
    }
}
