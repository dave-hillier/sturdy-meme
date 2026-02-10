#include "VulkanContext.h"
#include "VulkanHelpers.h"
#include "MetalLayerFix.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_log.h>
#include <cstdlib>

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

    // Destroy command pool and buffers before swapchain resources
    destroyCommandPoolAndBuffers();

    // Destroy swapchain-dependent resources (render pass, depth buffer, framebuffers)
    destroySwapchainResources();

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

    // Disable validation layers in release builds for performance
    // Validation layers add significant overhead to vkQueueSubmit
#ifdef NDEBUG
    constexpr bool enableValidation = false;
#else
    // Can be overridden via environment variable for profiling debug builds
    const bool enableValidation = (std::getenv("DISABLE_VULKAN_VALIDATION") == nullptr);
#endif

    if (!enableValidation) {
        SDL_Log("Vulkan validation layers disabled");
    }

    auto instRet = builder.set_app_name("Vulkan Game")
        .request_validation_layers(enableValidation)
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

    // Vulkan 1.2 required features - only timeline semaphores are mandatory
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;  // Required for non-blocking GPU timeline queries

    // Descriptor indexing features are OPTIONAL - try with them first, fall back without.
    // MoltenVK may report partial support that causes issues at runtime.
    VkPhysicalDeviceVulkan12Features vulkan12FeaturesWithBindless = vulkan12Features;
    vulkan12FeaturesWithBindless.descriptorIndexing = VK_TRUE;
    vulkan12FeaturesWithBindless.runtimeDescriptorArray = VK_TRUE;
    vulkan12FeaturesWithBindless.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12FeaturesWithBindless.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vulkan12FeaturesWithBindless.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12FeaturesWithBindless.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    // First try: select device with descriptor indexing (bindless rendering)
    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_minimum_version(1, 2)
        .set_surface(surface)
        .set_required_features(features)
        .set_required_features_12(vulkan12FeaturesWithBindless)
        .select();

    if (!physRet) {
        // Fallback: select device WITHOUT descriptor indexing features
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Device selection with descriptor indexing failed (%s), retrying without",
            physRet.error().message().c_str());

        vkb::PhysicalDeviceSelector fallbackSelector{vkbInstance};
        physRet = fallbackSelector.set_minimum_version(1, 2)
            .set_surface(surface)
            .set_required_features(features)
            .set_required_features_12(vulkan12Features)
            .select();

        if (!physRet) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Failed to select physical device: %s",
                physRet.error().message().c_str());
            return false;
        }
    }

    vkbPhysicalDevice = physRet.value();
    physicalDevice = vkbPhysicalDevice.physical_device;

    // Verify Vulkan 1.2 API version
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);

    if (major < 1 || (major == 1 && minor < 2)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Physical device does not support Vulkan 1.2 (found %u.%u)", major, minor);
        return false;
    }

    SDL_Log("Selected physical device: %s (Vulkan %u.%u.%u)",
        props.deviceName, major, minor, VK_API_VERSION_PATCH(props.apiVersion));

    // Verify feature support (should always be true if we got here since they were required)
    VkPhysicalDeviceVulkan12Features supportedFeatures12{};
    supportedFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &supportedFeatures12;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    hasTimelineSemaphores_ = supportedFeatures12.timelineSemaphore == VK_TRUE;
    if (hasTimelineSemaphores_) {
        SDL_Log("Timeline semaphores supported and enabled");
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Timeline semaphores not supported - falling back to fences");
    }

    // Verify descriptor indexing features
    hasDescriptorIndexing_ =
        supportedFeatures12.descriptorIndexing == VK_TRUE &&
        supportedFeatures12.runtimeDescriptorArray == VK_TRUE &&
        supportedFeatures12.descriptorBindingPartiallyBound == VK_TRUE &&
        supportedFeatures12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
        supportedFeatures12.descriptorBindingVariableDescriptorCount == VK_TRUE &&
        supportedFeatures12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;

    // Disable bindless on MoltenVK — it reports descriptor indexing support
    // but crashes in mvkUpdateDescriptorSets with update-after-bind descriptors
    if (hasDescriptorIndexing_) {
        VkPhysicalDeviceVulkan12Properties driverProps12{};
        driverProps12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        VkPhysicalDeviceProperties2 driverProps2{};
        driverProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        driverProps2.pNext = &driverProps12;
        vkGetPhysicalDeviceProperties2(physicalDevice, &driverProps2);

        if (driverProps12.driverID == VK_DRIVER_ID_MOLTENVK) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "MoltenVK detected — disabling bindless rendering (update-after-bind not reliable)");
            hasDescriptorIndexing_ = false;
        }
    }

    if (hasDescriptorIndexing_) {
        // Query limits for bindless texture array size
        VkPhysicalDeviceVulkan12Properties props12{};
        props12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &props12;
        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        maxBindlessTextures_ = props12.maxDescriptorSetUpdateAfterBindSampledImages;
        // Cap at a practical limit
        if (maxBindlessTextures_ > 16384) {
            maxBindlessTextures_ = 16384;
        }

        SDL_Log("Descriptor indexing enabled: bindless textures supported (max %u)", maxBindlessTextures_);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Descriptor indexing features not fully supported - bindless rendering unavailable");
    }

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
    // Query surface capabilities to understand composite alpha support
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

    // Log supported composite alpha modes for debugging ghost frame issues
    SDL_Log("Swapchain: Supported composite alpha modes: %s%s%s%s",
        (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? "OPAQUE " : "",
        (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) ? "PRE_MULTIPLIED " : "",
        (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) ? "POST_MULTIPLIED " : "",
        (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) ? "INHERIT " : "");

    // Prefer OPAQUE to prevent compositor alpha blending, fall back to INHERIT if not supported
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Swapchain: OPAQUE composite alpha not supported, using INHERIT. "
                "Ghost frames may occur on window background/restore.");
        } else if (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Swapchain: Using PRE_MULTIPLIED composite alpha");
        }
    }

    vkb::SwapchainBuilder swapchainBuilder{vkbDevice};
    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        // Use selected composite alpha mode to prevent ghost frames
        .set_composite_alpha_flags(compositeAlpha)
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

    SDL_Log("Swapchain: Created with composite alpha mode: %s",
        compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ? "OPAQUE" :
        compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR ? "INHERIT" :
        compositeAlpha == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ? "PRE_MULTIPLIED" :
        "POST_MULTIPLIED");

    // On macOS with INHERIT composite alpha, force the Metal layer to be opaque
    // to prevent the compositor from blending through to stale cached content
    ensureMetalLayerOpaque(window);

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

void VulkanContext::clearSwapchainImages() {
    // Clear and PRESENT all swapchain images to eliminate ghost frames after resize
    // Simply clearing isn't enough - we must present to force the compositor to update
    // This cycles through all swapchain images, acquiring, clearing, and presenting each

    if (swapchainImages.empty() || !commandPool_ || swapchain == VK_NULL_HANDLE) {
        return;
    }

    vk::Device vkDevice(device);
    vk::Queue vkGfxQueue(graphicsQueue);
    vk::Queue vkPresentQueue(presentQueue);
    vk::SwapchainKHR vkSwapchain(swapchain);

    // Create a temporary semaphore for synchronization
    vk::Semaphore acquireSem, renderSem;
    try {
        acquireSem = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{});
        renderSem = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{});
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create semaphores for swapchain clear: %s", e.what());
        return;
    }

    // Allocate a temporary command buffer
    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(**commandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    std::vector<vk::CommandBuffer> cmdBuffers;
    try {
        cmdBuffers = vkDevice.allocateCommandBuffers(allocInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate command buffer for swapchain clear: %s", e.what());
        vkDevice.destroySemaphore(acquireSem);
        vkDevice.destroySemaphore(renderSem);
        return;
    }

    vk::CommandBuffer cmd = cmdBuffers[0];

    // Clear color value (black with full alpha)
    vk::ClearColorValue clearColor{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};

    vk::ImageSubresourceRange range{};
    range.aspectMask = vk::ImageAspectFlagBits::eColor;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    // Present each swapchain image to flush the compositor
    uint32_t presentCount = 0;
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        // Acquire the next image
        uint32_t imageIndex;
        auto acquireResult = vkDevice.acquireNextImageKHR(vkSwapchain, UINT64_MAX, acquireSem, nullptr);
        if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain image %zu during clear", i);
            continue;
        }
        imageIndex = acquireResult.value;

        // Record clear command for this image
        try {
            cmd.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            VkImage image = swapchainImages[imageIndex];

            // Transition to TRANSFER_DST for clearing
            auto toTransfer = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eNone)
                .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(image)
                .setSubresourceRange(range);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {}, toTransfer);

            // Clear the image
            cmd.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, clearColor, range);

            // Transition to PRESENT_SRC for presentation
            auto toPresent = vk::ImageMemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(vk::AccessFlagBits::eNone)
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(image)
                .setSubresourceRange(range);

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eBottomOfPipe,
                {}, {}, {}, toPresent);

            cmd.end();

            // Submit with proper semaphore wait/signal
            vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTransfer;
            auto submitInfo = vk::SubmitInfo{}
                .setWaitSemaphores(acquireSem)
                .setWaitDstStageMask(waitStage)
                .setCommandBuffers(cmd)
                .setSignalSemaphores(renderSem);

            vkGfxQueue.submit(submitInfo, nullptr);

            // Present the cleared image
            auto presentInfo = vk::PresentInfoKHR{}
                .setWaitSemaphores(renderSem)
                .setSwapchains(vkSwapchain)
                .setImageIndices(imageIndex);

            auto presentResult = vkPresentQueue.presentKHR(presentInfo);
            if (presentResult == vk::Result::eSuccess || presentResult == vk::Result::eSuboptimalKHR) {
                presentCount++;
            }

            // Wait for this present to complete before next iteration
            vkGfxQueue.waitIdle();

            // Reset command buffer for next use
            cmd.reset(vk::CommandBufferResetFlagBits::eReleaseResources);

        } catch (const vk::SystemError& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Error during swapchain clear for image %zu: %s", i, e.what());
        }
    }

    SDL_Log("Cleared and presented %u/%zu swapchain images to eliminate ghost frames",
            presentCount, swapchainImages.size());

    // Cleanup
    vkDevice.freeCommandBuffers(**commandPool_, cmd);
    vkDevice.destroySemaphore(acquireSem);
    vkDevice.destroySemaphore(renderSem);
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
    return pipelineCache.init(*raiiDevice_, "pipeline_cache.bin");
}

// ============================================================================
// Swapchain-dependent resource creation
// ============================================================================

bool VulkanContext::createRenderPass() {
    depthFormat_ = VK_FORMAT_D32_SFLOAT;

    RenderPassConfig config{};
    config.colorFormat = swapchainImageFormat;
    config.depthFormat = depthFormat_;
    config.finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    config.finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // For Hi-Z
    config.storeDepth = true;  // For Hi-Z pyramid generation

    auto result = ::createRenderPass(*raiiDevice_, config);
    if (!result) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pass");
        return false;
    }
    renderPass_.emplace(std::move(*result));
    return true;
}

bool VulkanContext::createDepthResources() {
    DepthResources depth;
    vk::Extent2D extent{swapchainExtent.width, swapchainExtent.height};
    if (!::createDepthResources(*raiiDevice_, allocator, extent,
                                 static_cast<vk::Format>(depthFormat_), depth)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth resources");
        return false;
    }
    // Move RAII resources from temporary DepthResources to member fields
    depthImage_ = std::move(depth.image);
    depthImageView_ = std::move(depth.view);
    depthSampler_ = std::move(depth.sampler);
    return true;
}

bool VulkanContext::createFramebuffers() {
    if (!renderPass_ || !depthImageView_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot create framebuffers: render pass or depth view not ready");
        return false;
    }

    auto result = ::createFramebuffers(*raiiDevice_, *renderPass_,
                                        swapchainImageViews, **depthImageView_,
                                        swapchainExtent);
    if (!result) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create framebuffers");
        return false;
    }

    framebuffers_ = std::move(*result);
    return true;
}

bool VulkanContext::recreateDepthResources() {
    // Destroy existing depth resources (keep sampler - format doesn't change)
    depthImageView_.reset();
    depthImage_.reset();

    vk::Extent2D extent{swapchainExtent.width, swapchainExtent.height};
    VmaImage newImage;
    std::optional<vk::raii::ImageView> newView;

    if (!::createDepthImageAndView(*raiiDevice_, allocator, extent,
                                    static_cast<vk::Format>(depthFormat_),
                                    newImage, newView)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate depth resources");
        return false;
    }

    depthImage_ = std::move(newImage);
    depthImageView_ = std::move(newView);
    return true;
}

bool VulkanContext::createSwapchainResources() {
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    SDL_Log("Swapchain resources created (render pass, depth buffer, framebuffers)");
    return true;
}

void VulkanContext::destroySwapchainResources() {
    // RAII handles cleanup - just clear containers and reset managed objects
    framebuffers_.clear();
    depthSampler_.reset();
    depthImageView_.reset();
    depthImage_.reset();
    renderPass_.reset();
}

bool VulkanContext::recreateSwapchainResources() {
    // Handle minimized window (extent = 0)
    if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
        return true;  // Nothing to do for minimized window
    }

    // Recreate depth resources for new extent
    if (!recreateDepthResources()) {
        return false;
    }

    // Recreate framebuffers for new swapchain
    framebuffers_.clear();
    if (!createFramebuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate framebuffers during resize");
        return false;
    }

    SDL_Log("Swapchain resources recreated for %ux%u", swapchainExtent.width, swapchainExtent.height);
    return true;
}

// ============================================================================
// Command pool and buffers
// ============================================================================

bool VulkanContext::createCommandPoolAndBuffers(uint32_t frameCount) {
    if (frameCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot create command buffers: frame count is 0");
        return false;
    }

    try {
        auto poolInfo = vk::CommandPoolCreateInfo{}
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(getGraphicsQueueFamily());
        commandPool_.emplace(*raiiDevice_, poolInfo);

        commandBuffers_.resize(frameCount);

        auto allocInfo = vk::CommandBufferAllocateInfo{}
            .setCommandPool(**commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(frameCount);

        if (vkAllocateCommandBuffers(device,
                reinterpret_cast<const VkCommandBufferAllocateInfo*>(&allocInfo),
                commandBuffers_.data()) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate command buffers");
            commandPool_.reset();
            commandBuffers_.clear();
            return false;
        }

        SDL_Log("Command pool and %u command buffers created", frameCount);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create command pool: %s", e.what());
        return false;
    }
}

void VulkanContext::destroyCommandPoolAndBuffers() {
    // Command buffers are implicitly freed when pool is destroyed
    commandBuffers_.clear();
    commandPool_.reset();
}
