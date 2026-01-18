#pragma once

#include <fruit/fruit.h>
#include "VulkanServices.h"

class VulkanContext;
class DescriptorManager;

namespace di {

/**
 * Configuration passed to the Vulkan component
 */
struct VulkanConfig {
    VulkanContext* context = nullptr;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::string resourcePath;
    uint32_t framesInFlight = 3;
};

/**
 * Fruit component that provides VulkanServices
 *
 * Usage:
 *   VulkanConfig config{&vulkanContext, descriptorPool, resourcePath, 3};
 *   fruit::Injector<VulkanServices> injector(getVulkanComponent, &config);
 *   VulkanServices& services = injector.get<VulkanServices&>();
 *
 * Systems can then be written to take VulkanServices& as a dependency:
 *
 *   class MySystem {
 *   public:
 *       INJECT(MySystem(VulkanServices& services))
 *           : services_(services) {}
 *   private:
 *       VulkanServices& services_;
 *   };
 */
fruit::Component<VulkanServices> getVulkanComponent(VulkanConfig* config);

/**
 * Helper to create VulkanServices without full DI
 * (for gradual migration)
 */
inline std::unique_ptr<VulkanServices> createVulkanServices(
    const VulkanContext& context,
    DescriptorManager::Pool* descriptorPool,
    const std::string& resourcePath
) {
    return std::make_unique<VulkanServices>(context, descriptorPool, resourcePath);
}

} // namespace di
