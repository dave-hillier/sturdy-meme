#include "VulkanContext.h"
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_log.h>

bool VulkanContext::init(SDL_Window* win) {
    window = win;

    if (!createInstance()) return false;
    if (!createSurface()) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createAllocator()) return false;
    if (!createSwapchain()) return false;

    return true;
}

void VulkanContext::shutdown() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    destroySwapchain();

    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkb::destroy_debug_utils_messenger(instance, vkbInstance.debug_messenger);
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

bool VulkanContext::createInstance() {
    vkb::InstanceBuilder builder;
    auto instRet = builder.set_app_name("Vulkan Game")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 2, 0)
        .build();

    if (!instRet) {
        SDL_Log("Failed to create Vulkan instance: %s", instRet.error().message().c_str());
        return false;
    }

    vkbInstance = instRet.value();
    instance = vkbInstance.instance;
    return true;
}

bool VulkanContext::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanContext::selectPhysicalDevice() {
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_FALSE;

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_minimum_version(1, 2)
        .set_surface(surface)
        .set_required_features(features)
        .select();

    if (!physRet) {
        SDL_Log("Failed to select physical device: %s", physRet.error().message().c_str());
        return false;
    }

    physicalDevice = physRet.value().physical_device;
    return true;
}

bool VulkanContext::createLogicalDevice() {
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_FALSE;

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_minimum_version(1, 2)
        .set_surface(surface)
        .set_required_features(features)
        .select();

    if (!physRet) {
        return false;
    }

    vkb::DeviceBuilder deviceBuilder{physRet.value()};
    auto devRet = deviceBuilder.build();

    if (!devRet) {
        SDL_Log("Failed to create logical device: %s", devRet.error().message().c_str());
        return false;
    }

    vkbDevice = devRet.value();
    device = vkbDevice.device;

    auto graphicsQueueRet = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueueRet) {
        SDL_Log("Failed to get graphics queue");
        return false;
    }
    graphicsQueue = graphicsQueueRet.value();

    auto presentQueueRet = vkbDevice.get_queue(vkb::QueueType::present);
    if (!presentQueueRet) {
        SDL_Log("Failed to get present queue");
        return false;
    }
    presentQueue = presentQueueRet.value();

    return true;
}

bool VulkanContext::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        SDL_Log("Failed to create VMA allocator");
        return false;
    }
    return true;
}

bool VulkanContext::createSwapchain() {
    vkb::SwapchainBuilder swapchainBuilder{vkbDevice};
    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .build();

    if (!swapRet) {
        SDL_Log("Failed to create swapchain: %s", swapRet.error().message().c_str());
        return false;
    }

    auto vkbSwapchain = swapRet.value();
    swapchain = vkbSwapchain.swapchain;
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainImageViews = vkbSwapchain.get_image_views().value();
    swapchainImageFormat = vkbSwapchain.image_format;
    swapchainExtent = vkbSwapchain.extent;

    return true;
}

void VulkanContext::destroySwapchain() {
    if (device == VK_NULL_HANDLE) return;

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanContext::recreateSwapchain() {
    vkDeviceWaitIdle(device);
    destroySwapchain();
    return createSwapchain();
}

void VulkanContext::waitIdle() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
}

uint32_t VulkanContext::getGraphicsQueueFamily() const {
    return vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

uint32_t VulkanContext::getPresentQueueFamily() const {
    return vkbDevice.get_queue_index(vkb::QueueType::present).value();
}
