// VegetationSystemGroup.cpp - Self-initialization for vegetation systems

#include "VegetationSystemGroup.h"
#include "DisplacementSystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include <SDL3/SDL.h>

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

    // 2. Create RockSystem with rock placement config
    {
        RockSystem::InitInfo info{};
        info.device = ctx.device;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.physicalDevice = ctx.physicalDevice;
        info.resourcePath = ctx.resourcePath;
        info.terrainSize = deps.terrainSize;
        info.getTerrainHeight = deps.getTerrainHeight;

        bundle.rock = RockSystem::create(info, deps.rockConfig);
        if (!bundle.rock) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup: Failed to create RockSystem");
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
        TreeRenderer::InitInfo info{};
        info.raiiDevice = ctx.raiiDevice;
        info.device = vk::Device(ctx.device);
        info.physicalDevice = vk::PhysicalDevice(ctx.physicalDevice);
        info.allocator = ctx.allocator;
        info.hdrRenderPass = vk::RenderPass(deps.hdrRenderPass);
        info.shadowRenderPass = vk::RenderPass(deps.shadowRenderPass);
        info.descriptorPool = ctx.descriptorPool;
        info.extent = vk::Extent2D{ctx.extent.width, ctx.extent.height};
        info.shadowMapSize = deps.shadowMapSize;
        info.resourcePath = ctx.resourcePath;
        info.maxFramesInFlight = ctx.framesInFlight;

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
