#include "VulkanServices.h"
#include "vulkan/VulkanContext.h"

VulkanServices::VulkanServices(
    const VulkanContext& context,
    DescriptorManager::Pool* descriptorPool,
    const std::string& resourcePath
)
    : device_(context.getVkDevice())
    , physicalDevice_(context.getVkPhysicalDevice())
    , allocator_(context.getAllocator())
    , graphicsQueue_(context.getVkGraphicsQueue())
    , commandPool_(context.getCommandPool())
    , descriptorPool_(descriptorPool)
    , raiiDevice_(&context.getRaiiDevice())
    , shaderPath_(resourcePath + "/shaders")
    , resourcePath_(resourcePath)
    , framesInFlight_(3)  // Default, can be overridden
    , extent_(context.getVkSwapchainExtent())
{
}

VulkanServices::VulkanServices(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VmaAllocator allocator,
    VkQueue graphicsQueue,
    VkCommandPool commandPool,
    DescriptorManager::Pool* descriptorPool,
    const vk::raii::Device* raiiDevice,
    const std::string& shaderPath,
    const std::string& resourcePath,
    uint32_t framesInFlight,
    VkExtent2D extent
)
    : device_(device)
    , physicalDevice_(physicalDevice)
    , allocator_(allocator)
    , graphicsQueue_(graphicsQueue)
    , commandPool_(commandPool)
    , descriptorPool_(descriptorPool)
    , raiiDevice_(raiiDevice)
    , shaderPath_(shaderPath)
    , resourcePath_(resourcePath)
    , framesInFlight_(framesInFlight)
    , extent_(extent)
{
}
