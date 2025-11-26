#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <array>
#include <limits>

bool Renderer::init(SDL_Window* win, const std::string& resPath) {
    window = win;
    resourcePath = resPath;

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

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }

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

    vkb::PhysicalDevice vkbPhysicalDevice = physRet.value();
    physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
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

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        SDL_Log("Failed to create VMA allocator");
        return false;
    }

    if (!createSwapchain()) return false;
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createShadowResources()) return false;
    if (!createShadowRenderPass()) return false;
    if (!createDynamicShadowResources()) return false;
    if (!createDynamicShadowRenderPass()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool()) return false;

    // Initialize post-process system early to get HDR render pass
    PostProcessSystem::InitInfo postProcessInfo{};
    postProcessInfo.device = device;
    postProcessInfo.allocator = allocator;
    postProcessInfo.outputRenderPass = renderPass;
    postProcessInfo.descriptorPool = descriptorPool;
    postProcessInfo.extent = swapchainExtent;
    postProcessInfo.swapchainFormat = swapchainImageFormat;
    postProcessInfo.shaderPath = resourcePath + "/shaders";
    postProcessInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!postProcessSystem.init(postProcessInfo)) return false;

    if (!createGraphicsPipeline()) return false;
    if (!createSkyPipeline()) return false;
    if (!createShadowPipeline()) return false;
    if (!createDynamicShadowPipeline()) return false;
    if (!createCommandBuffers()) return false;
    if (!createUniformBuffers()) return false;
    if (!createLightBuffers()) return false;

    // Initialize scene lights
    setupSceneLights();

    // Initialize scene (meshes, textures, objects)
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = commandPool;
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;

    if (!sceneBuilder.init(sceneInfo)) return false;

    if (!createDescriptorSets()) return false;

    // Initialize grass system using HDR render pass
    GrassSystem::InitInfo grassInfo{};
    grassInfo.device = device;
    grassInfo.allocator = allocator;
    grassInfo.renderPass = postProcessSystem.getHDRRenderPass();
    grassInfo.shadowRenderPass = shadowRenderPass;
    grassInfo.descriptorPool = descriptorPool;
    grassInfo.extent = swapchainExtent;
    grassInfo.shadowMapSize = SHADOW_MAP_SIZE;
    grassInfo.shaderPath = resourcePath + "/shaders";
    grassInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!grassSystem.init(grassInfo)) return false;

    // Initialize wind system
    WindSystem::InitInfo windInfo{};
    windInfo.device = device;
    windInfo.allocator = allocator;
    windInfo.descriptorPool = descriptorPool;
    windInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!windSystem.init(windInfo)) return false;

    // Get wind buffers for grass descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = windSystem.getBufferInfo(i).buffer;
    }
    grassSystem.updateDescriptorSets(device, uniformBuffers, shadowImageView, shadowSampler, windBuffers, lightBuffers);

    // Initialize weather particle system (rain/snow)
    WeatherSystem::InitInfo weatherInfo{};
    weatherInfo.device = device;
    weatherInfo.allocator = allocator;
    weatherInfo.renderPass = postProcessSystem.getHDRRenderPass();
    weatherInfo.descriptorPool = descriptorPool;
    weatherInfo.extent = swapchainExtent;
    weatherInfo.shaderPath = resourcePath + "/shaders";
    weatherInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;

    if (!weatherSystem.init(weatherInfo)) return false;

    // Update weather system descriptor sets with wind buffers
    weatherSystem.updateDescriptorSets(device, uniformBuffers, windBuffers, depthImageView, shadowSampler);

    // Initialize froxel volumetric fog system (Phase 4.3)
    FroxelSystem::InitInfo froxelInfo{};
    froxelInfo.device = device;
    froxelInfo.allocator = allocator;
    froxelInfo.descriptorPool = descriptorPool;
    froxelInfo.extent = swapchainExtent;
    froxelInfo.shaderPath = resourcePath + "/shaders";
    froxelInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    froxelInfo.shadowMapView = shadowImageView;
    froxelInfo.shadowSampler = shadowSampler;

    if (!froxelSystem.init(froxelInfo)) return false;

    // Connect froxel volume to post-process system for compositing
    postProcessSystem.setFroxelVolume(froxelSystem.getScatteringVolumeView(), froxelSystem.getVolumeSampler());
    postProcessSystem.setFroxelParams(froxelSystem.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);
    postProcessSystem.setFroxelEnabled(true);

    if (!createSyncObjects()) return false;

    return true;
}

void Renderer::setWeatherIntensity(float intensity) {
    weatherSystem.setIntensity(intensity);
}

void Renderer::setWeatherType(uint32_t type) {
    weatherSystem.setWeatherType(type);
}

void Renderer::updatePlayerTransform(const glm::mat4& transform) {
    sceneBuilder.updatePlayerTransform(transform);
}

void Renderer::shutdown() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        sceneBuilder.destroy(allocator, device);

        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformBuffersAllocations[i]);
        }

        // Clean up light buffers
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (lightBuffers.size() > i && lightBuffers[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, lightBuffers[i], lightBufferAllocations[i]);
            }
        }

        grassSystem.destroy(device, allocator);
        windSystem.destroy(device, allocator);
        weatherSystem.destroy(device, allocator);
        froxelSystem.destroy(device, allocator);
        postProcessSystem.destroy(device, allocator);

        vkDestroyPipeline(device, skyPipeline, nullptr);
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        // Shadow cleanup
        if (shadowPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, shadowPipeline, nullptr);
        }
        if (shadowPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        }
        // Destroy per-cascade framebuffers
        for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
            if (cascadeFramebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, cascadeFramebuffers[i], nullptr);
            }
        }
        if (shadowRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, shadowRenderPass, nullptr);
        }
        if (shadowSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, shadowSampler, nullptr);
        }
        // Destroy per-cascade image views
        for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
            if (cascadeImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, cascadeImageViews[i], nullptr);
            }
        }
        if (shadowImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, shadowImageView, nullptr);
        }
        if (shadowImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, shadowImage, shadowImageAllocation);
        }

        // Dynamic shadow cleanup
        destroyDynamicShadowResources();

        vkDestroyCommandPool(device, commandPool, nullptr);

        destroySwapchain();

        vmaDestroyAllocator(allocator);
        vkb::destroy_device(vkbDevice);
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    vkb::destroy_instance(vkbInstance);
}

bool Renderer::createSwapchain() {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    vkb::SwapchainBuilder swapchainBuilder{vkbDevice};
    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(w, h)
        .build();

    if (!swapRet) {
        SDL_Log("Failed to create swapchain: %s", swapRet.error().message().c_str());
        return false;
    }

    vkb::Swapchain vkbSwapchain = swapRet.value();
    swapchain = vkbSwapchain.swapchain;
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainImageViews = vkbSwapchain.get_image_views().value();
    swapchainImageFormat = vkbSwapchain.image_format;
    swapchainExtent = vkbSwapchain.extent;

    return true;
}

void Renderer::destroySwapchain() {
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthImageAllocation);
        depthImage = VK_NULL_HANDLE;
    }

    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();

    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool Renderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    depthFormat = VK_FORMAT_D32_SFLOAT;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create render pass");
        return false;
    }

    return true;
}

bool Renderer::createDepthResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create depth image view");
        return false;
    }

    return true;
}

bool Renderer::createShadowResources() {
    // Create shadow map depth image array (NUM_SHADOW_CASCADES layers)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = SHADOW_MAP_SIZE;
    imageInfo.extent.height = SHADOW_MAP_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = NUM_SHADOW_CASCADES;  // 4 layers for CSM
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &shadowImage, &shadowImageAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow map image array");
        return false;
    }

    // Create shadow map array view (for sampling all cascades)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;  // Array view for shader sampling
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = NUM_SHADOW_CASCADES;

    if (vkCreateImageView(device, &viewInfo, nullptr, &shadowImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow map array view");
        return false;
    }

    // Create per-cascade image views (for rendering to individual layers)
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        VkImageViewCreateInfo cascadeViewInfo{};
        cascadeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cascadeViewInfo.image = shadowImage;
        cascadeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // Single layer view
        cascadeViewInfo.format = VK_FORMAT_D32_SFLOAT;
        cascadeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        cascadeViewInfo.subresourceRange.baseMipLevel = 0;
        cascadeViewInfo.subresourceRange.levelCount = 1;
        cascadeViewInfo.subresourceRange.baseArrayLayer = i;  // Layer for this cascade
        cascadeViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &cascadeViewInfo, nullptr, &cascadeImageViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create cascade image view %u", i);
            return false;
        }
    }

    // Create shadow sampler with depth comparison
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow sampler");
        return false;
    }

    return true;
}

bool Renderer::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow render pass");
        return false;
    }

    // Create per-cascade framebuffers
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &cascadeImageViews[i];  // Per-cascade view
        framebufferInfo.width = SHADOW_MAP_SIZE;
        framebufferInfo.height = SHADOW_MAP_SIZE;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &cascadeFramebuffers[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create cascade framebuffer %u", i);
            return false;
        }
    }

    return true;
}

bool Renderer::createShadowPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load shadow shaders");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Use the same vertex input as main pipeline
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;  // No color attachments

    // Shadow pipeline layout - reuse the main descriptor set layout for compatibility
    // (shadow shader only uses binding 0, but the descriptor sets have all 3 bindings)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;  // Use main layout for compatibility
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create shadow pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

bool Renderer::createDynamicShadowResources() {
    // Resize per-frame vectors
    pointShadowImages.resize(MAX_FRAMES_IN_FLIGHT);
    pointShadowAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    pointShadowArrayViews.resize(MAX_FRAMES_IN_FLIGHT);
    pointShadowFaceViews.resize(MAX_FRAMES_IN_FLIGHT);

    spotShadowImages.resize(MAX_FRAMES_IN_FLIGHT);
    spotShadowAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    spotShadowArrayViews.resize(MAX_FRAMES_IN_FLIGHT);
    spotShadowLayerViews.resize(MAX_FRAMES_IN_FLIGHT);

    pointShadowFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
    spotShadowFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);

    // Create resources per frame
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        // Create point light shadow cube maps
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.height = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = MAX_SHADOW_CASTING_LIGHTS * 6;  // 6 faces per cube map
            imageInfo.format = VK_FORMAT_D32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &pointShadowImages[frame],
                             &pointShadowAllocations[frame], nullptr) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow cube map array");
                return false;
            }

            // Create array view (for sampling in shaders)
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = pointShadowImages[frame];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            viewInfo.format = VK_FORMAT_D32_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = MAX_SHADOW_CASTING_LIGHTS * 6;

            if (vkCreateImageView(device, &viewInfo, nullptr, &pointShadowArrayViews[frame]) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow array view");
                return false;
            }

            // Create per-face views for rendering (we'll only use the first light's faces for now)
            for (uint32_t face = 0; face < 6; face++) {
                VkImageViewCreateInfo faceViewInfo{};
                faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                faceViewInfo.image = pointShadowImages[frame];
                faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                faceViewInfo.format = VK_FORMAT_D32_SFLOAT;
                faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                faceViewInfo.subresourceRange.baseMipLevel = 0;
                faceViewInfo.subresourceRange.levelCount = 1;
                faceViewInfo.subresourceRange.baseArrayLayer = face;  // First light only for now
                faceViewInfo.subresourceRange.layerCount = 1;

                if (vkCreateImageView(device, &faceViewInfo, nullptr, &pointShadowFaceViews[frame][face]) != VK_SUCCESS) {
                    SDL_Log("Failed to create point shadow face view");
                    return false;
                }
            }
        }

        // Create spot light shadow 2D texture array
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.height = DYNAMIC_SHADOW_MAP_SIZE;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = MAX_SHADOW_CASTING_LIGHTS;
            imageInfo.format = VK_FORMAT_D32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &spotShadowImages[frame],
                             &spotShadowAllocations[frame], nullptr) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow texture array");
                return false;
            }

            // Create array view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = spotShadowImages[frame];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = VK_FORMAT_D32_SFLOAT;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = MAX_SHADOW_CASTING_LIGHTS;

            if (vkCreateImageView(device, &viewInfo, nullptr, &spotShadowArrayViews[frame]) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow array view");
                return false;
            }

            // Create per-layer views
            spotShadowLayerViews[frame].resize(MAX_SHADOW_CASTING_LIGHTS);
            for (uint32_t light = 0; light < MAX_SHADOW_CASTING_LIGHTS; light++) {
                VkImageViewCreateInfo layerViewInfo{};
                layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                layerViewInfo.image = spotShadowImages[frame];
                layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                layerViewInfo.format = VK_FORMAT_D32_SFLOAT;
                layerViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                layerViewInfo.subresourceRange.baseMipLevel = 0;
                layerViewInfo.subresourceRange.levelCount = 1;
                layerViewInfo.subresourceRange.baseArrayLayer = light;
                layerViewInfo.subresourceRange.layerCount = 1;

                if (vkCreateImageView(device, &layerViewInfo, nullptr, &spotShadowLayerViews[frame][light]) != VK_SUCCESS) {
                    SDL_Log("Failed to create spot shadow layer view");
                    return false;
                }
            }
        }
    }

    // Create samplers
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &pointShadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create point shadow sampler");
        return false;
    }

    if (vkCreateSampler(device, &samplerInfo, nullptr, &spotShadowSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create spot shadow sampler");
        return false;
    }

    return true;
}

bool Renderer::createDynamicShadowRenderPass() {
    // Same as CSM shadow render pass
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPassDynamic) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow render pass");
        return false;
    }

    // Create framebuffers for each frame
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        // Point shadow framebuffers (6 faces for first light only for now)
        pointShadowFramebuffers[frame].resize(6);
        for (uint32_t face = 0; face < 6; face++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = shadowRenderPassDynamic;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &pointShadowFaceViews[frame][face];
            framebufferInfo.width = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.height = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                  &pointShadowFramebuffers[frame][face]) != VK_SUCCESS) {
                SDL_Log("Failed to create point shadow framebuffer");
                return false;
            }
        }

        // Spot shadow framebuffers (1 per light)
        spotShadowFramebuffers[frame].resize(MAX_SHADOW_CASTING_LIGHTS);
        for (uint32_t light = 0; light < MAX_SHADOW_CASTING_LIGHTS; light++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = shadowRenderPassDynamic;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &spotShadowLayerViews[frame][light];
            framebufferInfo.width = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.height = DYNAMIC_SHADOW_MAP_SIZE;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                  &spotShadowFramebuffers[frame][light]) != VK_SUCCESS) {
                SDL_Log("Failed to create spot shadow framebuffer");
                return false;
            }
        }
    }

    return true;
}

bool Renderer::createDynamicShadowPipeline() {
    // Reuse CSM shadow shaders for now (we'll create specialized ones later if needed)
    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shadow.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shadow.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load dynamic shadow shaders");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front-face culling for shadow maps
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &dynamicShadowPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = dynamicShadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPassDynamic;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dynamicShadowPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic shadow pipeline");
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

void Renderer::destroyDynamicShadowResources() {
    // Destroy framebuffers
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        for (auto fb : pointShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        }
        for (auto fb : spotShadowFramebuffers[frame]) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        }
    }

    // Destroy image views
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        if (pointShadowArrayViews[frame] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, pointShadowArrayViews[frame], nullptr);
        }
        if (spotShadowArrayViews[frame] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, spotShadowArrayViews[frame], nullptr);
        }

        for (auto& faceView : pointShadowFaceViews[frame]) {
            if (faceView != VK_NULL_HANDLE) vkDestroyImageView(device, faceView, nullptr);
        }
        for (auto& layerView : spotShadowLayerViews[frame]) {
            if (layerView != VK_NULL_HANDLE) vkDestroyImageView(device, layerView, nullptr);
        }
    }

    // Destroy images
    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        if (pointShadowImages[frame] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, pointShadowImages[frame], pointShadowAllocations[frame]);
        }
        if (spotShadowImages[frame] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, spotShadowImages[frame], spotShadowAllocations[frame]);
        }
    }

    // Destroy samplers
    if (pointShadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, pointShadowSampler, nullptr);
    if (spotShadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, spotShadowSampler, nullptr);

    // Destroy pipeline and layout
    if (dynamicShadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, dynamicShadowPipeline, nullptr);
    if (dynamicShadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, dynamicShadowPipelineLayout, nullptr);

    // Destroy render pass
    if (shadowRenderPassDynamic != VK_NULL_HANDLE) vkDestroyRenderPass(device, shadowRenderPassDynamic, nullptr);
}

void Renderer::renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex) {
    // TODO: Implement shadow rendering for each light
    // For now, this is a placeholder that will be filled in when we hook up the rendering
}

void Renderer::calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits) {
    // PSSM - Parallel Split Shadow Maps
    // Blend between logarithmic and uniform distribution
    splits.resize(NUM_SHADOW_CASCADES + 1);
    splits[0] = nearClip;

    float clipRange = farClip - nearClip;
    float ratio = farClip / nearClip;

    for (uint32_t i = 1; i <= NUM_SHADOW_CASCADES; i++) {
        float p = static_cast<float>(i) / NUM_SHADOW_CASCADES;

        // Logarithmic split (better near distribution)
        float logSplit = nearClip * std::pow(ratio, p);

        // Uniform split
        float uniformSplit = nearClip + clipRange * p;

        // Blend between log and uniform using lambda
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

glm::mat4 Renderer::calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, float nearSplit, float farSplit) {
    glm::vec3 lightDirNorm = glm::normalize(lightDir);
    if (glm::length(lightDirNorm) < std::numeric_limits<float>::epsilon()) {
        lightDirNorm = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    // Get camera's projection matrix (which has Vulkan Y-flip) and undo the flip for frustum calculation
    glm::mat4 cameraProj = camera.getProjectionMatrix();
    cameraProj[1][1] *= -1.0f;  // Undo Vulkan Y-flip for standard frustum corners

    // Extract frustum parameters from the camera's projection matrix
    // For perspective: proj[0][0] = 1/(aspect*tan(fov/2)), proj[1][1] = 1/tan(fov/2)
    float tanHalfFov = 1.0f / cameraProj[1][1];
    float aspect = cameraProj[1][1] / cameraProj[0][0];

    // Calculate frustum corners at near and far split distances
    float nearHeight = nearSplit * tanHalfFov;
    float nearWidth = nearHeight * aspect;
    float farHeight = farSplit * tanHalfFov;
    float farWidth = farHeight * aspect;

    // Get camera vectors from inverse view matrix
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 camPos = glm::vec3(invView[3]);
    glm::vec3 camForward = -glm::vec3(invView[2]);  // Camera looks down -Z
    glm::vec3 camRight = glm::vec3(invView[0]);
    glm::vec3 camUp = glm::vec3(invView[1]);

    // Calculate frustum corners in world space
    glm::vec3 nearCenter = camPos + camForward * nearSplit;
    glm::vec3 farCenter = camPos + camForward * farSplit;

    std::array<glm::vec3, 8> frustumCorners{
        // Near plane corners
        nearCenter - camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth + camUp * nearHeight,
        nearCenter - camRight * nearWidth + camUp * nearHeight,
        // Far plane corners
        farCenter - camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth + camUp * farHeight,
        farCenter - camRight * farWidth + camUp * farHeight,
    };

    // Calculate frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) {
        center += corner;
    }
    center /= static_cast<float>(frustumCorners.size());

    // Use bounding sphere for uniform shadow map coverage (like the original working code)
    float radius = 0.0f;
    for (const auto& corner : frustumCorners) {
        radius = std::max(radius, glm::length(corner - center));
    }

    // Position light far enough to avoid near-plane clipping
    glm::vec3 up = (std::abs(lightDirNorm.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 lightPos = center + lightDirNorm * (radius + 50.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, center, up);

    // Use sphere-based ortho projection for uniform texel density
    float orthoSize = radius * 1.1f;  // Small margin for safety
    float zRange = radius * 2.0f + 100.0f;  // Cover the full sphere plus padding

    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, zRange);

    // Vulkan corrections:
    // 1. Flip Y (Vulkan has inverted Y compared to OpenGL)
    lightProjection[1][1] *= -1.0f;
    // 2. Transform Z from [-1,1] (OpenGL) to [0,1] (Vulkan)
    //    new_z = old_z * 0.5 + 0.5
    lightProjection[2][2] = lightProjection[2][2] * 0.5f;
    lightProjection[3][2] = lightProjection[3][2] * 0.5f + 0.5f;

    return lightProjection * lightView;
}

void Renderer::updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera) {
    // Calculate cascade splits using PSSM
    const float shadowNear = 0.1f;
    const float shadowFar = 150.0f;  // Extended range for cascades
    const float lambda = 0.5f;  // 0.5 is good balance between log and uniform

    calculateCascadeSplits(shadowNear, shadowFar, lambda, cascadeSplitDepths);

    // Calculate light space matrix for each cascade
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        cascadeMatrices[i] = calculateCascadeMatrix(
            lightDir, camera,
            cascadeSplitDepths[i],
            cascadeSplitDepths[i + 1]
        );
    }
}

bool Renderer::createFramebuffers() {
    framebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool Renderer::createCommandPool() {
    auto queueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        SDL_Log("Failed to create command pool");
        return false;
    }

    return true;
}

bool Renderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool Renderer::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create sync objects");
            return false;
        }
    }

    return true;
}

bool Renderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowSamplerBinding{};
    shadowSamplerBinding.binding = 2;
    shadowSamplerBinding.descriptorCount = 1;
    shadowSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowSamplerBinding.pImmutableSamplers = nullptr;
    shadowSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalMapBinding{};
    normalMapBinding.binding = 3;
    normalMapBinding.descriptorCount = 1;
    normalMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalMapBinding.pImmutableSamplers = nullptr;
    normalMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightBufferBinding{};
    lightBufferBinding.binding = 4;
    lightBufferBinding.descriptorCount = 1;
    lightBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBufferBinding.pImmutableSamplers = nullptr;
    lightBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding emissiveMapBinding{};
    emissiveMapBinding.binding = 5;
    emissiveMapBinding.descriptorCount = 1;
    emissiveMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    emissiveMapBinding.pImmutableSamplers = nullptr;
    emissiveMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding pointShadowBinding{};
    pointShadowBinding.binding = 6;
    pointShadowBinding.descriptorCount = 1;
    pointShadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pointShadowBinding.pImmutableSamplers = nullptr;
    pointShadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding spotShadowBinding{};
    spotShadowBinding.binding = 7;
    spotShadowBinding.descriptorCount = 1;
    spotShadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    spotShadowBinding.pImmutableSamplers = nullptr;
    spotShadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 8> bindings = {uboLayoutBinding, samplerLayoutBinding, shadowSamplerBinding, normalMapBinding, lightBufferBinding, emissiveMapBinding, pointShadowBinding, spotShadowBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create descriptor set layout");
        return false;
    }

    return true;
}

bool Renderer::createGraphicsPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shader.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/shader.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = postProcessSystem.getHDRRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

bool Renderer::createSkyPipeline() {
    auto vertShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/sky.vert.spv");
    auto fragShaderCode = ShaderLoader::readFile(resourcePath + "/shaders/sky.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        SDL_Log("Failed to load sky shader files");
        return false;
    }

    VkShaderModule vertShaderModule = ShaderLoader::createShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = ShaderLoader::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = postProcessSystem.getHDRRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create sky pipeline");
        return false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    return true;
}

bool Renderer::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                            &uniformBuffersAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create uniform buffer");
            return false;
        }

        uniformBuffersMapped[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool Renderer::createLightBuffers() {
    VkDeviceSize bufferSize = sizeof(LightBuffer);

    lightBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    lightBufferAllocations.resize(MAX_FRAMES_IN_FLIGHT);
    lightBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &lightBuffers[i],
                            &lightBufferAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create light buffer");
            return false;
        }

        lightBuffersMapped[i] = allocationInfo.pMappedData;

        // Initialize with empty light buffer
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        memcpy(lightBuffersMapped[i], &emptyBuffer, sizeof(LightBuffer));
    }

    return true;
}

void Renderer::setupSceneLights() {
    // Clear any existing lights
    lightManager.clear();

    // Add the glowing orb point light (same as the hardcoded one)
    Light orbLight;
    orbLight.type = LightType::Point;
    orbLight.position = glm::vec3(2.0f, 1.3f, 0.0f);
    orbLight.color = glm::vec3(1.0f, 0.9f, 0.7f);  // Warm white
    orbLight.intensity = 5.0f;
    orbLight.radius = 8.0f;
    orbLight.priority = 10.0f;  // High priority - always visible
    lightManager.addLight(orbLight);

    // Add a few more example lights for testing
    Light blueLight;
    blueLight.type = LightType::Point;
    blueLight.position = glm::vec3(-3.0f, 2.0f, 2.0f);
    blueLight.color = glm::vec3(0.3f, 0.5f, 1.0f);  // Blue
    blueLight.intensity = 3.0f;
    blueLight.radius = 6.0f;
    blueLight.priority = 5.0f;
    lightManager.addLight(blueLight);

    Light greenLight;
    greenLight.type = LightType::Point;
    greenLight.position = glm::vec3(4.0f, 1.5f, -2.0f);
    greenLight.color = glm::vec3(0.4f, 1.0f, 0.4f);  // Green
    greenLight.intensity = 2.5f;
    greenLight.radius = 5.0f;
    greenLight.priority = 5.0f;
    lightManager.addLight(greenLight);
}

void Renderer::updateLightBuffer(uint32_t currentImage, const Camera& camera) {
    LightBuffer buffer{};
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    lightManager.buildLightBuffer(buffer, camera.getPosition(), camera.getFront(), viewProj, lightCullRadius);
    memcpy(lightBuffersMapped[currentImage], &buffer, sizeof(LightBuffer));
}

bool Renderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 12);  // +2 for post-process, +2 for grass, +4 for weather
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 26);  // diffuse + shadow + normal + emissive + HDR + grass + weather + dynamic point shadow + dynamic spot shadow
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 28);  // +6 grass, +6 light, +10 weather (5 compute + 2 graphics × 2 sets)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 18);  // +6 grass, +4 weather

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        SDL_Log("Failed to create descriptor pool");
        return false;
    }

    return true;
}

bool Renderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate descriptor sets");
        return false;
    }

    groundDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, groundDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate ground descriptor sets");
        return false;
    }

    metalDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, metalDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate metal descriptor sets");
        return false;
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo crateImageInfo{};
        crateImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        crateImageInfo.imageView = sceneBuilder.getCrateTexture().getImageView();
        crateImageInfo.sampler = sceneBuilder.getCrateTexture().getSampler();

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = shadowImageView;
        shadowImageInfo.sampler = shadowSampler;

        VkDescriptorImageInfo crateNormalImageInfo{};
        crateNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        crateNormalImageInfo.imageView = sceneBuilder.getCrateNormalMap().getImageView();
        crateNormalImageInfo.sampler = sceneBuilder.getCrateNormalMap().getSampler();

        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffers[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(LightBuffer);

        VkDescriptorImageInfo emissiveImageInfo{};
        emissiveImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        emissiveImageInfo.imageView = sceneBuilder.getDefaultEmissiveMap().getImageView();
        emissiveImageInfo.sampler = sceneBuilder.getDefaultEmissiveMap().getSampler();

        VkDescriptorImageInfo pointShadowImageInfo{};
        pointShadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pointShadowImageInfo.imageView = pointShadowArrayViews[i];
        pointShadowImageInfo.sampler = pointShadowSampler;

        VkDescriptorImageInfo spotShadowImageInfo{};
        spotShadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        spotShadowImageInfo.imageView = spotShadowArrayViews[i];
        spotShadowImageInfo.sampler = spotShadowSampler;

        std::array<VkWriteDescriptorSet, 8> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &crateImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowImageInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &crateNormalImageInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &lightBufferInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &emissiveImageInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = descriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &pointShadowImageInfo;

        descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[7].dstSet = descriptorSets[i];
        descriptorWrites[7].dstBinding = 7;
        descriptorWrites[7].dstArrayElement = 0;
        descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[7].descriptorCount = 1;
        descriptorWrites[7].pImageInfo = &spotShadowImageInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);

        // Ground descriptor sets
        VkDescriptorImageInfo groundImageInfo{};
        groundImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        groundImageInfo.imageView = sceneBuilder.getGroundTexture().getImageView();
        groundImageInfo.sampler = sceneBuilder.getGroundTexture().getSampler();

        VkDescriptorImageInfo groundNormalImageInfo{};
        groundNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        groundNormalImageInfo.imageView = sceneBuilder.getGroundNormalMap().getImageView();
        groundNormalImageInfo.sampler = sceneBuilder.getGroundNormalMap().getSampler();

        descriptorWrites[0].dstSet = groundDescriptorSets[i];
        descriptorWrites[1].dstSet = groundDescriptorSets[i];
        descriptorWrites[1].pImageInfo = &groundImageInfo;
        descriptorWrites[2].dstSet = groundDescriptorSets[i];
        descriptorWrites[3].dstSet = groundDescriptorSets[i];
        descriptorWrites[3].pImageInfo = &groundNormalImageInfo;
        descriptorWrites[4].dstSet = groundDescriptorSets[i];
        descriptorWrites[5].dstSet = groundDescriptorSets[i];
        descriptorWrites[6].dstSet = groundDescriptorSets[i];
        descriptorWrites[7].dstSet = groundDescriptorSets[i];

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);

        // Metal texture descriptor sets
        VkDescriptorImageInfo metalImageInfo{};
        metalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        metalImageInfo.imageView = sceneBuilder.getMetalTexture().getImageView();
        metalImageInfo.sampler = sceneBuilder.getMetalTexture().getSampler();

        VkDescriptorImageInfo metalNormalImageInfo{};
        metalNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        metalNormalImageInfo.imageView = sceneBuilder.getMetalNormalMap().getImageView();
        metalNormalImageInfo.sampler = sceneBuilder.getMetalNormalMap().getSampler();

        descriptorWrites[0].dstSet = metalDescriptorSets[i];
        descriptorWrites[1].dstSet = metalDescriptorSets[i];
        descriptorWrites[1].pImageInfo = &metalImageInfo;
        descriptorWrites[2].dstSet = metalDescriptorSets[i];
        descriptorWrites[3].dstSet = metalDescriptorSets[i];
        descriptorWrites[3].pImageInfo = &metalNormalImageInfo;
        descriptorWrites[4].dstSet = metalDescriptorSets[i];
        descriptorWrites[5].dstSet = metalDescriptorSets[i];
        descriptorWrites[6].dstSet = metalDescriptorSets[i];
        descriptorWrites[7].dstSet = metalDescriptorSets[i];

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    return true;
}


void Renderer::updateUniformBuffer(uint32_t currentImage, const Camera& camera) {
    // Update time of day (state mutation)
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    float cycleDuration = 120.0f;
    if (useManualTime) {
        currentTimeOfDay = manualTime;
    } else {
        currentTimeOfDay = fmod((time * timeScale) / cycleDuration, 1.0f);
    }

    // Pure calculations
    LightingParams lighting = calculateLightingParams(currentTimeOfDay);

    // Update cascade matrices (state mutation - modifies cascadeMatrices and cascadeSplitDepths)
    updateCascadeMatrices(lighting.sunDir, camera);

    // Build UBO data (pure calculation)
    UniformBufferObject ubo = buildUniformBufferData(camera, lighting, currentTimeOfDay);

    // State mutations
    lastSunIntensity = lighting.sunIntensity;
    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));

    // Update light buffer with camera-based culling
    updateLightBuffer(currentImage, camera);

    // Calculate sun screen position (pure) and update post-process (state mutation)
    glm::vec2 sunScreenPos = calculateSunScreenPos(camera, lighting.sunDir);
    postProcessSystem.setSunScreenPos(sunScreenPos);
}

void Renderer::render(const Camera& camera) {
    // Frame synchronization
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // Update uniform buffer data
    updateUniformBuffer(currentFrame, camera);

    // Calculate frame timing
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastTime = startTime;
    auto currentTime = std::chrono::high_resolution_clock::now();
    float grassTime = std::chrono::duration<float>(currentTime - startTime).count();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;

    // Update subsystems (state mutations)
    windSystem.update(deltaTime);
    windSystem.updateUniforms(currentFrame);

    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    grassSystem.updateUniforms(currentFrame, camera.getPosition(), viewProj);
    weatherSystem.updateUniforms(currentFrame, camera.getPosition(), viewProj, deltaTime, grassTime, windSystem);

    // Begin command buffer recording
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

    VkCommandBuffer cmd = commandBuffers[currentFrame];

    // Grass compute pass
    grassSystem.recordResetAndCompute(cmd, currentFrame, grassTime);

    // Weather particle compute pass
    weatherSystem.recordResetAndCompute(cmd, currentFrame, grassTime, deltaTime);

    // Shadow pass (skip when sun is below horizon)
    if (lastSunIntensity > 0.001f) {
        recordShadowPass(cmd, currentFrame, grassTime);
    }

    // Froxel volumetric fog compute pass
    {
        UniformBufferObject* ubo = static_cast<UniformBufferObject*>(uniformBuffersMapped[currentFrame]);
        glm::vec3 sunDir = glm::normalize(glm::vec3(ubo->sunDirection));
        float sunIntensity = ubo->sunDirection.w;
        glm::vec3 sunColor = glm::vec3(ubo->sunColor);

        froxelSystem.recordFroxelUpdate(cmd, currentFrame,
                                        camera.getViewMatrix(), camera.getProjectionMatrix(),
                                        camera.getPosition(),
                                        sunDir, sunIntensity, sunColor);

        postProcessSystem.setCameraPlanes(camera.getNearPlane(), camera.getFarPlane());
    }

    // HDR scene render pass
    recordHDRPass(cmd, currentFrame, grassTime);

    // Post-process pass
    postProcessSystem.recordPostProcess(cmd, currentFrame, framebuffers[imageIndex], deltaTime);

    vkEndCommandBuffer(cmd);

    // Queue submission
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);

    // Advance grass double-buffer sets after frame submission
    // This swaps compute/render buffer sets so next frame can overlap:
    // - Next frame's compute writes to what was the render set
    // - Next frame's render reads from what was the compute set (now contains fresh data)
    grassSystem.advanceBufferSet();
    weatherSystem.advanceBufferSet();

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::waitIdle() {
    vkDeviceWaitIdle(device);
}

// Pure calculation helpers - no state mutation

Renderer::LightingParams Renderer::calculateLightingParams(float timeOfDay) const {
    LightingParams params{};

    DateTime dateTime = DateTime::fromTimeOfDay(timeOfDay, currentYear, currentMonth, currentDay);
    CelestialPosition sunPos = celestialCalculator.calculateSunPosition(dateTime);
    MoonPosition moonPos = celestialCalculator.calculateMoonPosition(dateTime);

    params.sunDir = sunPos.direction;
    params.moonDir = moonPos.direction;
    params.sunIntensity = sunPos.intensity;
    params.moonIntensity = moonPos.intensity;

    // Smooth transition for moon as light source during twilight
    if (moonPos.altitude > -5.0f) {
        float twilightFactor = glm::smoothstep(10.0f, -6.0f, sunPos.altitude);
        params.moonIntensity *= (1.0f + twilightFactor * 1.0f);
    }

    params.sunColor = celestialCalculator.getSunColor(sunPos.altitude);
    params.moonColor = celestialCalculator.getMoonColor(moonPos.altitude, moonPos.illumination);
    params.ambientColor = celestialCalculator.getAmbientColor(sunPos.altitude);

    return params;
}

UniformBufferObject Renderer::buildUniformBufferData(const Camera& camera, const LightingParams& lighting, float timeOfDay) const {
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix();

    // Copy cascade matrices
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        ubo.cascadeViewProj[i] = cascadeMatrices[i];
    }

    // Store view-space split depths
    ubo.cascadeSplits = glm::vec4(
        cascadeSplitDepths[1],
        cascadeSplitDepths[2],
        cascadeSplitDepths[3],
        cascadeSplitDepths[4]
    );

    ubo.sunDirection = glm::vec4(lighting.sunDir, lighting.sunIntensity);
    ubo.moonDirection = glm::vec4(lighting.moonDir, lighting.moonIntensity);
    ubo.sunColor = glm::vec4(lighting.sunColor, 1.0f);
    ubo.moonColor = glm::vec4(lighting.moonColor, 1.0f);
    ubo.ambientColor = glm::vec4(lighting.ambientColor, 1.0f);
    ubo.cameraPosition = glm::vec4(camera.getPosition(), 1.0f);

    // Point light from the glowing sphere
    glm::vec3 pointLightPos = glm::vec3(2.0f, 1.3f, 0.0f);
    float pointLightIntensity = 5.0f;
    float pointLightRadius = 8.0f;
    ubo.pointLightPosition = glm::vec4(pointLightPos, pointLightIntensity);
    ubo.pointLightColor = glm::vec4(1.0f, 0.9f, 0.7f, pointLightRadius);

    ubo.timeOfDay = timeOfDay;
    ubo.shadowMapSize = static_cast<float>(SHADOW_MAP_SIZE);
    ubo.debugCascades = showCascadeDebug ? 1.0f : 0.0f;

    return ubo;
}

glm::vec2 Renderer::calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const {
    glm::vec3 sunWorldPos = camera.getPosition() + sunDir * 1000.0f;
    glm::vec4 sunClipPos = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(sunWorldPos, 1.0f);

    glm::vec2 sunScreenPos(0.5f, 0.5f);
    if (sunClipPos.w > 0.0f) {
        glm::vec3 sunNDC = glm::vec3(sunClipPos) / sunClipPos.w;
        sunScreenPos = glm::vec2(sunNDC.x * 0.5f + 0.5f, sunNDC.y * 0.5f + 0.5f);
        sunScreenPos.y = 1.0f - sunScreenPos.y;
    }
    return sunScreenPos;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = shadowRenderPass;
        shadowPassInfo.framebuffer = cascadeFramebuffers[cascade];
        shadowPassInfo.renderArea.offset = {0, 0};
        shadowPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;

        vkCmdBeginRenderPass(cmd, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shadowPipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);

        for (const auto& obj : sceneBuilder.getSceneObjects()) {
            if (!obj.castsShadow) continue;

            ShadowPushConstants shadowPush{};
            shadowPush.model = obj.transform;
            shadowPush.cascadeIndex = static_cast<int>(cascade);
            vkCmdPushConstants(cmd, shadowPipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &shadowPush);

            VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
        }

        grassSystem.recordShadowDraw(cmd, frameIndex, grassTime, cascade);

        vkCmdEndRenderPass(cmd);
    }
}

void Renderer::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
    for (const auto& obj : sceneBuilder.getSceneObjects()) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);  // alpha=1 uses emissive color

        vkCmdPushConstants(cmd, pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &push);

        // Select descriptor set based on texture
        VkDescriptorSet* descSet;
        if (obj.texture == &sceneBuilder.getGroundTexture()) {
            descSet = &groundDescriptorSets[frameIndex];
        } else if (obj.texture == &sceneBuilder.getMetalTexture()) {
            descSet = &metalDescriptorSets[frameIndex];
        } else {
            descSet = &descriptorSets[frameIndex];
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, descSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
    }
}

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    VkRenderPassBeginInfo hdrPassInfo{};
    hdrPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    hdrPassInfo.renderPass = postProcessSystem.getHDRRenderPass();
    hdrPassInfo.framebuffer = postProcessSystem.getHDRFramebuffer();
    hdrPassInfo.renderArea.offset = {0, 0};
    hdrPassInfo.renderArea.extent = postProcessSystem.getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    hdrPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    hdrPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &hdrPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Draw sky
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Draw scene objects
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    recordSceneObjects(cmd, frameIndex);

    // Draw grass
    grassSystem.recordDraw(cmd, frameIndex, grassTime);

    // Draw weather particles (rain/snow) - after opaque geometry
    weatherSystem.recordDraw(cmd, frameIndex, grassTime);

    vkCmdEndRenderPass(cmd);
}
