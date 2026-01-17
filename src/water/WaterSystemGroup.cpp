// WaterSystemGroup.cpp - Self-initialization for water systems

#include "WaterSystemGroup.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include <SDL3/SDL.h>

std::optional<WaterSystemGroup::Bundle> WaterSystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;
    const auto& ctx = deps.ctx;

    // 1. Create WaterSystem (main water rendering)
    {
        WaterSystem::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.descriptorPool = ctx.descriptorPool;
        info.hdrRenderPass = deps.hdrRenderPass;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.extent = ctx.extent;
        info.commandPool = ctx.commandPool;
        info.graphicsQueue = ctx.graphicsQueue;
        info.waterSize = deps.waterSize;
        info.assetPath = deps.assetPath;
        info.raiiDevice = ctx.raiiDevice;

        bundle.system = WaterSystem::create(info);
        if (!bundle.system) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create WaterSystem");
            return std::nullopt;
        }
    }

    // 2. Create FlowMapGenerator
    {
        FlowMapGenerator::InitInfo info{};
        info.device = ctx.device;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.queue = ctx.graphicsQueue;
        info.raiiDevice = ctx.raiiDevice;

        bundle.flowMap = FlowMapGenerator::create(info);
        if (!bundle.flowMap) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create FlowMapGenerator");
            return std::nullopt;
        }
    }

    // 3. Create WaterDisplacement (FFT waves)
    {
        WaterDisplacement::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.framesInFlight = ctx.framesInFlight;
        info.displacementResolution = 512;
        info.worldSize = deps.waterSize;
        info.raiiDevice = ctx.raiiDevice;

        bundle.displacement = WaterDisplacement::create(info);
        if (!bundle.displacement) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create WaterDisplacement");
            return std::nullopt;
        }
    }

    // 4. Create FoamBuffer
    {
        FoamBuffer::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.resolution = 512;
        info.worldSize = deps.waterSize;
        info.raiiDevice = ctx.raiiDevice;

        bundle.foam = FoamBuffer::create(info);
        if (!bundle.foam) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create FoamBuffer");
            return std::nullopt;
        }
    }

    // 5. Create SSRSystem
    bundle.ssr = SSRSystem::create(ctx);
    if (!bundle.ssr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: Failed to create SSRSystem");
        return std::nullopt;
    }

    // 6. Create WaterTileCull (optional)
    {
        WaterTileCull::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.commandPool = ctx.commandPool;
        info.computeQueue = ctx.graphicsQueue;
        info.shaderPath = ctx.shaderPath;
        info.framesInFlight = ctx.framesInFlight;
        info.extent = ctx.extent;
        info.tileSize = 32;
        info.raiiDevice = ctx.raiiDevice;

        bundle.tileCull = WaterTileCull::create(info);
        if (!bundle.tileCull) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: WaterTileCull creation failed (non-fatal)");
        }
    }

    // 7. Create WaterGBuffer (optional)
    {
        WaterGBuffer::InitInfo info{};
        info.device = ctx.device;
        info.physicalDevice = ctx.physicalDevice;
        info.allocator = ctx.allocator;
        info.fullResExtent = ctx.extent;
        info.resolutionScale = 0.5f;
        info.framesInFlight = ctx.framesInFlight;
        info.shaderPath = ctx.shaderPath;
        info.descriptorPool = ctx.descriptorPool;
        info.raiiDevice = ctx.raiiDevice;

        bundle.gBuffer = WaterGBuffer::create(info);
        if (!bundle.gBuffer) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup: WaterGBuffer creation failed (non-fatal)");
        }
    }

    SDL_Log("WaterSystemGroup: All systems created successfully");
    return bundle;
}
