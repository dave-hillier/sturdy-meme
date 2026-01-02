#include "InitContext.h"
#include "vulkan/VulkanContext.h"

InitContext InitContext::build(
    const VulkanContext& vulkanContext,
    VkCommandPool cmdPool,
    DescriptorManager::Pool* descPool,
    const std::string& resourcePath,
    uint32_t framesInFlight,
    std::optional<DescriptorPoolSizes> poolSizes
) {
    InitContext ctx{};
    ctx.raiiDevice = &vulkanContext.getRaiiDevice();
    ctx.device = vulkanContext.getVkDevice();
    ctx.physicalDevice = vulkanContext.getVkPhysicalDevice();
    ctx.allocator = vulkanContext.getAllocator();
    ctx.raiiDevice = &vulkanContext.getRaiiDevice();
    ctx.graphicsQueue = vulkanContext.getVkGraphicsQueue();
    ctx.commandPool = cmdPool;
    ctx.descriptorPool = descPool;
    ctx.shaderPath = resourcePath + "/shaders";
    ctx.resourcePath = resourcePath;
    ctx.framesInFlight = framesInFlight;
    ctx.extent = vulkanContext.getVkSwapchainExtent();
    ctx.poolSizesHint = poolSizes;
    return ctx;
}
