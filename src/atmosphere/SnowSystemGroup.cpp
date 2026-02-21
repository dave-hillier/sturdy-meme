// SnowSystemGroup.cpp - Self-initialization for snow and weather systems

#include "SnowSystemGroup.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "RendererSystems.h"
#include <SDL3/SDL.h>

void SnowSystemGroup::Bundle::registerAll(RendererSystems& systems) {
    systems.registry().add<SnowMaskSystem>(std::move(snowMask));
    systems.registry().add<VolumetricSnowSystem>(std::move(volumetricSnow));
    systems.registry().add<WeatherSystem>(std::move(weather));
    systems.registry().add<LeafSystem>(std::move(leaf));
}

bool SnowSystemGroup::createAndRegister(const CreateDeps& deps, RendererSystems& systems) {
    auto bundle = createAll(deps);
    if (!bundle) return false;
    bundle->registerAll(systems);
    return true;
}

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
