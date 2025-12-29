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
    ctx.device = vulkanContext.getDevice();
    ctx.physicalDevice = vulkanContext.getPhysicalDevice();
    ctx.allocator = vulkanContext.getAllocator();
    ctx.graphicsQueue = vulkanContext.getGraphicsQueue();
    ctx.commandPool = cmdPool;
    ctx.descriptorPool = descPool;
    ctx.shaderPath = resourcePath + "/shaders";
    ctx.resourcePath = resourcePath;
    ctx.framesInFlight = framesInFlight;
    ctx.extent = vulkanContext.getSwapchainExtent();
    ctx.poolSizesHint = poolSizes;
    return ctx;
}
