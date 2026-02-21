// GeometrySystemGroup.cpp - Self-initialization for geometry processing systems

#include "GeometrySystemGroup.h"
#include "CatmullClarkSystem.h"
#include "RendererSystems.h"
#include "ResizeCoordinator.h"
#include <SDL3/SDL.h>

void GeometrySystemGroup::Bundle::registerAll(RendererSystems& systems) {
    systems.registry().add<CatmullClarkSystem>(std::move(catmullClark));
}

bool GeometrySystemGroup::createAndRegister(const CreateDeps& deps, RendererSystems& systems) {
    auto bundle = createAll(deps);
    if (!bundle) return false;
    bundle->registerAll(systems);
    return true;
}

std::optional<GeometrySystemGroup::Bundle> GeometrySystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;

    // Compute Catmull-Clark mesh position using terrain height
    CatmullClarkConfig config = deps.catmullClarkConfig;

    // Default position if not specified
    if (config.position == glm::vec3(0.0f)) {
        float suzanneX = 5.0f;
        float suzanneZ = -5.0f;
        float height = deps.getTerrainHeight ? deps.getTerrainHeight(suzanneX, suzanneZ) : 0.0f;
        config.position = glm::vec3(suzanneX, height + 2.0f, suzanneZ);
    }

    // Set default obj path if not specified
    if (config.objPath.empty()) {
        config.objPath = deps.resourcePath + "/assets/suzanne.obj";
    }

    // Set sensible defaults if not specified
    if (config.scale == glm::vec3(0.0f)) {
        config.scale = glm::vec3(2.0f);
    }
    if (config.targetEdgePixels == 0.0f) {
        config.targetEdgePixels = 12.0f;
    }
    if (config.maxDepth == 0) {
        config.maxDepth = 16;
    }
    if (config.splitThreshold == 0.0f) {
        config.splitThreshold = 18.0f;
    }
    if (config.mergeThreshold == 0.0f) {
        config.mergeThreshold = 6.0f;
    }

    // Build InitInfo from InitContext
    CatmullClarkSystem::InitInfo initInfo{};
    initInfo.device = deps.ctx.device;
    initInfo.physicalDevice = deps.ctx.physicalDevice;
    initInfo.allocator = deps.ctx.allocator;
    initInfo.renderPass = deps.hdrRenderPass;
    initInfo.descriptorPool = deps.ctx.descriptorPool;
    initInfo.extent = deps.ctx.extent;
    initInfo.shaderPath = deps.ctx.shaderPath;
    initInfo.framesInFlight = deps.ctx.framesInFlight;
    initInfo.graphicsQueue = deps.ctx.graphicsQueue;
    initInfo.commandPool = deps.ctx.commandPool;
    initInfo.raiiDevice = deps.ctx.raiiDevice;

    // Create CatmullClarkSystem
    bundle.catmullClark = CatmullClarkSystem::create(initInfo, config);
    if (!bundle.catmullClark) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GeometrySystemGroup: Failed to create CatmullClarkSystem");
        return std::nullopt;
    }

    // Update descriptor sets with uniform buffers
    bundle.catmullClark->updateDescriptorSets(deps.ctx.device, deps.uniformBuffers);

    SDL_Log("GeometrySystemGroup: All systems created successfully");
    return bundle;
}

void GeometrySystemGroup::registerResize(ResizeCoordinator& coord, RendererSystems& systems) {
    coord.registerWithExtent(systems.catmullClark(), "CatmullClarkSystem");
}
