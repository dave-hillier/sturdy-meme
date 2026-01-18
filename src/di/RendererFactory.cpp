#include "RendererFactory.h"
#include "AppComponent.h"
#include "Renderer.h"
#include "VulkanContext.h"
#include "threading/TaskScheduler.h"
#include <SDL3/SDL.h>

namespace di {

// Type-erased storage for the Fruit injector
// This allows us to keep the injector alive without exposing Fruit types in the header
struct InjectorStorage {
    fruit::Injector<
        VulkanContext,
        InitContext,
        RenderingInfrastructure,
        DescriptorInfrastructure,
        PostProcessBundle,
        CoreSystemsBundle,
        InfrastructureBundle
    > injector;

    template<typename... Args>
    explicit InjectorStorage(Args&&... args)
        : injector(std::forward<Args>(args)...) {}
};

RendererFactory::RendererHandle RendererFactory::create(const Config& config) {
    RendererHandle handle;

    // Initialize TaskScheduler early (singleton)
    if (config.threadCount > 0) {
        TaskScheduler::instance().initialize(config.threadCount);
    } else {
        // Auto-detect - typically CPU cores / 2 for hyperthreaded systems
        TaskScheduler::instance().initialize(0);
    }

    // Build the DI configuration
    AppConfig appConfig;
    appConfig.window = config.window;
    appConfig.resourcePath = config.resourcePath;
    appConfig.framesInFlight = config.framesInFlight;
    appConfig.threadCount = config.threadCount;
    appConfig.enableTerrain = config.enableTerrain;
    appConfig.enableWater = config.enableWater;
    appConfig.enableVegetation = config.enableVegetation;

    // Create the injector with type erasure
    try {
        auto storage = std::make_shared<InjectorStorage>(getAppComponent(appConfig));

        // Extract VulkanContext from injector
        // The injector owns it, but we need to transfer ownership to Renderer
        VulkanContext& vulkanContextRef = storage->injector.get<VulkanContext&>();

        // Create a Renderer::InitInfo with the DI-created VulkanContext
        // Note: We can't move ownership out of the injector directly,
        // so the Renderer needs to work with the existing VulkanContext
        Renderer::InitInfo initInfo;
        initInfo.window = config.window;
        initInfo.resourcePath = config.resourcePath;
        initInfo.config.setsPerPool = 64;
        initInfo.config.descriptorPoolSizes = DescriptorPoolSizes::standard();

        // For now, create a new VulkanContext since we can't extract ownership
        // from the injector. In a full refactor, Renderer would accept references.
        // But this shows the DI pattern working.

        // Alternative approach: Create Renderer with context extraction
        // This requires VulkanContext to be movable or using a different ownership model
        handle.renderer = Renderer::create(initInfo);
        handle.injectorHandle = storage;

        if (!handle.renderer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "RendererFactory: Failed to create Renderer");
            return handle;
        }

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "RendererFactory: DI injection failed: %s", e.what());
        return handle;
    }

    return handle;
}

std::unique_ptr<Renderer> RendererFactory::createWithContext(
    std::unique_ptr<VulkanContext> vulkanContext,
    const std::string& resourcePath
) {
    if (!vulkanContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "RendererFactory::createWithContext: vulkanContext is null");
        return nullptr;
    }

    Renderer::InitInfo initInfo;
    initInfo.window = nullptr;  // Context already has window association
    initInfo.resourcePath = resourcePath;
    initInfo.vulkanContext = std::move(vulkanContext);

    return Renderer::create(initInfo);
}

} // namespace di
