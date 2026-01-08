#include "VulkanContext.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_log.h>

// Required for dynamic dispatch loader - only define in one .cpp file
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

bool VulkanContext::initInstance() {
    if (instanceReady) {
        return true;  // Already initialized
    }

    if (!createInstance()) return false;

    instanceReady = true;
    SDL_Log("Vulkan instance ready (early init phase complete)");
    return true;
}

bool VulkanContext::initDevice(SDL_Window* win) {
    if (!instanceReady) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "initDevice called before initInstance");
        return false;
    }

    window = win;

    if (!createSurface()) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createAllocator()) return false;
    if (!createPipelineCache()) return false;
    if (!createSwapchain()) return false;

    return true;
}

bool VulkanContext::init(SDL_Window* win) {
    // Combined init for backwards compatibility
    if (!initInstance()) return false;
    if (!initDevice(win)) return false;
    return true;
}

void VulkanContext::shutdown() {
    if (device != VK_NULL_HANDLE) {
        vk::Device(device).waitIdle();
    }

    destroySwapchain();

    pipelineCache.shutdown();

    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    // Release handles from RAII wrappers before manual destruction
    // (RAII wrappers took ownership, but we need to destroy in specific order with vkb cleanup)
    if (raiiDevice_) {
        raiiDevice_->release();
        raiiDevice_.reset();
    }
    if (raiiPhysicalDevice_) {
        raiiPhysicalDevice_.reset();  // Physical devices don't need destruction
    }
    if (raiiInstance_) {
        raiiInstance_->release();
        raiiInstance_.reset();
    }

    if (device != VK_NULL_HANDLE) {
        vk::Device(device).destroy();
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE) {
        vk::Instance(instance).destroySurfaceKHR(surface);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkb::destroy_debug_utils_messenger(instance, vkbInstance.debug_messenger);
        vk::Instance(instance).destroy();
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

    // Initialize vulkan-hpp dynamic dispatcher with instance-level functions
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance), vkGetInstanceProcAddr);

    // Create RAII wrapper for instance (takes ownership)
    raiiInstance_ = std::make_unique<vk::raii::Instance>(raiiContext_, instance);

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

    vkbPhysicalDevice = physRet.value();
    physicalDevice = vkbPhysicalDevice.physical_device;

    // Create RAII wrapper for physical device (non-owning - physical devices aren't destroyed)
    raiiPhysicalDevice_ = std::make_unique<vk::raii::PhysicalDevice>(*raiiInstance_, physicalDevice);

    return true;
}

bool VulkanContext::createLogicalDevice() {
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    auto devRet = deviceBuilder.build();

    if (!devRet) {
        SDL_Log("Failed to create logical device: %s", devRet.error().message().c_str());
        return false;
    }

    vkbDevice = devRet.value();
    device = vkbDevice.device;

    // Initialize vulkan-hpp dynamic dispatcher with device-level functions
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(device));

    // Create RAII wrapper for device (takes ownership)
    raiiDevice_ = std::make_unique<vk::raii::Device>(*raiiPhysicalDevice_, device);

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

    // Try to get a dedicated transfer queue (as recommended in video for async asset loading)
    auto transferQueueRet = vkbDevice.get_dedicated_queue(vkb::QueueType::transfer);
    if (transferQueueRet) {
        transferQueue_ = transferQueueRet.value();
        auto transferFamilyRet = vkbDevice.get_dedicated_queue_index(vkb::QueueType::transfer);
        if (transferFamilyRet) {
            transferQueueFamily_ = transferFamilyRet.value();
            hasDedicatedTransfer_ = true;
            SDL_Log("Using dedicated transfer queue (family %u)", transferQueueFamily_);
        }
    }

    // Fall back to graphics queue for transfers if no dedicated queue available
    if (!hasDedicatedTransfer_) {
        transferQueue_ = graphicsQueue;
        transferQueueFamily_ = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
        SDL_Log("No dedicated transfer queue, using graphics queue for transfers");
    }

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

    vk::Device vkDevice(device);
    for (auto imageView : swapchainImageViews) {
        vkDevice.destroyImageView(imageView);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDevice.destroySwapchainKHR(swapchain);
        swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanContext::recreateSwapchain() {
    vk::Device(device).waitIdle();
    destroySwapchain();
    return createSwapchain();
}

void VulkanContext::waitIdle() {
    if (device != VK_NULL_HANDLE) {
        vk::Device(device).waitIdle();
    }
}

uint32_t VulkanContext::getGraphicsQueueFamily() const {
    return vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

uint32_t VulkanContext::getPresentQueueFamily() const {
    return vkbDevice.get_queue_index(vkb::QueueType::present).value();
}

uint32_t VulkanContext::getTransferQueueFamily() const {
    return transferQueueFamily_;
}

bool VulkanContext::createPipelineCache() {
    return pipelineCache.init(device, "pipeline_cache.bin");
}
