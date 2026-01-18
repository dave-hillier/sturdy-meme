#include "VulkanComponent.h"
#include "vulkan/VulkanContext.h"
#include <SDL3/SDL.h>

namespace di {

fruit::Component<VulkanServices> getVulkanComponent(VulkanConfig* config) {
    return fruit::createComponent()
        .registerProvider([config]() -> VulkanServices* {
            if (!config || !config->context) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "VulkanComponent: config or context is null");
                return nullptr;
            }

            auto services = new VulkanServices(
                *config->context,
                config->descriptorPool,
                config->resourcePath
            );

            return services;
        });
}

} // namespace di
