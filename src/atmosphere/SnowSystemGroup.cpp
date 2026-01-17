// SnowSystemGroup.cpp - Self-initialization for snow and weather systems

#include "SnowSystemGroup.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include <SDL3/SDL.h>

std::optional<SnowSystemGroup::Bundle> SnowSystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;

    // 1. Create SnowMask and VolumetricSnow via existing createWithDependencies
    {
        auto snowBundle = SnowMaskSystem::createWithDependencies(deps.ctx, deps.hdrRenderPass);
        if (!snowBundle) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SnowSystemGroup: Failed to create snow subsystems");
            return std::nullopt;
        }
        bundle.snowMask = std::move(snowBundle->snowMask);
        bundle.volumetricSnow = std::move(snowBundle->volumetricSnow);
    }

    // 2. Create Weather and Leaf via existing createWithDependencies
    {
        auto weatherBundle = WeatherSystem::createWithDependencies(deps.ctx, deps.hdrRenderPass);
        if (!weatherBundle) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SnowSystemGroup: Failed to create weather subsystems");
            return std::nullopt;
        }
        bundle.weather = std::move(weatherBundle->weather);
        bundle.leaf = std::move(weatherBundle->leaf);
    }

    SDL_Log("SnowSystemGroup: All systems created successfully");
    return bundle;
}
